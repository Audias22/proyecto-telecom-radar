"""Detector de apnea por caída de amplitud o de actividad, con validación cruzada.

Uso:
    python detectar_apnea.py

El detector no estima la frecuencia. Una vez por segundo calcula una medida corta (últimos
VENTANA_CORTA_S segundos) y la compara con una referencia: la mediana de esa medida en los últimos
REFERENCIA_S segundos sin sospecha. Si, con presencia, la medida queda por debajo de
umbral × referencia durante una duración mínima, da la alarma.

Medidas (MEDIDAS):
    rms_fase_resp         RMS de fase_resp filtrada con el pasabanda causal (variante original)
    actividad_fase_total  mediana de |fase_total| en las tramas con dato (fase_total se comporta
                          como el incremento de fase entre tramas: mide qué tan rápido se mueve el
                          pecho). Sin filtro.

- Todo es causal y por muestra (como correría en el ESP32): el retraso medido es el real.
- La referencia se congela mientras la medida está baja, para que la apnea no la contamine.
- Solo se arma con REFERENCIA_MIN_S de historia y si la referencia supera el mínimo de una persona
  (ARMADO_MIN): un objeto quieto nunca "respiró", así que no puede dejar de hacerlo.

Validación cruzada: con solo dos apneas, el umbral y la duración se eligen con una (rejilla
REJILLA_UMBRAL × REJILLA_DURACION_S) y se evalúan en la otra, y al revés. Las falsas alarmas se
cuentan en todas las capturas sin apnea.

Salidas en pruebas/resultados/deteccion_apnea/:
    apnea_acostado1.png, apnea_acostado2.png  señales en el tiempo con la apnea marcada
    actividad.png                             actividad / referencia en todas las capturas
    deteccion.md                              escenas, validación cruzada y resultados
"""

from collections import deque
from pathlib import Path

import numpy as np
from scipy import signal

import estimar_respiracion as er

# --- Parámetros ----------------------------------------------------------------------------

VENTANA_CORTA_S = 5.0      # ventana de la medida corta
REFERENCIA_S = 45.0        # historia con la que se calcula la referencia
REFERENCIA_MIN_S = 30.0    # historia mínima antes de armar el detector
PASO_S = 1.0               # el detector se evalúa una vez por segundo
MARGEN_DETECCION_S = 15.0  # una alarma cuenta como verdadera hasta este tiempo tras la apnea
TOLERANCIA_INICIO_S = 2.0  # ...y solo si la caída que la dispara empezó dentro de la apnea
                           # (con esta tolerancia); si empezó antes, es una falsa alarma

# Referencia mínima para armarse, por medida. Elegidas con los objetos quietos (pared y cama
# vacía) contra las personas: RMS 0.001-0.003 contra >= 0.035; actividad 0.004-0.017 contra
# >= 0.054 en tramos de 15 s. Se toma un valor intermedio.
ARMADO_MIN = {"rms_fase_resp": 0.01, "actividad_fase_total": 0.03}
MEDIDAS = tuple(ARMADO_MIN)

REJILLA_UMBRAL = (0.3, 0.4, 0.5, 0.6, 0.7)
REJILLA_DURACION_S = (5.0, 10.0, 15.0, 20.0)

CARPETA_CRUDOS = Path(__file__).resolve().parent.parent / "pruebas" / "crudos"
CARPETA_SALIDA = er.CARPETA_RESULTADOS / "deteccion_apnea"

# Capturas con apnea: (nombre corto, archivo, intervalo de apnea en s)
APNEAS = {
    "acostado1": ("2026-10-09_001540_acostado_apnea1_50cm", (45.0, 75.0)),
    "acostado2": ("2026-10-09_001918_acostado_apnea2_50cm", (45.0, 75.0)),
}
# Apnea sentado a ~84 cm: se reporta, pero no entra en la validación cruzada.
APNEA_SENTADO = ("2026-10-07_224314_apnea_84cm", (45.0, 65.0))

# Capturas sin apnea, para contar falsas alarmas.
SIN_APNEA = [
    ("acostado", "2026-10-08_235538_acostado_8rpm_50cm"),
    ("acostado", "2026-10-08_235002_acostado_15rpm_50cm"),
    ("acostado", "2026-10-09_001141_acostado_30rpm_50cm"),
    ("acostado", "2026-10-09_000001_acostado_variable_rapido_a_lento_50cm"),
    ("sentado", "2026-10-06_172200_persona_sentada_84cm"),
    ("sentado", "2026-10-07_222846_normal_84cm"),
    ("sentado", "2026-10-07_223440_lenta_84cm"),
    ("sentado", "2026-10-07_223850_rapida_84cm"),
    ("sentado", "2026-10-06_161758_persona_sentada_80cm"),
    ("sentado", "2026-10-06_162052_persona_frente_computadora_50cm"),
    ("sin persona", "2026-10-09_002233_cama_vacia_50cm"),
    ("sin persona", "2026-10-06_163155_sin_nadie_apuntando_pared"),
]


# --- Medidas por muestra -------------------------------------------------------------------

def entrada_medida(datos, medida):
    """Devuelve (t, valor) de las muestras que alimentan la medida, en orden de llegada."""
    if medida == "rms_fase_resp":
        t_uniforme, x_uniforme = er.remuestrear(datos["t"], datos["fase_resp"])
        filtrada = signal.sosfilt(er.disenar_filtro(), x_uniforme - x_uniforme[0])
        return t_uniforme, filtrada
    if medida == "actividad_fase_total":
        validas = datos["fase_total"] != 0.0
        return datos["t"][validas], np.abs(datos["fase_total"][validas])
    raise ValueError(f"medida desconocida: {medida}")


def calcular_medida(valores, medida):
    if medida == "rms_fase_resp":
        return float(np.sqrt(np.mean(np.square(valores))))
    return float(np.median(valores))


class DetectorApnea:
    """Recibe muestras (t, valor) en orden y evalúa el estado una vez por PASO_S.

    En el firmware: un buffer circular con las muestras de los últimos VENTANA_CORTA_S y otro con
    REFERENCIA_S valores de la medida (uno por segundo).
    """

    def __init__(self, medida, umbral, duracion_min_s):
        self.medida = medida
        self.umbral = umbral
        self.duracion_min_s = duracion_min_s
        self.ventana = deque()      # (t, valor) de los últimos VENTANA_CORTA_S
        self.historia = deque()     # medida de los segundos sin sospecha
        self.proximo_paso = None
        self.bajo_desde = None
        self.en_alarma = False

    def agregar(self, t, valor):
        self.ventana.append((t, valor))
        while self.ventana and self.ventana[0][0] <= t - VENTANA_CORTA_S:
            self.ventana.popleft()

    def evaluar(self, t, presencia):
        """Estado en el instante t (llamar una vez por PASO_S)."""
        if not self.ventana:
            return None
        actual = calcular_medida([v for _, v in self.ventana], self.medida)
        armado = len(self.historia) * PASO_S >= REFERENCIA_MIN_S
        referencia = float(np.median(self.historia)) if armado else float("nan")
        armado = armado and referencia >= ARMADO_MIN[self.medida] and presencia
        bajo = armado and actual < self.umbral * referencia

        alarma = None
        if bajo:
            if self.bajo_desde is None:
                self.bajo_desde = t
            if not self.en_alarma and t - self.bajo_desde >= self.duracion_min_s:
                self.en_alarma = True
                alarma = (t, self.bajo_desde)
        else:
            self.bajo_desde = None
            self.en_alarma = False
            self.historia.append(actual)
            if len(self.historia) * PASO_S > REFERENCIA_S:
                self.historia.popleft()
        return {"t": t, "medida": actual, "referencia": referencia, "alarma": alarma,
                "en_alarma": self.en_alarma}


def correr_detector(datos, medida, umbral, duracion_min_s):
    """Devuelve (estados por segundo, alarmas). Cada alarma es (instante, inicio de la caída)."""
    t_muestras, valores = entrada_medida(datos, medida)
    detector = DetectorApnea(medida, umbral, duracion_min_s)
    estados = []
    proximo = t_muestras[0] + PASO_S
    for t, valor in zip(t_muestras, valores):
        while t >= proximo:
            presencia = np.interp(proximo, datos["t"], datos["presencia"]) >= 0.5
            estado = detector.evaluar(proximo, presencia)
            if estado is not None:
                estados.append(estado)
            proximo += PASO_S
        detector.agregar(t, valor)
    return estados, [e["alarma"] for e in estados if e["alarma"]]


def es_verdadera(alarma, intervalo):
    instante, desde = alarma
    inicio, fin = intervalo
    return desde >= inicio - TOLERANCIA_INICIO_S and instante <= fin + MARGEN_DETECCION_S


def clasificar(alarmas, intervalo):
    """(instante de la primera alarma verdadera o None, instantes de las falsas)."""
    verdaderas = [a[0] for a in alarmas if es_verdadera(a, intervalo)]
    falsas = [a[0] for a in alarmas if not es_verdadera(a, intervalo)]
    return (verdaderas[0] if verdaderas else None), falsas


# --- Validación cruzada --------------------------------------------------------------------

def entrenar(datos, intervalo, medida):
    """Elige (umbral, duración) con una captura de apnea.

    Criterio: detectar la apnea sin falsas alarmas en esa captura, con el menor retraso; en
    empate, el umbral más bajo y la duración más larga (lo más conservador). None si ninguna
    combinación cumple.
    """
    candidatos = []
    for umbral in REJILLA_UMBRAL:
        for duracion in REJILLA_DURACION_S:
            _, alarmas = correr_detector(datos, medida, umbral, duracion)
            deteccion, falsas = clasificar(alarmas, intervalo)
            if deteccion is not None and not falsas:
                candidatos.append((deteccion, umbral, -duracion))
    if not candidatos:
        return None
    _, umbral, menos_duracion = min(candidatos)
    return umbral, -menos_duracion


def evaluar(capturas, medida, umbral, duracion):
    """Resultados por captura: (detección o None, falsas, segundos en alarma).

    Los segundos en alarma importan porque la referencia se congela mientras la medida está
    baja: una falsa alarma puede quedar activa mucho tiempo aunque cuente como una sola.
    """
    resultados = {}
    for nombre, (datos, intervalo) in capturas.items():
        estados, alarmas = correr_detector(datos, medida, umbral, duracion)
        segundos = sum(PASO_S for e in estados if e["en_alarma"])
        if intervalo:
            deteccion, falsas = clasificar(alarmas, intervalo)
        else:
            deteccion, falsas = None, [a[0] for a in alarmas]
        resultados[nombre] = (deteccion, falsas, segundos)
    return resultados


# --- Escenas -------------------------------------------------------------------------------

def resumen_escena(datos, desde_s=30.0):
    """Lo que reporta el radar después del acomodo: presencia, distancia, tasa, medidas."""
    m = datos["t"] >= desde_s
    t = datos["t"][m]
    t_res, x_res = entrada_medida(datos, "rms_fase_resp")
    t_act, x_act = entrada_medida(datos, "actividad_fase_total")
    return {
        "presencia": 100.0 * float(np.mean(datos["presencia"][m])),
        "distancia": float(np.nanmedian(datos["distancia_cm"][m])),
        "tasa": (len(t) - 1) / (t[-1] - t[0]),
        "ceros": 100.0 * float(np.mean(datos["fase_total"][m] == 0.0)),
        "rms": calcular_medida(x_res[t_res >= desde_s], "rms_fase_resp"),
        "actividad": calcular_medida(x_act[t_act >= desde_s], "actividad_fase_total"),
        "resp_rpm": float(np.median(datos["resp_rpm"][m])),
        "latido_bpm": float(np.median(datos["latido_bpm"][m])),
    }


def tasa_tramas(t, ventana_s=2.0):
    """Tramas por segundo en una ventana deslizante, evaluada una vez por segundo."""
    instantes = np.arange(ventana_s, t[-1], 1.0)
    return instantes, np.array([np.sum((t > s - ventana_s) & (t <= s)) / ventana_s for s in instantes])


# --- Figuras -------------------------------------------------------------------------------

def graficar_apnea(ruta_png, titulo, datos, intervalo, detecciones):
    """Señales de una captura de apnea. detecciones: {medida: (estados, alarmas, umbral)}."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    t = datos["t"]
    t_resp, resp_filtrada = entrada_medida(datos, "rms_fase_resp")
    t_int, x_int = er.senal_respiracion(datos, "fase_total_integrada")
    t_int_u, x_int_u = er.remuestrear(t_int, x_int)
    integrada = signal.sosfilt(er.disenar_filtro(), x_int_u - x_int_u[0])
    t_tasa, tasa = tasa_tramas(t)

    paneles = [
        ("fase_resp\n(cruda y filtrada)", lambda e: (e.plot(t, datos["fase_resp"], color="0.6", lw=0.6),
                                                     e.plot(t_resp, resp_filtrada, color="C0", lw=0.9))),
        ("fase_total integrada\nfiltrada", lambda e: e.plot(t_int_u, integrada, color="C1", lw=0.9)),
    ]
    for medida, color in zip(MEDIDAS, ("C4", "C2")):
        estados, alarmas, umbral = detecciones[medida]
        te = [s["t"] for s in estados]
        ref = np.array([s["referencia"] for s in estados])

        def dibujar(e, te=te, estados=estados, ref=ref, umbral=umbral, color=color, alarmas=alarmas):
            e.plot(te, [s["medida"] for s in estados], "o-", color=color, ms=2.5, lw=0.9)
            e.plot(te, ref, color="0.3", lw=0.9, label="referencia")
            e.plot(te, umbral * ref, color="C3", ls="--", lw=0.9, label=f"umbral ({umbral:g} × ref.)")
            for instante, _ in alarmas:
                e.axvline(instante, color="C3", lw=2.0)
            e.legend(loc="upper right", fontsize=7)
        etiqueta = "RMS 5 s fase_resp" if medida == "rms_fase_resp" else "actividad 5 s\nmediana |fase_total|"
        paneles.append((etiqueta, dibujar))
    paneles += [
        ("fase_latido", lambda e: e.plot(t, datos["fase_latido"], color="C5", lw=0.6)),
        ("resp_rpm módulo", lambda e: e.plot(t, datos["resp_rpm"], color="0.3", lw=0.9)),
        ("latido_bpm módulo", lambda e: e.plot(t, datos["latido_bpm"], color="0.3", lw=0.9)),
        ("presencia", lambda e: e.plot(t, datos["presencia"], color="0.3", lw=0.9)),
        ("distancia (cm)", lambda e: e.plot(t, datos["distancia_cm"], color="0.3", lw=0.9)),
        ("tramas/s\n(ventana 2 s)", lambda e: e.plot(t_tasa, tasa, color="0.3", lw=0.9)),
    ]

    figura, ejes = plt.subplots(len(paneles), 1, figsize=(12, 2.0 * len(paneles)), sharex=True)
    for eje, (etiqueta, dibujar) in zip(ejes, paneles):
        dibujar(eje)
        eje.axvspan(*intervalo, color="C3", alpha=0.12)
        eje.set_ylabel(etiqueta, fontsize=8)
        eje.grid(True, alpha=0.3)
    ejes[-1].set_xlabel("tiempo desde el inicio de la captura (s)")
    figura.suptitle(titulo)
    figura.tight_layout(rect=(0, 0, 1, 0.98))
    figura.savefig(ruta_png, dpi=90)
    plt.close(figura)


def graficar_relacion(ruta_png, filas, medida, umbral):
    """Medida corta / referencia en el tiempo para varias capturas. filas: (etiqueta, estados,
    alarmas, intervalo o None)."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figura, ejes = plt.subplots(len(filas), 1, figsize=(12, 1.6 * len(filas)), sharex=True)
    for eje, (etiqueta, estados, alarmas, intervalo) in zip(ejes, filas):
        te = [s["t"] for s in estados]
        relacion = [s["medida"] / s["referencia"] if s["referencia"] > 0 else np.nan for s in estados]
        eje.plot(te, relacion, color="C0", lw=0.9)
        eje.axhline(umbral, color="C3", ls="--", lw=0.9)
        if intervalo:
            eje.axvspan(*intervalo, color="C3", alpha=0.12)
        for instante, _ in alarmas:
            eje.axvline(instante, color="C3", lw=1.8)
        eje.set_ylim(0, 2.5)
        eje.set_ylabel(etiqueta, fontsize=7)
        eje.grid(True, alpha=0.3)
    ejes[-1].set_xlabel("tiempo (s)")
    figura.suptitle(f"{medida}: medida corta / referencia; línea discontinua: umbral {umbral:g}; "
                    "líneas rojas: alarmas; franja: apnea")
    figura.tight_layout(rect=(0, 0, 1, 0.98))
    figura.savefig(ruta_png, dpi=90)
    plt.close(figura)


# --- Programa ------------------------------------------------------------------------------

def corto(archivo):
    return archivo.split("_", 2)[2]


def texto_resultado(resultado, intervalo):
    deteccion, falsas, segundos = resultado
    if intervalo is None:
        if not falsas:
            return "sin falsas alarmas"
        return (f"{len(falsas)} falsa(s) (inicio a los {', '.join(f'{a:.0f}' for a in falsas)} s; "
                f"{segundos:.0f} s en alarma)")
    if deteccion is None:
        texto = "**no detecta**"
    else:
        texto = f"detecta a los {deteccion:.0f} s (retraso {deteccion - intervalo[0]:.0f} s)"
    if falsas:
        texto += f"; {len(falsas)} falsa(s) fuera de la apnea"
    return texto


def main():
    CARPETA_SALIDA.mkdir(parents=True, exist_ok=True)
    cargar = lambda archivo: er.cargar_csv(CARPETA_CRUDOS / f"{archivo}.csv")
    apneas = {clave: (cargar(archivo), intervalo) for clave, (archivo, intervalo) in APNEAS.items()}
    sin_apnea = {archivo: (cargar(archivo), None) for _, archivo in SIN_APNEA}
    apnea_sentado = {APNEA_SENTADO[0]: (cargar(APNEA_SENTADO[0]), APNEA_SENTADO[1])}
    grupo = {archivo: g for g, archivo in SIN_APNEA}

    lineas = ["# Detección de apnea", ""]

    # Escenas
    lineas += [
        "## Qué reporta el radar en cada escena (después de los primeros 30 s)",
        "",
        "| Captura | Grupo | Presencia | Distancia (cm) | Tramas/s | fase_total en 0 | RMS fase_resp | Actividad | resp_rpm | latido_bpm |",
        "|---|---|---|---|---|---|---|---|---|---|",
    ]
    escenas = [(APNEAS[c][0], "acostado, apnea", apneas[c][0]) for c in APNEAS]
    escenas += [(archivo, grupo[archivo], datos) for archivo, (datos, _) in sin_apnea.items()]
    escenas += [(APNEA_SENTADO[0], "sentado, apnea", apnea_sentado[APNEA_SENTADO[0]][0])]
    for archivo, etiqueta, datos in escenas:
        e = resumen_escena(datos)
        lineas.append(f"| {corto(archivo)} | {etiqueta} | {e['presencia']:.0f} % | {e['distancia']:.0f} | "
                      f"{e['tasa']:.2f} | {e['ceros']:.1f} % | {e['rms']:.3f} | {e['actividad']:.3f} | "
                      f"{e['resp_rpm']:.0f} | {e['latido_bpm']:.0f} |")

    # Validación cruzada
    detecciones_figura = {clave: {} for clave in APNEAS}
    relacion_figura = {}
    for medida in MEDIDAS:
        lineas += ["", f"## Medida `{medida}`", ""]
        for entrenamiento, prueba in (("acostado1", "acostado2"), ("acostado2", "acostado1")):
            datos_ent, intervalo_ent = apneas[entrenamiento]
            elegido = entrenar(datos_ent, intervalo_ent, medida)
            lineas.append(f"### Ajuste con {entrenamiento}, prueba con {prueba}")
            lineas.append("")
            if elegido is None:
                lineas += ["Ninguna combinación de la rejilla detecta la apnea de entrenamiento sin "
                           "falsas alarmas en esa misma captura.", ""]
                continue
            umbral, duracion = elegido
            lineas += [f"Parámetros elegidos: umbral {umbral:g} × referencia, duración {duracion:g} s.", "",
                       "| Captura | Grupo | Resultado |", "|---|---|---|"]
            for clave, resultado in evaluar(apneas, medida, umbral, duracion).items():
                papel = "entrenamiento" if clave == entrenamiento else "**prueba**"
                lineas.append(f"| {corto(APNEAS[clave][0])} | {papel} | {texto_resultado(resultado, apneas[clave][1])} |")
            falsas_total, segundos_alarma = 0, 0.0
            for archivo, resultado in evaluar(sin_apnea, medida, umbral, duracion).items():
                falsas_total += len(resultado[1])
                segundos_alarma += resultado[2]
                lineas.append(f"| {corto(archivo)} | {grupo[archivo]} | {texto_resultado(resultado, None)} |")
            for archivo, resultado in evaluar(apnea_sentado, medida, umbral, duracion).items():
                lineas.append(f"| {corto(archivo)} | sentado, apnea (informativa) | "
                              f"{texto_resultado(resultado, APNEA_SENTADO[1])} |")
            minutos_persona = sum(datos["t"][-1] - 30.0 for archivo, (datos, _) in sin_apnea.items()
                                  if grupo[archivo] != "sin persona") / 60.0
            lineas += ["", f"Capturas sin apnea: **{falsas_total}** falsas alarmas y **{segundos_alarma:.0f} s** en "
                           f"alarma, en {minutos_persona:.1f} min de captura con persona (después de los 30 s).", ""]

            # Para las figuras: la detección en cada apnea con los parámetros ajustados con la otra.
            estados, alarmas = correr_detector(apneas[prueba][0], medida, umbral, duracion)
            detecciones_figura[prueba][medida] = (estados, alarmas, umbral)
            if medida == "actividad_fase_total" and entrenamiento == "acostado1":
                relacion_figura = {"umbral": umbral, "duracion": duracion}

    # Figuras
    for clave, (datos, intervalo) in apneas.items():
        for medida in MEDIDAS:
            if medida not in detecciones_figura[clave]:
                estados, alarmas = correr_detector(datos, medida, 0.5, 10.0)
                detecciones_figura[clave][medida] = (estados, alarmas, 0.5)
        graficar_apnea(CARPETA_SALIDA / f"apnea_{clave}.png",
                       f"{corto(APNEAS[clave][0])}: apnea {intervalo[0]:g}-{intervalo[1]:g} s (franja); "
                       "alarmas con parámetros ajustados en la otra captura (líneas rojas)",
                       datos, intervalo, detecciones_figura[clave])
    if relacion_figura:
        filas = []
        todas = list(apneas.items()) + list(sin_apnea.items()) + list(apnea_sentado.items())
        for nombre, (datos, intervalo) in todas:
            estados, alarmas = correr_detector(datos, "actividad_fase_total",
                                               relacion_figura["umbral"], relacion_figura["duracion"])
            etiqueta = nombre if nombre in APNEAS else corto(nombre)
            filas.append((etiqueta, estados, alarmas, intervalo))
        graficar_relacion(CARPETA_SALIDA / "actividad.png", filas, "actividad_fase_total", relacion_figura["umbral"])

    for viejo in ("apnea.png", "relacion.png"):
        (CARPETA_SALIDA / viejo).unlink(missing_ok=True)

    contenido = "\n".join(lineas) + "\n"
    (CARPETA_SALIDA / "deteccion.md").write_text(contenido, encoding="utf-8", newline="\n")
    print(contenido)
    print(f"Resultados en {CARPETA_SALIDA}")


if __name__ == "__main__":
    main()
