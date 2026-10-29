"""Simulador de la base para Firebase Realtime Database.

Escribe por la API REST exactamente como lo hará la base (docs/modelo-datos.md): estado cada
minuto, una lectura cada 5 s, heartbeat del colgante y eventos de ejemplo. Inicia sesión con la
cuenta de la base (Firebase Auth, signInWithPassword), renueva el token antes de que venza y
vuelve a iniciar sesión ante un 401.

Modos:
    normal         la base funciona normal y crea un evento BOTON a los 30 s
    desconectada   escribe un rato, deja de escribir (el panel debe mostrar "base desconectada")
                   y al volver crea BASE_DESCONECTADA con la duración, como la base real
    bateria-baja   el colgante reporta batería baja y se crea un evento BATERIA_BAJA
    verificar      prueba las reglas: casos que deben pasar y casos que deben fallar

Credenciales en credenciales.local.json (plantilla: credenciales.example.json).
Solo usa la librería estándar de Python.
"""

import argparse
import json
import random
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

URL_INICIO_SESION = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword"
URL_RENOVAR_TOKEN = "https://securetoken.googleapis.com/v1/token"

# Mismos periodos que la base (firmware/base-radar/include/config.h y docs/protocolo.md).
ESTADO_INTERVALO_S = 60
LECTURA_INTERVALO_S = 5
HEARTBEAT_INTERVALO_S = 900

# El token dura 1 h; se renueva 5 min antes.
MARGEN_RENOVACION_S = 300

# Umbral de actividad de "persona detectada" (docs/radar.md, 13.6; docs/fallos.md, escenario 8).
UMBRAL_ACTIVIDAD = 0.03

COLGANTE_ID = 1
BATERIA_NORMAL_MV = 3950
BATERIA_BAJA_MV = 3400  # por debajo de BATERIA_BAJA_MV (3500) de docs/protocolo.md

HORA_SERVIDOR = {".sv": "timestamp"}
TIEMPO_ESPERA_HTTP_S = 15


def registrar(mensaje):
    print(f"{time.strftime('%H:%M:%S')}  {mensaje}", flush=True)


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------

def peticion(metodo, url, cuerpo_json=None, formulario=None):
    """Hace una petición HTTP. Devuelve (código, respuesta JSON o None). Código 0 = error de red."""
    datos = None
    cabeceras = {}
    if cuerpo_json is not None:
        datos = json.dumps(cuerpo_json).encode("utf-8")
        cabeceras["Content-Type"] = "application/json"
    elif formulario is not None:
        datos = urllib.parse.urlencode(formulario).encode("utf-8")
        cabeceras["Content-Type"] = "application/x-www-form-urlencoded"
    solicitud = urllib.request.Request(url, data=datos, headers=cabeceras, method=metodo)
    try:
        with urllib.request.urlopen(solicitud, timeout=TIEMPO_ESPERA_HTTP_S) as respuesta:
            return respuesta.status, leer_json(respuesta.read())
    except urllib.error.HTTPError as error:
        return error.code, leer_json(error.read())
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        return 0, {"error": str(error)}


def leer_json(contenido):
    if not contenido:
        return None
    try:
        return json.loads(contenido)
    except ValueError:
        return {"error": contenido.decode("utf-8", "replace")}


def url_bd(database_url, ruta, token=None, silencioso=False, parametros=None):
    """URL de la API REST de Realtime Database para una ruta (sin barra inicial)."""
    consulta = dict(parametros or {})
    if token:
        consulta["auth"] = token
    if silencioso:
        consulta["print"] = "silent"
    url = f"{database_url.rstrip('/')}/{ruta}.json"
    if consulta:
        url += "?" + urllib.parse.urlencode(consulta)
    return url


def texto_error(respuesta):
    if isinstance(respuesta, dict):
        error = respuesta.get("error")
        if isinstance(error, dict):  # formato de Firebase Auth
            return error.get("message", str(error))
        if error:
            return str(error)
    return ""


# ---------------------------------------------------------------------------
# Sesión de Firebase Auth
# ---------------------------------------------------------------------------

class ErrorSesion(Exception):
    pass


class Sesion:
    """Cuenta de Firebase Auth con correo y contraseña. Renueva el idToken antes de que venza."""

    def __init__(self, api_key, correo, clave):
        self.api_key = api_key
        self.correo = correo
        self.clave = clave
        self.id_token = None
        self.token_renovacion = None
        self.uid = None
        self.vence = 0.0

    def iniciar(self):
        codigo, respuesta = peticion(
            "POST",
            f"{URL_INICIO_SESION}?key={self.api_key}",
            cuerpo_json={"email": self.correo, "password": self.clave, "returnSecureToken": True},
        )
        if codigo != 200:
            raise ErrorSesion(f"no se pudo iniciar sesión con {self.correo}: "
                              f"HTTP {codigo} {texto_error(respuesta)}")
        self.id_token = respuesta["idToken"]
        self.token_renovacion = respuesta["refreshToken"]
        self.uid = respuesta["localId"]
        self.vence = time.monotonic() + int(respuesta["expiresIn"])
        registrar(f"sesión iniciada: {self.correo} (uid {self.uid})")

    def renovar(self):
        codigo, respuesta = peticion(
            "POST",
            f"{URL_RENOVAR_TOKEN}?key={self.api_key}",
            formulario={"grant_type": "refresh_token", "refresh_token": self.token_renovacion},
        )
        if codigo != 200:
            registrar(f"no se pudo renovar el token (HTTP {codigo} {texto_error(respuesta)}); "
                      "se vuelve a iniciar sesión")
            self.iniciar()
            return
        self.id_token = respuesta["id_token"]
        self.token_renovacion = respuesta["refresh_token"]
        self.vence = time.monotonic() + int(respuesta["expires_in"])
        registrar("token renovado")

    def token(self):
        if self.id_token is None:
            self.iniciar()
        elif time.monotonic() > self.vence - MARGEN_RENOVACION_S:
            self.renovar()
        return self.id_token


# ---------------------------------------------------------------------------
# Base simulada
# ---------------------------------------------------------------------------

class BaseSimulada:
    """Escribe en Firebase como la base: mismas rutas, mismos campos, hora del servidor."""

    def __init__(self, database_url, base_id, sesion):
        self.database_url = database_url
        self.base_id = base_id
        self.sesion = sesion
        # Referencia de hora del servidor (docs/modelo-datos.md, sección 2).
        self.hora_servidor_ref = None
        self.monotonic_ref = 0.0
        self.bateria_mv = BATERIA_NORMAL_MV
        # Estado del radar simulado.
        self.objetivo = True
        self.distancia_cm = 55.0

    def escribir(self, metodo, ruta, datos):
        """PUT/PATCH/POST con ?print=silent. Ante un 401 inicia sesión otra vez y reintenta una vez."""
        for intento in range(2):
            url = url_bd(self.database_url, ruta, self.sesion.token(), silencioso=True)
            codigo, respuesta = peticion(metodo, url, cuerpo_json=datos)
            if codigo == 401 and intento == 0:
                registrar(f"401 en {ruta} ({texto_error(respuesta)}); se vuelve a iniciar sesión")
                self.sesion.iniciar()
                continue
            if not 200 <= codigo < 300:
                registrar(f"ERROR {metodo} {ruta}: HTTP {codigo} {texto_error(respuesta)}")
                return False
            return True
        return False

    def leer(self, ruta):
        for intento in range(2):
            url = url_bd(self.database_url, ruta, self.sesion.token())
            codigo, respuesta = peticion("GET", url)
            if codigo == 401 and intento == 0:
                self.sesion.iniciar()
                continue
            if codigo != 200:
                registrar(f"ERROR GET {ruta}: HTTP {codigo} {texto_error(respuesta)}")
                return None
            return respuesta
        return None

    def ruta_estado(self):
        return f"dispositivos/{self.base_id}/estado"

    def ultimo_contacto(self):
        return self.leer(f"{self.ruta_estado()}/ultimo_contacto")

    def enviar_estado(self):
        """PATCH del estado y lectura de vuelta de ultimo_contacto como referencia de hora."""
        datos = {
            "online": True,
            "ultimo_contacto": HORA_SERVIDOR,
            "rssi_wifi": random.randint(-70, -45),
            "version_firmware": "simulador",
        }
        if not self.escribir("PATCH", self.ruta_estado(), datos):
            return False
        hora = self.ultimo_contacto()
        if isinstance(hora, (int, float)):
            self.hora_servidor_ref = int(hora)
            self.monotonic_ref = time.monotonic()
        registrar(f"estado enviado (ultimo_contacto {hora})")
        return True

    def hora_servidor_ms(self):
        if self.hora_servidor_ref is None:
            return 0
        return self.hora_servidor_ref + int((time.monotonic() - self.monotonic_ref) * 1000)

    def generar_lectura(self):
        """Lectura sintética con los campos de docs/modelo-datos.md, sección 1.1."""
        # El radar pierde y recupera el objetivo por tramos (docs/radar.md, 13.1).
        if self.objetivo and random.random() < 0.02:
            self.objetivo = False
        elif not self.objetivo and random.random() < 0.05:
            self.objetivo = True

        if not self.objetivo:
            return {"persona": False, "objetivo": False}

        if random.random() < 0.03:
            actividad = random.uniform(0.15, 0.3)  # se acomoda en la cama
        else:
            actividad = random.lognormvariate(-3.0, 0.45)  # mediana ~0.05, a veces < 0.03
        actividad = round(min(actividad, 10.0), 3)
        self.distancia_cm = min(max(self.distancia_cm + random.gauss(0, 1.0), 35.0), 90.0)

        lectura = {
            "persona": actividad >= UMBRAL_ACTIVIDAD,
            "objetivo": True,
            "actividad": actividad,
            "distancia_cm": round(self.distancia_cm),
        }
        # El módulo da 0 la mayor parte del sueño (docs/radar.md, 13.5); con 0 no se envía.
        if random.random() < 0.45:
            lectura["resp_rpm"] = round(min(max(random.gauss(13.5, 1.5), 6.0), 30.0), 1)
        if random.random() < 0.40:
            lectura["latido_bpm"] = round(min(max(random.gauss(68, 5), 45), 110))
        return lectura

    def enviar_lectura(self):
        ts = self.hora_servidor_ms()
        if ts == 0:
            registrar("lectura no enviada: todavía no hay referencia de hora del servidor")
            return False
        lectura = self.generar_lectura()
        ok = self.escribir("PUT", f"lecturas/{self.base_id}/{ts:013d}", lectura)
        if ok:
            registrar(f"lectura {ts:013d} {json.dumps(lectura)}")
        return ok

    def enviar_heartbeat(self):
        datos = {
            "bateria_mv": self.bateria_mv,
            "ultimo_heartbeat": HORA_SERVIDOR,
            "rssi": random.randint(-80, -55),
            "base": self.base_id,
        }
        ok = self.escribir("PATCH", f"colgantes/{COLGANTE_ID}", datos)
        if ok:
            registrar(f"heartbeat del colgante {COLGANTE_ID} ({self.bateria_mv} mV)")
        return ok

    def enviar_evento(self, tipo, origen, detalle):
        datos = {
            "tipo": tipo,
            "origen": origen,
            "timestamp": HORA_SERVIDOR,
            "atendido": False,
            "detalle": detalle,
        }
        ok = self.escribir("POST", "eventos", datos)
        if ok:
            registrar(f"evento {tipo} de {origen}: {detalle}")
        return ok


def simular(base, modo, duracion_s, heartbeat_s, boton_cada_s, antes_corte_s, corte_s):
    inicio = time.monotonic()
    origen_colgante = f"colgante{COLGANTE_ID}"

    if modo == "bateria-baja":
        base.bateria_mv = BATERIA_BAJA_MV

    base.enviar_estado()
    base.enviar_heartbeat()
    if modo == "bateria-baja":
        # Una vez por descarga (docs/fallos.md, escenario 7), no en cada heartbeat.
        base.enviar_evento("BATERIA_BAJA", origen_colgante,
                           f"simulador: batería del colgante en {base.bateria_mv} mV")

    proximo_estado = inicio + ESTADO_INTERVALO_S
    proxima_lectura = inicio
    proximo_heartbeat = inicio + heartbeat_s
    proximo_boton = inicio + 30 if modo == "normal" else None
    corte_desde = inicio + antes_corte_s if modo == "desconectada" else None
    corte_hasta = corte_desde + corte_s if corte_desde is not None else None
    ultimo_contacto_antes_corte = None

    while True:
        ahora = time.monotonic()
        if duracion_s and ahora - inicio >= duracion_s:
            registrar("fin de la simulación")
            return

        if corte_desde is not None and corte_desde <= ahora < corte_hasta:
            if ultimo_contacto_antes_corte is None:
                # El último ultimo_contacto que la base leyó de vuelta antes de perder la red.
                ultimo_contacto_antes_corte = base.hora_servidor_ref
                registrar(f"base desconectada: deja de escribir por {corte_s} s "
                          f"(el panel la marca desconectada a los 3 min)")
            time.sleep(min(1.0, corte_hasta - ahora))
            continue

        if corte_hasta is not None and ahora >= corte_hasta:
            # Al volver, la base compara la hora del servidor con su último contacto
            # (docs/fallos.md, escenario 6).
            corte_desde = corte_hasta = None
            base.enviar_estado()
            if isinstance(ultimo_contacto_antes_corte, (int, float)):
                minutos = (base.hora_servidor_ms() - ultimo_contacto_antes_corte) / 60000
                detalle = f"simulador: sin conexión por {minutos:.0f} min"
            else:
                detalle = "simulador: sin conexión (duración desconocida)"
            base.enviar_evento("BASE_DESCONECTADA", base.base_id, detalle)
            proximo_estado = ahora + ESTADO_INTERVALO_S
            proxima_lectura = ahora

        if ahora >= proximo_estado:
            base.enviar_estado()
            proximo_estado += ESTADO_INTERVALO_S
        if ahora >= proxima_lectura:
            base.enviar_lectura()
            proxima_lectura += LECTURA_INTERVALO_S
            if proxima_lectura < ahora:  # no recuperar lecturas atrasadas de golpe
                proxima_lectura = ahora + LECTURA_INTERVALO_S
        if ahora >= proximo_heartbeat:
            base.enviar_heartbeat()
            proximo_heartbeat += heartbeat_s
        if proximo_boton is not None and ahora >= proximo_boton:
            base.enviar_evento("BOTON", origen_colgante, "simulador: botón del colgante")
            proximo_boton = proximo_boton + boton_cada_s if boton_cada_s else None

        time.sleep(0.2)


# ---------------------------------------------------------------------------
# Verificación de reglas
# ---------------------------------------------------------------------------

class Verificacion:
    def __init__(self, database_url):
        self.database_url = database_url
        self.resultados = []  # (ok, nombre)

    def http(self, metodo, ruta, token, datos=None, parametros=None, silencioso=True):
        url = url_bd(self.database_url, ruta, token, silencioso=silencioso and metodo != "GET",
                     parametros=parametros)
        return peticion(metodo, url, cuerpo_json=datos)

    def caso(self, nombre, debe_pasar, metodo, ruta, token, datos=None, parametros=None,
             silencioso=True):
        codigo, respuesta = self.http(metodo, ruta, token, datos, parametros, silencioso)
        if debe_pasar:
            ok = 200 <= codigo < 300
        else:
            # Las reglas rechazan con 401 "Permission denied" (403 en algunos casos).
            ok = codigo in (401, 403)
        esperado = "debe pasar" if debe_pasar else "debe fallar"
        detalle = f"HTTP {codigo}"
        if texto_error(respuesta):
            detalle += f" {texto_error(respuesta)}"
        print(f"{'OK   ' if ok else 'FALLO'}  [{esperado}] {nombre} ({detalle})", flush=True)
        self.resultados.append((ok, nombre))
        return codigo, respuesta

    def resumen(self):
        fallos = [nombre for ok, nombre in self.resultados if not ok]
        print()
        print(f"{len(self.resultados) - len(fallos)} OK, {len(fallos)} FALLO "
              f"de {len(self.resultados)} casos")
        for nombre in fallos:
            print(f"  FALLO: {nombre}")
        return not fallos


def verificar(credenciales):
    database_url = credenciales["database_url"]
    base_id = credenciales["base_id"]
    otra_base = "base99"  # no existe en /cuentas_dispositivo
    v = Verificacion(database_url)

    sesion_base = Sesion(credenciales["api_key"], credenciales["base"]["correo"],
                         credenciales["base"]["clave"])
    sesion_base.iniciar()
    base = BaseSimulada(database_url, base_id, sesion_base)
    tb = sesion_base.token()

    print()
    print("== Base: lo que debe pasar ==")
    v.caso("base actualiza su estado", True, "PATCH", f"dispositivos/{base_id}/estado", tb,
           {"online": True, "ultimo_contacto": HORA_SERVIDOR, "rssi_wifi": -60,
            "version_firmware": "verificacion"})
    v.caso("base lee su ultimo_contacto", True, "GET",
           f"dispositivos/{base_id}/estado/ultimo_contacto", tb)
    hora = base.ultimo_contacto()
    if not isinstance(hora, (int, float)):
        print("No se pudo leer la hora del servidor; se detiene la verificación.")
        return False
    ts_completa = f"{int(hora):013d}"
    ts_minima = f"{int(hora) + 1:013d}"
    lectura_valida = {"persona": True, "objetivo": True, "actividad": 0.048, "distancia_cm": 85,
                      "resp_rpm": 13, "latido_bpm": 72}
    v.caso("base escribe una lectura completa", True, "PUT",
           f"lecturas/{base_id}/{ts_completa}", tb, lectura_valida)
    v.caso("base escribe una lectura sin objetivo (solo campos obligatorios)", True, "PUT",
           f"lecturas/{base_id}/{ts_minima}", tb, {"persona": False, "objetivo": False})
    v.caso("base lee sus lecturas (última)", True, "GET", f"lecturas/{base_id}", tb,
           parametros={"orderBy": '"$key"', "limitToLast": 1})
    v.caso("base actualiza el colgante", True, "PATCH", f"colgantes/{COLGANTE_ID}", tb,
           {"bateria_mv": BATERIA_NORMAL_MV, "ultimo_heartbeat": HORA_SERVIDOR, "rssi": -70,
            "base": base_id})
    evento = {"tipo": "BOTON", "origen": f"colgante{COLGANTE_ID}", "timestamp": HORA_SERVIDOR,
              "atendido": False, "detalle": "verificacion de reglas"}
    _, respuesta = v.caso("base crea un evento BOTON", True, "POST", "eventos", tb, evento,
                          silencioso=False)
    evento_id = respuesta.get("name") if isinstance(respuesta, dict) else None

    print()
    print("== Sin sesión: todo debe fallar ==")
    v.caso("sin token lee lecturas", False, "GET", f"lecturas/{base_id}", None,
           parametros={"orderBy": '"$key"', "limitToLast": 1})
    v.caso("sin token lee estado", False, "GET", f"dispositivos/{base_id}/estado", None)
    v.caso("sin token lee eventos", False, "GET", "eventos", None,
           parametros={"orderBy": '"timestamp"', "limitToLast": 1})
    v.caso("sin token escribe estado", False, "PATCH", f"dispositivos/{base_id}/estado", None,
           {"online": True, "ultimo_contacto": HORA_SERVIDOR})
    v.caso("sin token escribe lectura", False, "PUT", f"lecturas/{base_id}/{ts_completa}", None,
           lectura_valida)

    print()
    print("== Base: lo que debe fallar ==")
    v.caso("base escribe lectura en la ruta de otra base", False, "PUT",
           f"lecturas/{otra_base}/{ts_completa}", tb, lectura_valida)
    v.caso("base escribe el estado de otra base", False, "PATCH",
           f"dispositivos/{otra_base}/estado", tb,
           {"online": True, "ultimo_contacto": HORA_SERVIDOR})
    v.caso("base lee el estado de otra base", False, "GET", f"dispositivos/{otra_base}/estado", tb)
    v.caso("lectura con campo no definido", False, "PUT", f"lecturas/{base_id}/{ts_completa}", tb,
           dict(lectura_valida, temperatura=36))
    v.caso("lectura sin el campo obligatorio 'objetivo'", False, "PUT",
           f"lecturas/{base_id}/{ts_completa}", tb, {"persona": False})
    v.caso("lectura con persona true y objetivo false", False, "PUT",
           f"lecturas/{base_id}/{ts_completa}", tb, {"persona": True, "objetivo": False})
    v.caso("lectura con resp_rpm = 0 (debe omitirse)", False, "PUT",
           f"lecturas/{base_id}/{ts_completa}", tb, dict(lectura_valida, resp_rpm=0))
    v.caso("lectura con resp_rpm fuera de rango (99)", False, "PUT",
           f"lecturas/{base_id}/{ts_completa}", tb, dict(lectura_valida, resp_rpm=99))
    v.caso("lectura con persona de tipo texto", False, "PUT",
           f"lecturas/{base_id}/{ts_completa}", tb, dict(lectura_valida, persona="si"))
    v.caso("lectura con clave de 10 dígitos", False, "PUT",
           f"lecturas/{base_id}/{ts_completa[:10]}", tb, lectura_valida)
    v.caso("estado con hora inventada en ultimo_contacto", False, "PATCH",
           f"dispositivos/{base_id}/estado", tb, {"online": True, "ultimo_contacto": 1000})
    v.caso("estado con campo no definido", False, "PATCH", f"dispositivos/{base_id}/estado", tb,
           {"online": True, "ultimo_contacto": HORA_SERVIDOR, "ip": "192.168.1.10"})
    v.caso("colgante con la base de otra cuenta", False, "PATCH", f"colgantes/{COLGANTE_ID}", tb,
           {"bateria_mv": 3900, "ultimo_heartbeat": HORA_SERVIDOR, "base": otra_base})
    v.caso("evento APNEA (tipo reservado)", False, "POST", "eventos", tb,
           dict(evento, tipo="APNEA"))
    v.caso("evento de tipo desconocido", False, "POST", "eventos", tb, dict(evento, tipo="OTRO"))
    v.caso("evento creado ya atendido", False, "POST", "eventos", tb, dict(evento, atendido=True))
    v.caso("evento a nombre de otra base", False, "POST", "eventos", tb,
           dict(evento, origen=otra_base))
    v.caso("evento con atendido_por puesto por la base", False, "POST", "eventos", tb,
           dict(evento, atendido_por=sesion_base.uid))
    v.caso("base lee eventos", False, "GET", "eventos", tb,
           parametros={"orderBy": '"timestamp"', "limitToLast": 1})
    v.caso("base lee colgantes", False, "GET", "colgantes", tb)
    v.caso("base lee /cuentas_dispositivo", False, "GET", "cuentas_dispositivo", tb)
    v.caso("base lee /usuarios_panel", False, "GET", "usuarios_panel", tb)
    v.caso("base escribe en /usuarios_panel", False, "PUT",
           f"usuarios_panel/{sesion_base.uid}", tb, True)
    if evento_id:
        v.caso("base marca un evento como atendido", False, "PUT",
               f"eventos/{evento_id}/atendido", tb, True)
        v.caso("base sobrescribe un evento existente", False, "PUT", f"eventos/{evento_id}", tb,
               evento)
        v.caso("base borra un evento", False, "DELETE", f"eventos/{evento_id}", tb)

    cuidador = credenciales.get("cuidador") or {}
    if cuidador.get("correo") and cuidador.get("clave"):
        sesion_cuidador = Sesion(credenciales["api_key"], cuidador["correo"], cuidador["clave"])
        sesion_cuidador.iniciar()
        tc = sesion_cuidador.token()

        print()
        print("== Cuidador: solo lee y marca eventos como atendidos ==")
        v.caso("cuidador lee la última lectura", True, "GET", f"lecturas/{base_id}", tc,
               parametros={"orderBy": '"$key"', "limitToLast": 1})
        v.caso("cuidador lee el estado de la base", True, "GET", f"dispositivos/{base_id}/estado",
               tc)
        v.caso("cuidador lee colgantes", True, "GET", "colgantes", tc)
        v.caso("cuidador lee el último evento", True, "GET", "eventos", tc,
               parametros={"orderBy": '"timestamp"', "limitToLast": 1})
        # Las dos consultas que hace el panel (docs/panel.md). Sin ".indexOn" de "atendido" la
        # API REST responde 400 en vez de filtrar.
        v.caso("cuidador consulta eventos sin atender (índice de atendido)", True, "GET",
               "eventos", tc, parametros={"orderBy": '"atendido"', "equalTo": "false"})
        hace_un_minuto = int(time.time() * 1000) - 60_000
        v.caso("cuidador lee lecturas por rango de claves", True, "GET", f"lecturas/{base_id}",
               tc, parametros={"orderBy": '"$key"', "startAt": f'"{hace_un_minuto:013d}"'})
        v.caso("cuidador escribe una lectura", False, "PUT", f"lecturas/{base_id}/{ts_completa}",
               tc, lectura_valida)
        v.caso("cuidador borra lecturas", False, "DELETE", f"lecturas/{base_id}/{ts_completa}", tc)
        v.caso("cuidador escribe el estado de la base", False, "PATCH",
               f"dispositivos/{base_id}/estado", tc,
               {"online": True, "ultimo_contacto": HORA_SERVIDOR})
        v.caso("cuidador actualiza un colgante", False, "PATCH", f"colgantes/{COLGANTE_ID}", tc,
               {"bateria_mv": 3900, "ultimo_heartbeat": HORA_SERVIDOR})
        v.caso("cuidador crea un evento", False, "POST", "eventos", tc,
               dict(evento, origen=base_id))
        v.caso("cuidador lee /usuarios_panel", False, "GET", "usuarios_panel", tc)
        v.caso("cuidador lee /cuentas_dispositivo", False, "GET", "cuentas_dispositivo", tc)
        if evento_id:
            v.caso("cuidador marca el evento como atendido", True, "PATCH",
                   f"eventos/{evento_id}", tc,
                   {"atendido": True, "atendido_por": sesion_cuidador.uid,
                    "atendido_en": HORA_SERVIDOR})
            v.caso("cuidador vuelve atendido a false", False, "PUT",
                   f"eventos/{evento_id}/atendido", tc, False)
            v.caso("cuidador cambia atendido_por", False, "PUT",
                   f"eventos/{evento_id}/atendido_por", tc, sesion_cuidador.uid)
            v.caso("cuidador cambia el tipo del evento", False, "PUT",
                   f"eventos/{evento_id}/tipo", tc, "BATERIA_BAJA")
            v.caso("cuidador borra el evento", False, "DELETE", f"eventos/{evento_id}", tc)
    else:
        print()
        print("== Cuidador: sin credenciales en el archivo, se omiten sus casos ==")

    print()
    print("== Limpieza ==")
    v.caso("base borra sus lecturas de prueba", True, "PATCH", f"lecturas/{base_id}", tb,
           {ts_completa: None, ts_minima: None})
    if evento_id:
        print(f"El evento de prueba /eventos/{evento_id} (detalle 'verificacion de reglas') queda "
              "en la base de datos: ni la base ni el cuidador pueden borrarlo. Se borra desde la "
              "consola si molesta.")

    return v.resumen()


# ---------------------------------------------------------------------------
# Entrada
# ---------------------------------------------------------------------------

def cargar_credenciales(ruta):
    try:
        with open(ruta, encoding="utf-8") as archivo:
            credenciales = json.load(archivo)
    except FileNotFoundError:
        sys.exit(f"No existe {ruta}. Copia credenciales.example.json como "
                 "credenciales.local.json y llénalo.")
    faltan = [clave for clave in ("api_key", "database_url", "base_id", "base")
              if not credenciales.get(clave)]
    base = credenciales.get("base") or {}
    faltan += [f"base.{clave}" for clave in ("correo", "clave") if not base.get(clave)]
    if faltan:
        sys.exit(f"Faltan datos en {ruta}: {', '.join(faltan)}")
    return credenciales


def main():
    analizador = argparse.ArgumentParser(
        description="Simula la base escribiendo en Firebase y verifica las reglas.")
    analizador.add_argument("modo", choices=["normal", "desconectada", "bateria-baja", "verificar"])
    analizador.add_argument("--credenciales", type=Path,
                            default=Path(__file__).with_name("credenciales.local.json"),
                            help="archivo de credenciales (por defecto credenciales.local.json)")
    analizador.add_argument("--duracion", type=int, default=0,
                            help="segundos de simulación (0 = hasta Ctrl+C)")
    analizador.add_argument("--heartbeat", type=int, default=HEARTBEAT_INTERVALO_S,
                            help=f"periodo del heartbeat del colgante en s "
                                 f"(por defecto {HEARTBEAT_INTERVALO_S})")
    analizador.add_argument("--boton-cada", type=int, default=0,
                            help="modo normal: repetir el evento BOTON cada N s "
                                 "(0 = solo uno a los 30 s)")
    analizador.add_argument("--antes", type=int, default=60,
                            help="modo desconectada: segundos escribiendo antes del corte")
    analizador.add_argument("--corte", type=int, default=240,
                            help="modo desconectada: segundos sin escribir (más de 180 para que "
                                 "el panel la marque desconectada)")
    argumentos = analizador.parse_args()

    credenciales = cargar_credenciales(argumentos.credenciales)
    try:
        if argumentos.modo == "verificar":
            sys.exit(0 if verificar(credenciales) else 1)
        sesion = Sesion(credenciales["api_key"], credenciales["base"]["correo"],
                        credenciales["base"]["clave"])
        sesion.iniciar()
        base = BaseSimulada(credenciales["database_url"], credenciales["base_id"], sesion)
        simular(base, argumentos.modo, argumentos.duracion, argumentos.heartbeat,
                argumentos.boton_cada, argumentos.antes, argumentos.corte)
    except ErrorSesion as error:
        sys.exit(f"Error de sesión: {error}")
    except KeyboardInterrupt:
        registrar("detenido con Ctrl+C")


if __name__ == "__main__":
    main()
