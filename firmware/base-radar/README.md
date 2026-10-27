# firmware/base-radar

Firmware del XIAO ESP32-C6 que viene en el kit Seeed MR60BHA2.

## Módulos (`src/`)

| Módulo | Responsabilidad |
|---|---|
| `radar` | Leer respiración, latido, presencia y distancia del radar por UART; ventana de actividad de 30 s |
| `ventana_actividad` | Criterio "persona detectada": mediana de \|`fase_total`\| en 30 s, umbral 0.03 (lógica pura) |
| `espnow_rx` | Recibir mensajes del colgante, filtrar duplicados, responder ACK |
| `red_wifi` | Conexión WiFi STA y reconexión |
| `firebase_cliente` | Tarea de red: sesión de Firebase Auth, escritura en Realtime Database por REST (HTTPS), eventos en NVS, `BASE_DESCONECTADA`; atiende también Telegram |
| `peticion_https` | Lo común de cada petición HTTPS (Firebase y Telegram): certificado, límites de tiempo, cuerpo, vigilante y traza |
| `vigilante_red` | Cuándo cortar una petición colgada y cuándo reiniciar (lógica pura) |
| `cuerpo_http` | Cuerpo de las respuestas HTTP con memoria fija y sin esperas sin límite (lógica pura) |
| `formato_nube` | JSON exacto que aceptan las reglas y lectura de las respuestas (lógica pura) |
| `cola_peticiones` | Cola acotada de loop() a la tarea de red (lógica pura) |
| `cola_offline` | Lecturas sin red (1 h en RAM) y eventos pendientes para Firebase y Telegram, guardables en NVS (lógica pura) |
| `detectores` | Cuándo crear `BATERIA_BAJA`, `COLGANTE_AUSENTE` y `RADAR_SIN_DATOS` (una vez por episodio) y la duración de `BASE_DESCONECTADA` (lógica pura) |
| `telegram` | `sendMessage` al cuidador por la API de bots (HTTPS) |
| `mensajes_telegram` | Texto de cada aviso y límite de frecuencia (lógica pura) |
| `almacen` | NVS: eventos pendientes y estado de los detectores, que sobreviven a un reinicio |
| `watchdog_tareas` | Watchdog de tareas de ESP-IDF sobre loop() y la tarea de red |
| `motivo_reinicio` | Motivo del arranque (`esp_reset_reason()`) y si fue inesperado |
| `registro` | Líneas de registro por serial con hora y módulo |
| `alerta_local` | Buzzer y LED; funciona sin red |
| `boton_silencio` | Botón BOOT de la placa: silencia la alerta local |

Estado (v0.6.0, nube parte 2):

- **Verificado en hardware** (hasta v0.5.1, 2026-10-15): WiFi, ESP-NOW con el colgante, ACK con
  canal, LED, buzzer, botón BOOT, radar (~16 tramas de fase por segundo con persona) y Firebase:
  estado, lecturas cada 5 s, colgante y evento `BOTON` (del botón al evento en Firebase en
  ~270 ms), 0 errores. Sin internet el error sale en 5 s y la base reintenta sin colgarse.
- **Nuevo en v0.6.0, compilado y probado en PC (`pruebas_host/`), por confirmar con el módulo**:
  cola offline de 1 h con eventos en NVS, eventos `BATERIA_BAJA`, `COLGANTE_AUSENTE`,
  `RADAR_SIN_DATOS` y `BASE_DESCONECTADA`, Telegram, watchdog de tareas y aviso de reinicio
  inesperado. Guía de prueba de cada uno abajo.

## Qué hace hoy

- **WiFi** (`BASE_WIFI_HABILITADO 1`, por defecto): modo STA con `WIFI_SSID` y `WIFI_CLAVE` de
  `secrets.h`, sin ahorro de energía. La base no fija canal: queda en el del router y el colgante
  la encuentra con su barrido (`docs/protocolo.md`, sección 6). La reconexión automática del
  framework está desactivada; `red_wifi` reintenta con espera creciente: 5 s tras perder la
  conexión y el doble en cada fallo, hasta 120 s (`WIFI_ESPERA_*` en `config.h`). Así la radio no
  salta de canal sin parar y entre intentos el colgante la puede encontrar (`docs/fallos.md`,
  "Reconexión WiFi y ESP-NOW"). Nada de esto bloquea `loop()`.

  Orden de arranque, verificado en arduino-esp32 3.3.7: `WiFi.mode(WIFI_STA)` arranca la radio,
  después `esp_now_init()` (necesita la radio arrancada) y el `WiFi.begin()` sale en la primera
  vuelta de `loop()`. Los peers de ESP-NOW usan `channel = 0` ("canal actual", `esp_now.h`), así
  que el ACK sigue saliendo bien cuando el canal cambia.

  Con `BASE_WIFI_HABILITADO 0` es la fase local: no se conecta, canal fijo `BASE_CANAL_SIN_WIFI`
  (1) y no hace falta `secrets.h`.
- **Colgante**: el callback de recepción valida el paquete, aplica la regla de `duplicados.h`
  con el estado de cada id de colgante, responde el ACK en el mismo callback y copia el mensaje a
  una cola FreeRTOS. Es la
  misma lógica del receptor de prueba (`firmware/pruebas/receptor_espnow`), ya validada con el
  colgante.
- **Radar: persona y objetivo** (`docs/modelo-datos.md`, 1.1; `docs/radar.md`, 13.6). Con cada
  trama de fase, `radar` guarda |`fase_total`| (sin las tramas en 0) en una ventana deslizante de
  30 s (`RADAR_VENTANA_ACTIVIDAD_MS`). `radar::estadoPersona()` devuelve:
  - `actividad`: mediana de esa ventana (como `numpy.median`: con cantidad par, el promedio de
    los dos del medio);
  - `objetivo`: hubo al menos una trama de fase con dato en los últimos 30 s;
  - `persona`: `actividad >= 0.03` (`RADAR_UMBRAL_PERSONA`).

  Memoria fija: 640 muestras (`RADAR_CAPACIDAD_VENTANA`, ~490 a 16.2 tramas/s más ~30 % de
  margen), 7.5 kB entre muestras y la copia para la mediana. La bandera `presencia` del módulo ya
  no se usa para decir "persona": sigue en 1 con la cama vacía. Por serial sale cada cambio:
  `radar: persona detectada (actividad 0.048)`, `sin persona detectada (actividad 0.012 < 0.03)` o
  `sin persona detectada (radar sin objetivo)`. "Sin persona detectada" **no** es "cama vacía".
- **Alerta local**: LED WS2812 de la placa y buzzer, sin `delay()`.

  | Situación | LED | Buzzer |
  |---|---|---|
  | En funcionamiento | verde tenue fijo | no |
  | ALERTA nueva del colgante | rojo intermitente, 60 s (`ALERTA_LOCAL_DURACION_MS`) | intermitente, 60 s |
  | Aviso: batería baja del colgante, radar sin datos, ESP-NOW no inició | amarillo intermitente, 10 s | no |

  Un duplicado no vuelve a disparar la alerta. Una ALERTA nueva durante otra reinicia los 60 s.
  El botón BOOT de la placa (o el comando `s`) silencia la alerta.
  `PATRON_APNEA` está definido pero el radar no lo dispara: la apnea por radar no se usa como
  alarma (`docs/radar.md`, sección 13.6). Brillos en `LED_BRILLO_ALERTA` y `LED_BRILLO_REPOSO`.
- **Serial** (115200): por defecto, registros legibles. Todos empiezan con `#`:

  ```
  # [    52310 ms] AA:BB:CC:DD:EE:FF ALERTA    id=1 seq=301 flags=- bat=3900 mV rssi=-48 dBm canal=1 NUEVO ack=OK
  # [    52420 ms] AA:BB:CC:DD:EE:FF ALERTA    id=1 seq=301 flags=REINTENTO bat=3900 mV rssi=-49 dBm canal=1 DUPLICADO ack=OK
  # [    60001 ms] radar: persona detectada
  # [       12 ms] wifi: intento 1, buscando "MiRed" (radio en canal 1)
  # [       95 ms] radio: canal 1 -> 6 (wifi intentando)
  # [     2310 ms] wifi: conectado a "MiRed", canal 6, RSSI -52 dBm, IP 192.168.1.40 (intento 1)
  # [   900120 ms] wifi: desconectado de "MiRed", canal 6, ultimo RSSI -53 dBm, motivo 200 (BEACON_TIMEOUT); reintento en 5 s
  # [   905120 ms] wifi: intento 2, buscando "MiRed" (radio en canal 6)
  # [   907480 ms] wifi: fallo el intento 2 (motivo 201 NO_AP_FOUND), 1 seguidos; siguiente en 10 s
  ```

  Las líneas de WiFi son un ejemplo del formato: los tiempos, canales y motivos reales salen de
  la prueba de abajo.

  `ack=OK` significa que `esp_now_send()` aceptó el ACK, no que llegó al colgante. Con
  `BASE_SALIDA_CSV 1` en `config.h` se imprime además una línea CSV por lectura del radar, para
  `analisis/capturar_serial.py`, que ignora las líneas con `#`.

### Firebase (`firebase_cliente`)

Escribe como `web/simulador/simulador.py` (mismas rutas, campos y formatos, que pasaron la
verificación contra las reglas publicadas):

| Qué | Cuándo | Petición |
|---|---|---|
| Estado: `online`, `ultimo_contacto` (`{".sv":"timestamp"}`), `rssi_wifi`, `version_firmware` | al tener sesión y después cada `ESTADO_INTERVALO_MS` (60 s) | `PATCH /dispositivos/{BASE_ID}/estado` y `GET .../ultimo_contacto` |
| Lectura: `persona`, `objetivo`, `actividad`, `distancia_cm`, `resp_rpm`, `latido_bpm` (los opcionales solo cuando corresponden, `docs/modelo-datos.md` 1.1) | cada `LECTURA_INTERVALO_MS` (5 s), si el radar tiene datos | `PUT /lecturas/{BASE_ID}/{ts}` |
| Colgante: `bateria_mv`, `ultimo_heartbeat`, `rssi`, `base` | cada mensaje NUEVO del colgante (los duplicados no) | `PATCH /colgantes/{id}` |
| Evento `BOTON` (`origen` = `colganteN`, `detalle` "boton del colgante") | cada ALERTA nueva | `POST /eventos` |
| Eventos `BATERIA_BAJA`, `COLGANTE_AUSENTE`, `RADAR_SIN_DATOS`, `BASE_DESCONECTADA` | una vez por episodio (abajo, "Eventos") | `POST /eventos` |

Todas las escrituras van con `?print=silent` (respuesta vacía, no gasta descarga). Solo la
lectura de `ultimo_contacto` trae datos (~13 bytes por minuto).

- **Hora del servidor** (`docs/modelo-datos.md`, sección 2): tras cada estado se lee
  `ultimo_contacto` y se guarda con el `millis()` de ese momento. La clave de una lectura es
  `ts = hora_ref + (millis_lectura - millis_ref)`, con 13 dígitos. Hasta tener la primera
  referencia, las lecturas esperan en la cola. Una lectura que sale más de 10 s
  (`LECTURA_DIFERIDA_MS`) después de medirse lleva `diferida: true`.
- **Sesión**: `signInWithPassword` con `DISPOSITIVO_EMAIL` / `DISPOSITIVO_CLAVE`. El `idToken`
  (1 h) se renueva con el refresh token (`securetoken.googleapis.com`) 5 min antes de vencer
  (`FIREBASE_MARGEN_RENOVACION_MS`); si la renovación falla, inicia sesión otra vez. Ante un 401
  inicia sesión de nuevo y reintenta una vez.
- **Errores**: sin red o con error del servidor (5xx, tiempo agotado) la petición queda en la
  cola y se reintenta tras 5 s, 10 s, 20 s... hasta 60 s. Si después de iniciar sesión otra vez
  sigue en 400/401/403 (las reglas la rechazan), reintentar no sirve: se descarta y se registra.
  El estado nunca se descarta: rechazado, sigue reintentando con espera.
- **HTTPS**: el certificado del servidor se verifica con el bundle de CA de ESP-IDF que trae
  arduino-esp32 3.3.7 dentro de `libmbedtls.a` (`_binary_x509_crt_bundle_start`, lista completa
  de Mozilla) mediante `NetworkClientSecure::setCACertBundle()`. Nunca `setInsecure()`. Se
  verifican la cadena y el nombre del servidor; las fechas no, porque el framework se compila sin
  `MBEDTLS_HAVE_TIME_DATE` (la base no tiene reloj al arrancar). La conexión con la base de datos
  queda abierta (keep-alive): un handshake por conexión, no por petición.

**Sin bloqueos.** Toda la red corre en la tarea FreeRTOS `red` (`TAREA_RED_*` en `config.h`).
`loop()` solo llama a `firebase_cliente::encolar*()`, que copian la petición con un mutex que
nunca se tiene tomado durante una petición. La alerta local y el ACK al colgante (que sale en el
callback de ESP-NOW, en la tarea de WiFi) no pasan por la red.

- La tarea tiene la misma prioridad que `loop()` (1). El ESP32-C6 tiene un solo núcleo; con la
  misma prioridad FreeRTOS reparte la CPU por turnos de 1 ms, así que un handshake TLS (que hace
  cálculos de ~1 s) no deja a `loop()` sin correr.
- **Buffer de la UART del radar**: no hace falta agrandarlo. La librería Seeed lo fija en 32 kB
  en `begin()`; con objetivo llegan ~74 tramas/s (~1.7 kB/s), o sea ~19 s de margen. El límite
  real es la cola de tramas de la librería: guarda 120 tramas (~1.6 s, ~2.8 kB) y descarta las
  más viejas si `loop()` tarda más que eso. El comando `e` muestra el pico de bytes en la UART:
  si queda por debajo de 2800 B, no se perdió ninguna trama. Por confirmar con el módulo
  durante un handshake TLS.

**Cola acotada** (`cola_peticiones` y `cola_offline`, capacidades en `config.h`): eventos aparte
(32, guardados en NVS), una entrada por colgante (8) y lecturas (720, 1 h). Llena, se descarta la
lectura más antigua; una lectura nunca desplaza un evento. Sale primero lo más importante:
eventos, colgantes, lecturas. Detalle abajo, "Sin red".

Registros:

```
# [     2510 ms] firebase: sesion iniciada: base01@... (uid AbC123...), token por 3600 s
# [  3302511 ms] firebase: token renovado, vence en 3600 s
# [    52400 ms] firebase: evento BOTON de colgante1 enviado
# [   120330 ms] firebase: ERROR PUT lecturas/base01/1791234567890: HTTP -11 read Timeout (se reintenta)
# [   130020 ms] firebase: 401 en dispositivos/base01/estado (Permission denied); se inicia sesion de nuevo
```

### Eventos

Cada evento va a `/eventos` y a Telegram, y sale por serial. Ninguno se repite mientras dure la
condición (`detectores`); el estado de cada episodio se guarda en NVS, así un reinicio no repite el
aviso. Tiempos en `config.h` y su justificación en `docs/fallos.md`, "Tipos de evento".

| Evento | Cuándo | Termina el episodio | Serial |
|---|---|---|---|
| `BOTON` | ALERTA nueva del colgante | (cada pulsación es uno) | `firebase: evento BOTON de colgante1 enviado` |
| `BATERIA_BAJA` | primer mensaje con `FLAG_BATERIA_BAJA` (< 3500 mV en el colgante) | un mensaje sin la bandera y con ≥ 3700 mV | `colgante 1: bateria baja (3420 mV), evento BATERIA_BAJA` |
| `COLGANTE_AUSENTE` | 45 min sin ningún mensaje del colgante (3 heartbeats) | el siguiente mensaje | `colgante 1: sin mensajes por 45 min, evento COLGANTE_AUSENTE` |
| `RADAR_SIN_DATOS` | 30 s sin tramas del radar | 60 s seguidos con datos | `radar sin datos por 30 s, evento RADAR_SIN_DATOS` |
| `BASE_DESCONECTADA` | al arrancar o reconectar, si pasaron ≥ 3 min desde el último `ultimo_contacto` | (uno por corte) | `firebase: la base estuvo 12 min sin actualizar su estado: evento BASE_DESCONECTADA` |

Un colgante es "conocido" desde su primer mensaje; tras un reinicio de la base, los conocidos
(de NVS) cuentan su ausencia desde el arranque. Hasta 8 colgantes.

### Telegram (`telegram`)

`POST https://api.telegram.org/bot<TOKEN>/sendMessage` con `{"chat_id":..., "text":...}` desde la
tarea de red, con el certificado verificado con el bundle de CA (como Firebase). Mensajes:

| Aviso | Texto |
|---|---|
| `BOTON` | ALERTA: botón de auxilio del colgante 1. Atender de inmediato. |
| `BATERIA_BAJA` | Batería baja en el colgante 1 (3420 mV). Hay que cargarlo. |
| `COLGANTE_AUSENTE` | El colgante 1 no se comunica (sin mensajes por 45 min). Revisar la batería y el alcance. |
| `RADAR_SIN_DATOS` | El radar de la base base01 no envía datos (sin datos por 30 s). Revisar el cable y la alimentación. |
| `BASE_DESCONECTADA` | La base base01 volvió a estar en línea (sin conexión por 12 min). |
| Reinicio inesperado | La base base01 se reinició de forma inesperada: watchdog de tareas. |

Un aviso que salió con más de 1 min de atraso agrega "(ocurrió hace 5 min)", y uno de antes de un
reinicio, "(aviso de antes de un reinicio de la base)".

- **No depende de Firebase**: conexión, espera tras error y límite propios; en cada vuelta la
  tarea de red atiende Telegram antes que Firebase. Funciona también con Firebase desactivado.
- **El botón primero**: el `BOTON` sale antes que cualquier otro aviso, espera solo 1 s desde el
  mensaje anterior y no cuenta para el tope. Los demás: uno cada 3 s y 12 por hora como mucho; lo
  que pasa del tope espera en la cola.
- **Reintentos sin bloquear**: con error de red o 5xx, 2 s y el doble hasta 30 s; con 429 lo que
  pide Telegram (`retry_after`). Con 400/401/403/404 (token o chat mal, bot fuera del grupo) el
  aviso se descarta con `telegram: ERROR: HTTP 401 Unauthorized; el aviso se descarta ...`.
- Con `TELEGRAM_TOKEN` o `TELEGRAM_CHAT_ID` de ejemplo: `telegram: deshabilitado: secrets.h tiene
  los valores de ejemplo ...` y la base sigue sin Telegram.

### Sin red (`cola_offline`, `almacen`)

- **Lecturas**: hasta 720 en RAM (1 h). Llena, se descartan las más viejas. Al volver salen en
  orden con `diferida: true`. Se pierden con un reinicio.
- **Eventos**: hasta 32, no se descartan (con la cola llena un `BOTON` reemplaza al evento más
  antiguo que no sea `BOTON`). Se guardan en NVS mientras estén pendientes y sobreviven a un
  reinicio; salen con "(de antes de un reinicio de la base)" en `detalle`.
- **Al volver**: primero Telegram (`BOTON` primero) y los eventos, después el colgante y las
  lecturas en orden.

### Watchdog y reinicios (`watchdog_tareas`, `motivo_reinicio`)

- Watchdog de tareas de ESP-IDF de 10 s sobre `loop()` y sobre la tarea de red fuera de sus
  peticiones HTTPS (dentro, el vigilante de la red). Si vence, la base se reinicia.
- Al arrancar: `# Arranque: encendido`, `# Arranque: watchdog de tareas (reinicio inesperado)`,
  `# Arranque: vigilante de la red: ... (reinicio inesperado)`. Los inesperados (watchdog, panic,
  brownout, vigilante...) se avisan por Telegram; encendido, botón RST y USB no.

### Comandos por el monitor serial

Una letra y Enter (`pio device monitor -d firmware/base-radar --echo`):

| Comando | Qué hace |
|---|---|
| `b` | Buzzer fijo 10 s (`PRUEBA_BUZZER_MS`), para medir el voltaje en D10 |
| `s` | Silencia la alerta local (igual que el botón BOOT) |
| `e` | Estado: MAC, canal, antena, contadores de mensajes y del radar, colgantes conocidos, persona/objetivo/actividad y pico de la UART, WiFi (estado, SSID, canal, RSSI, IP, intentos, fallidos seguidos y desconexiones), Firebase, Telegram, eventos pendientes y cola offline (abajo) |
| `r` | PRUEBA: simula el radar sin datos (deja de leer tramas); otra vez `r` para volver |
| `p` | PRUEBA: mensaje simulado del último colgante, sin radio: alterna 3400 mV con `BAT_BAJA` y 3900 mV (evento `BATERIA_BAJA` y su rearme) |
| `w` | PRUEBA: cuelga `loop()` a propósito para que el watchdog reinicie la base (10 s) |
| `h` | Ayuda |

Con `e`, la nube se ve así:

```
# Detectores: 1 colgantes conocidos (ausente tras 45 min sin mensajes), radar normal
...
# Firebase: sesion activa (uid AbC123...), inicios 1, renovaciones 0
# Firebase: ultima escritura OK hace 3 s; OK 128, errores 1 (rechazadas 0)
# Firebase: ultima peticion: PATCH colgantes/1 -> HTTP 204 (231 ms), hace 2 s
# Firebase: ultimo error: PUT lecturas/base01/1791234567890 -> error -11 read Timeout (6004 ms), hace 40 s
# Firebase: hora del servidor sincronizada, ahora 1791234612345 ms (referencia de hace 21 s)
# Telegram: activo; enviados 3, errores 0, rechazados 0; ultimo OK hace 120 s; proximo aviso no urgente en 0 s
# Red: peticion en curso: PUT lecturas/base01/1791234597890, lleva 0.3 s (corte a los 45 s)
# Red: vigilante: 0 cortes
# Cola: eventos pendientes 0/32 (Firebase 0, Telegram 0), guardados en NVS; rechazados 0, reemplazados por un BOTON 0
# Cola offline: lecturas 0/720 (0 s de lecturas), descartadas 0; colgantes 0/8
# Memoria: heap libre 182340 B (minimo 140112 B), pila libre de la tarea de red 5120 B
```

`errores` cuenta toda petición fallida, también los inicios de sesión y las renovaciones del
token que fallan (hasta la v0.5.1 esos no se sumaban).

(Números de ejemplo: los reales salen de la prueba.)

Con `FIREBASE_TRAZA 1` (`config.h`) cada petición HTTPS deja además una línea con método, ruta
(sin token), código o error y duración, p. ej. `firebase: PATCH dispositivos/base01/estado -> HTTP
204 (402 ms)`. Cada espera de la red tiene un límite corto y un vigilante corta una petición que
pasa de 45 s; detalle en `docs/fallos.md`, "Bloqueo de la tarea de red".

## Configuración

- `include/config.h`: versión, salida CSV, antena, pines, botón, tiempos y brillo de la alerta
  local, WiFi (`BASE_WIFI_HABILITADO`, esperas de reconexión), canal sin WiFi, rangos válidos del
  radar y tiempo de "radar sin datos".
- `include/secrets.h`: credenciales. No se sube al repo. Con `BASE_WIFI_HABILITADO 1` es
  obligatorio; si falta, la compilación falla con un mensaje que lo pide. Crearlo así:

  ```
  copy firmware\base-radar\include\secrets.example.h firmware\base-radar\include\secrets.h
  ```

  Campos que hay que llenar:

  | Campo | De dónde sale |
  |---|---|
  | `WIFI_SSID`, `WIFI_CLAVE` | red de la casa, 2.4 GHz |
  | `FIREBASE_URL` | consola de Firebase > Realtime Database: la URL de arriba de los datos, sin barra final (la misma que `database_url` del simulador) |
  | `FIREBASE_API_KEY` | Configuración del proyecto > General > Clave de API web (la misma que `api_key` del simulador) |
  | `DISPOSITIVO_EMAIL`, `DISPOSITIVO_CLAVE` | la cuenta de la base en Authentication > Usuarios (la misma que `base` del simulador) |
  | `BASE_ID` | el valor del uid de esa cuenta en `/cuentas_dispositivo`, p. ej. `base01` |
  | `TELEGRAM_TOKEN`, `TELEGRAM_CHAT_ID` | el bot de @BotFather y el chat del cuidador (abajo, "Crear el bot de Telegram") |

  Mientras `FIREBASE_URL`, `FIREBASE_API_KEY` o `DISPOSITIVO_CLAVE` tengan los valores de
  ejemplo, la base funciona igual sin Firebase y lo avisa (`firebase: deshabilitado: ...`). Lo
  mismo con `TELEGRAM_TOKEN` y `TELEGRAM_CHAT_ID` (`telegram: deshabilitado: ...`). Cada uno
  funciona sin el otro.

## Memoria

`pio run` del entorno `base` con Firebase y Telegram (v0.6.0, arduino-esp32 3.3.7, 2026-10-27):

| | Usado | Total | % |
|---|---|---|---|
| RAM estática | 88 424 B | 327 680 B | 27.0 % |
| Flash (partición de la app) | 1 250 898 B | 3 145 728 B | 39.8 % |

Antes (v0.5.1, `default.csv`): 65 416 B de RAM (20.0 %) y 1 235 430 B de 1 310 720 B de flash
(94.3 %). Los 23 kB de RAM nuevos son casi todos la cola offline (720 lecturas, ~20 kB) y los
eventos (~3 kB). Telegram sumó ~15 kB de flash: reusa el TLS, `HTTPClient` y el bundle de CA.

Cada conexión TLS usa además ~40-50 kB de heap mientras está abierta: la de la base de datos
siempre, la de Telegram hasta 30 s después de un aviso y la de Auth durante un inicio de sesión
(hasta tres a la vez). El comando `e` muestra el heap libre y el mínimo. Por confirmar con el
módulo.

### Particiones (sin OTA)

El proyecto no usa OTA, así que la base usa `board_build.partitions = huge_app.csv` en
`platformio.ini`. Tablas de 4 MB que trae arduino-esp32 3.3.7 (`tools/partitions/` del paquete
`framework-arduinoespressif32`):

| Tabla | App | Otros | Notas |
|---|---|---|---|
| `default.csv` (antes) | 2 × 1.25 MB (OTA) | SPIFFS 1.4 MB | la app estaba al 94.3 % |
| `no_ota.csv` | 2 MB | SPIFFS 1.9 MB | |
| **`huge_app.csv`** (elegida) | **3 MB** | SPIFFS 896 kB (sin uso) | "Huge APP (3MB No OTA/1MB SPIFFS)" del IDE de Arduino |
| `max_app_4MB.csv` | 3.9 MB | ninguno | la app como `factory` |

Se eligió `huge_app.csv`: da 2.4 veces el espacio de antes (la app queda al 40 %, con margen para
años de cambios), es la tabla sin OTA más usada con estas placas y no cambia nada más: la app
sigue en 0x10000 como `ota_0` (el `boot_app0.bin` que carga PlatformIO la elige igual que antes) y
`nvs` (0x9000, 20 kB), `otadata` (0xE000) y `coredump` (0x3F0000) quedan en las mismas
direcciones. `max_app_4MB.csv` da más, pero no hace falta y cambia el tipo de la app a `factory`.

**No hace falta borrar la flash al cambiarla.** En cada carga PlatformIO escribe el bootloader, la
tabla de particiones (0x8000), `otadata` y la app. NVS está en el mismo lugar, así que se
conservan los eventos pendientes y el estado de los detectores; lo que quede de la vieja `app1` o
de SPIFFS son bytes sin uso que nadie lee. Solo hay que borrarla si se quiere empezar limpio
(olvidar eventos pendientes, colgantes conocidos y calibración del WiFi) o si alguna vez se mueve
NVS:

```
pio run -d firmware/base-radar -t erase
pio run -d firmware/base-radar -t upload
```

`erase` borra los 4 MB, NVS incluida. Si el puerto no responde, mantener presionado BOOT mientras
se conecta el USB para entrar al modo de carga, y soltarlo antes de reiniciar.

## Pruebas host

`pruebas_host/` compila en el PC la lógica pura de la base (ventana de actividad, JSON de
Firebase y Telegram, cola de peticiones y cola offline, lector del cuerpo HTTP, vigilante de la
tarea de red, detectores de eventos y límite de Telegram) con los `.cpp` reales y la ejecuta, sin
ESP32 (63 casos, 1674 verificaciones):

```
powershell -File firmware\base-radar\pruebas_host\ejecutar.ps1
bash firmware/base-radar/pruebas_host/ejecutar.sh
```

Los JSON esperados son los mismos que escribe el simulador y que aceptan las reglas publicadas.


## Hardware del kit

- **Antena externa confirmada.** El XIAO ESP32-C6 del kit tiene una antena externa conectada por
  U.FL. La variante `XIAO_ESP32C6` de arduino-esp32 3.3.7 (`initVariant()` en `variant.cpp`) pone
  `WIFI_ENABLE` (GPIO3) en bajo, que habilita el conmutador de RF, y `WIFI_ANT_CONFIG` (GPIO14) en
  bajo, que elige la antena de chip. Con `BASE_ANTENA_EXTERNA 1`, `setup()` pone GPIO14 en alto
  antes de iniciar WiFi.
- **Tiras hembra de 2.54 mm** de la placa del kit (rotuladas):

  | Izquierda | GPIO | Derecha | GPIO |
  |---|---|---|---|
  | TXD | GPIO16 (D6), UART del radar | RXD | GPIO17 (D7), UART del radar |
  | D8 | GPIO19 | SCL | GPIO23 (D5), I2C del BH1750 |
  | D9 | GPIO20 | SDA | GPIO22 (D4), I2C del BH1750 |
  | D10 | GPIO18, **buzzer** | RST | reset |
  | 3V3 | | D2 | GPIO2 |
  | GND | | D1 | GPIO1, **LED RGB WS2812** de la placa |
  | 5V | | D0 | GPIO0 |

  Grove: GND, VCC, D10, D0.
- **Radar**: UART0, RX = GPIO17 (D7), TX = GPIO16 (D6), 115200 baudios. Confirmado con el kit.
- **Buzzer**: módulo de buzzer activo sin transistor (negro, tipo KY-012). `S` a D10 (GPIO18),
  `-` a GND. Suena con el pin en alto; la corriente sale directo del GPIO, por eso el pin se
  configura con la fuerza de salida máxima (`GPIO_DRIVE_CAP_3`, ver `config.h`). La corriente no
  se pudo medir (fusible de mA del multímetro quemado); se valida con la prueba de voltaje de
  abajo.
- **LED**: el WS2812 de la placa en D1, con `rgbLedWrite()` de arduino-esp32 3.x (sin librerías).
- **Botón BOOT**: GPIO9. Confirmado en el esquemático oficial de Seeed
  (`XIAO_ESP32_C6_v1.0_SCH_260114.pdf`, hoja 4: red BOOT al pin 15 del ESP32-C6, R17 de 10 kΩ a
  3V3 y pulsador a GND) y en la wiki de Seeed ("Boot: GPIO9"). Se lee sin bloqueo con antirrebote
  de 30 ms; una pulsación corta silencia la alerta local. **Mantenerlo presionado al reiniciar o
  al conectar la alimentación entra en modo de carga (bootloader)**: el firmware no arranca y la
  base queda sin alerta hasta reiniciar con el botón suelto. Por confirmar con el módulo que el
  botón quede accesible con la placa montada en el kit.
- Al cerrar el puerto serie con DTR/RTS activos, la base se reinicia (ver `docs/radar.md`).

## Prueba de voltaje del buzzer en D10

Objetivo: confirmar que el GPIO aguanta el buzzer sin transistor.

1. Cargar la base (`pio run -d firmware/base-radar -t upload`) con el buzzer conectado.
2. Abrir el monitor, escribir `b` y Enter. El buzzer suena fijo 10 s.
3. Mientras suena, medir con el voltímetro entre D10 y GND.
4. Criterio: **3.0 V o más** pasa. Con menos, el pin cae demasiado bajo carga: poner un transistor
   entre D10 y el buzzer, o cambiar a un módulo con transistor.
5. Anotar el voltaje medido aquí y en `pruebas/resultados/`.

Resultado: por confirmar con el módulo.

## Guía de prueba con el colgante

Escrita para la fase local (`BASE_WIFI_HABILITADO 0`, canal 1, sin `secrets.h`). Con WiFi
habilitado vale igual, pero el canal es el del router (ver la guía de la fase WiFi).

1. **Base**: cargar el firmware y abrir el monitor
   (`pio run -d firmware/base-radar -t upload`, `pio device monitor -d firmware/base-radar --echo`).
   Debe imprimir la versión, la MAC STA, una línea `#define BASE_MAC { ... }`, `Canal 1, antena
   externa (U.FL)` y quedar con el LED en verde tenue.
2. **MAC**: copiar la línea `#define BASE_MAC { ... }` (sin nada antes del `#define`) en
   `firmware/colgante/include/secrets.h`, reemplazando la anterior.
3. **Colgante**: cargarlo (`pio run -d firmware/colgante -t upload`). Al arrancar manda un
   `HEARTBEAT` con `ARRANQUE_FRIO`: en la base debe salir `HEARTBEAT ... NUEVO ack=OK` y el
   colgante no debe sonar.
4. **ALERTA**: presionar el botón. En la base, `ALERTA ... NUEVO ack=OK`, LED rojo y buzzer
   intermitentes durante 60 s; el colgante da el beep corto de confirmación. `s` silencia antes.
5. **HEARTBEAT**: esperar el intervalo del colgante (15 min, o el corto de prueba si está
   configurado). Debe salir `HEARTBEAT ... NUEVO` sin alerta local.
6. **Duplicados**: presionar el botón varias veces durante una alerta; cada pulsación es una
   secuencia nueva (`NUEVO`) y reinicia los 60 s. Un `DUPLICADO` solo aparece cuando se pierde un
   ACK y el colgante reintenta la misma secuencia: sale `DUPLICADO ack=OK` y la alerta no se
   reinicia. Para provocarlo, alejar el colgante hasta el límite del alcance y mirar los mensajes
   con `REINTENTO` (por confirmar con el módulo que se logre así; la regla es la misma que pasó la
   prueba 6 con el receptor de prueba). Con `e` se ven los contadores.
7. **LED y buzzer**: verde tenue en reposo, rojo y buzzer en la ALERTA, amarillo sin sonido si
   llega un mensaje con `BAT_BAJA` o el radar se queda sin datos. `b` para la prueba de voltaje.

## Guía de prueba de la fase WiFi

Necesita la base con `secrets.h` (solo `WIFI_SSID` y `WIFI_CLAVE`), el colgante cargado con la MAC
de la base y acceso al router (para ver o cambiar su canal y apagarlo).

1. **Conectar al router**: cargar la base y abrir el monitor
   (`pio run -d firmware/base-radar -t upload`, `pio device monitor -d firmware/base-radar --echo`).
   Debe salir `wifi: intento 1, buscando "..."` y, en pocos segundos,
   `wifi: conectado a "...", canal N, RSSI ... dBm, IP ...`. Si sale `fallo el intento` con
   `AUTH_FAIL` o `4WAY_HANDSHAKE_TIMEOUT`, revisar la clave; con `NO_AP_FOUND`, el SSID o que la
   red sea de 2.4 GHz.
2. **Ver el canal**: con `e` se ve `WiFi: conectado, SSID "...", canal N, RSSI ...` y los
   intentos. El canal debe coincidir con el que muestra la página del router. Anotarlo.
3. **El colgante encuentra la base por barrido**: si el canal del router no es el que el colgante
   tiene guardado (el 1 tras encenderlo), el colgante no recibe ACK y barre. Presionar el botón:
   en la base debe salir `ALERTA ... flags=REINTENTO|BARRIDO ... canal=N NUEVO ack=OK`, la alerta
   local suena y el colgante da el beep corto. La siguiente pulsación llega con `flags=-`, ya en
   el canal guardado. El ACK lleva el canal de la base, así que el colgante guarda ese aunque haya
   oído el ACK en un canal vecino (en su monitor sale `ACK recibido en el canal X, se guarda el
   canal de la base: N`). Si el router está en el canal 1, cambiarlo a otro (por ejemplo 6 u 11) en
   la configuración del router para probar el barrido.
4. **Apagar el router**: tras unos segundos debe salir
   `wifi: desconectado de "...", canal N, ultimo RSSI ..., motivo ...` y luego intentos fallidos
   con esperas de 5, 10, 20, 40... s. Anotar las líneas `radio: canal X -> Y` y, sobre todo,
   **en qué canal queda la radio en estado `esperando`** (dato pendiente en `docs/fallos.md`).
   Durante la espera, presionar el botón del colgante: la alerta local debe sonar igual; anotar
   en qué canal llegó el mensaje.
5. **Encender el router**: en el siguiente intento debe salir `wifi: conectado ...`. Con `e`,
   `desconexiones 1` y `fallidos seguidos 0`. Si el router volvió en otro canal, el colgante lo
   encuentra con el barrido.
6. **Botón BOOT**: durante una alerta, una pulsación corta del botón BOOT la silencia
   (`Alerta local silenciada (boton BOOT)`). No presionarlo al reiniciar.

Resultados: anotarlos en `pruebas/resultados/`. Por confirmar con el módulo.

## Guía de prueba de Firebase

Necesita `secrets.h` completo (tabla de arriba), las reglas publicadas, la cuenta de la base en
`/cuentas_dispositivo` y el colgante cargado. En la consola de Firebase abrir Realtime Database >
Datos: los cambios se ven en vivo (se resaltan en amarillo un momento).

1. **Sesión**: cargar la base y abrir el monitor. Tras `wifi: conectado` debe salir
   `firebase: sesion iniciada: ... (uid ...)`. Si sale `no se pudo iniciar sesion ... HTTP 400
   INVALID_LOGIN_CREDENTIALS`, revisar correo y clave; con `API key not valid`, la API key.
2. **Estado**: enseguida aparece `/dispositivos/{BASE_ID}/estado` con `online: true`,
   `ultimo_contacto` (ms), `rssi_wifi` y `version_firmware: "0.6.0"`. `ultimo_contacto` cambia
   cada minuto. Si sale `401 ... Permission denied` dos veces seguidas, el uid no está en
   `/cuentas_dispositivo` o no vale `BASE_ID`. Con `e`: `hora del servidor sincronizada`.
3. **Lecturas**: en `/lecturas/{BASE_ID}` aparece una clave de 13 dígitos cada 5 s. Sin nadie
   frente al radar: `persona: false, objetivo: false` (o `objetivo: true` con `actividad` baja si
   apunta a una pared). Sentarse o acostarse frente al radar: en menos de 30 s, `persona: true`,
   `actividad` ≥ 0.03 y `distancia_cm`; `resp_rpm` y `latido_bpm` solo cuando el módulo da un
   valor. La clave debe estar cerca de la hora real (convertir con cualquier conversor de epoch en
   ms). Comparar con las líneas `radar: persona detectada` del monitor.
4. **Colgante**: al encender el colgante (HEARTBEAT con `ARRANQUE_FRIO`) aparece `/colgantes/{id}`
   con `bateria_mv`, `ultimo_heartbeat`, `rssi` y `base`. Un reintento `DUPLICADO` no lo
   actualiza.
5. **Evento BOTON**: presionar el botón del colgante. La alerta local suena enseguida (no espera a
   la nube) y en `/eventos` aparece un nodo nuevo con `tipo: "BOTON"`, `origen: "colganteN"`,
   `timestamp`, `atendido: false` y `detalle: "boton del colgante"`; por serial,
   `firebase: evento BOTON de colganteN enviado`. Una pulsación = un evento.
6. **Sin red**: apagar el router con la base funcionando y presionar el botón: la alerta local y
   el ACK siguen igual. Al volver el router, el evento llega a `/eventos` con
   `(enviado N s despues)` en `detalle`, y las lecturas que esperaban (hasta 1 h) llegan en orden
   con `diferida: true`. Con más de 1 h sin red se pierden las lecturas más viejas (`descartadas`
   en `e`). Más detalle en la guía de los eventos, paso 7.
7. **Token**: dejar la base más de 1 h: debe salir `firebase: token renovado` a los ~55 min sin
   errores de escritura.
8. **Sin bloqueos**: con `e`, `pico de la UART` debe quedar por debajo de 2800 B (no se perdieron
   tramas del radar durante el TLS), y `pila libre de la tarea de red` por encima de ~1 kB.
9. **Internet cortado con el WiFi conectado** (la falla del 2026-10-14, `docs/fallos.md`):
   desconectar el cable de internet del router (o cortar el internet del plan) sin apagar el WiFi.
   Las líneas de traza deben seguir saliendo con errores y su texto (`read Timeout`, `sin
   conexion`), nunca más de 45 s sin una línea; con `e`, `ultimo error` cambia y `peticion en
   curso` nunca pasa de 45 s. Si llega a 45 s debe salir `VIGILANTE: ... se cierra su socket` y
   `cliente TLS de la base de datos recreado`. Al volver internet, escrituras OK en menos de un
   minuto y `hora del servidor sincronizada`. Probar también con una alerta del colgante sonando:
   la base no se reinicia mientras dura.

Resultados: anotarlos en `pruebas/resultados/`. Todo por confirmar con el módulo.

## Crear el bot de Telegram

Todo desde la app de Telegram y la API oficial de bots (`api.telegram.org`), sin servicios de
terceros.

1. **Crear el bot.** En Telegram buscar **@BotFather** (cuenta verificada, con la marca azul) y
   abrir el chat. Enviar `/newbot`. Pide un nombre (p. ej. `Monitor Abuela`) y un usuario que
   termine en `bot` (p. ej. `monitor_abuela_bot`). Responde con el **token**, de la forma
   `123456789:AAH...`. Ese es `TELEGRAM_TOKEN`. Quien tenga el token controla el bot: no subirlo
   al repo ni mandarlo por chat; si se filtra, `/revoke` en @BotFather da uno nuevo.
2. **Elegir el chat.**
   - Un solo cuidador: abrir el chat con el bot (`t.me/<usuario_del_bot>`), tocar **Iniciar** (o
     enviar `/start`). Un bot no puede escribirle a alguien que nunca le escribió.
   - Varios cuidadores: crear un grupo, agregar al bot como miembro y enviar en el grupo
     `/start@<usuario_del_bot>` (con el modo privado por defecto el bot solo ve los comandos que lo
     nombran).
3. **Obtener el `chat_id`.** En el navegador abrir
   `https://api.telegram.org/bot<TOKEN>/getUpdates` (con el token del paso 1, sin `<>`). En la
   respuesta buscar `"chat":{"id":...`. Ese número es `TELEGRAM_CHAT_ID`: positivo para un chat
   privado, negativo para un grupo (los supergrupos empiezan con `-100`). Si sale `"result":[]`,
   enviar otro mensaje al bot (o al grupo, paso 2) y recargar.
4. **Probar el bot sin la base.** Abrir
   `https://api.telegram.org/bot<TOKEN>/sendMessage?chat_id=<CHAT_ID>&text=prueba`. Debe llegar
   "prueba" al chat y la página debe decir `"ok":true`. Con `"error_code":403` el bot no está en el
   grupo o el cuidador no le escribió; con 400 `chat not found`, el chat_id está mal.
5. **Configurar la base.** En `include/secrets.h`:

   ```
   #define TELEGRAM_TOKEN    "123456789:AAH..."
   #define TELEGRAM_CHAT_ID  "987654321"
   ```

   (Con comillas: el chat_id va como texto.) Cargar la base: debe salir `telegram: activo`.
6. Borrar del historial del navegador las direcciones con el token.

## Guía de prueba de los eventos

Necesita la base con `secrets.h` completo (Firebase y Telegram), el colgante cargado con la MAC de
la base, la consola de Firebase en Realtime Database > Datos y el chat de Telegram abierto. Cada
paso dice qué ver por serial, en `/eventos` y en Telegram. Con `e` se ven los contadores. Todo por
confirmar con el módulo; anotar los resultados en `pruebas/resultados/`.

1. **Arranque.** Cargar la base y abrir el monitor. Debe salir `# Arranque: USB (carga del
   firmware o monitor serial)` (o `encendido`), `watchdog: watchdog de tareas activo sobre loop(),
   10 s`, `telegram: activo` y, tras `firebase: sesion iniciada`, el estado. Sin aviso de
   reinicio en Telegram: no fue inesperado.
2. **BOTON.** Presionar el botón del colgante. La alerta local suena enseguida; en Telegram debe
   llegar "ALERTA: botón de auxilio del colgante 1..." en pocos segundos y en `/eventos` un
   `BOTON`. Por serial: `telegram: POST telegram/sendMessage -> HTTP 200 (... ms)` y `telegram:
   aviso BOTON de colgante1 enviado`. Anotar el tiempo del botón al mensaje en el teléfono.
   Presionar 3 veces seguidas: llegan los 3 mensajes, con ~1 s entre ellos como mínimo.
3. **Telegram con Firebase caído.** Poner un `FIREBASE_URL` inválido (p. ej. otro nombre de
   proyecto) en `secrets.h`, cargar y presionar el botón: el mensaje de Telegram debe llegar igual
   aunque Firebase dé errores. Volver a la URL buena.
4. **BATERIA_BAJA.** El colgante no se puede compilar con batería simulada baja (un
   `static_assert` de `firmware/colgante/src/bateria.cpp` lo impide), así que se prueba con el
   comando `p` de la base, que simula un mensaje del último colgante que habló. Primer `p`:
   `PRUEBA: mensaje simulado del colgante 1 con 3400 mV y BAT_BAJA`, `colgante 1: bateria baja
   (3400 mV), evento BATERIA_BAJA`, el evento en `/eventos` y "Batería baja en el colgante 1 (3400
   mV)..." en Telegram. Reiniciar la base (RST) y escribir `p` (de nuevo 3400 mV): no debe haber
   un evento nuevo (misma descarga, estado en NVS). Un `p` con 3900 mV rearma; el siguiente con
   3400 mV da un evento nuevo. Con la batería real del colgante, por confirmar con el módulo que
   la bandera llegue sola al bajar de 3500 mV.
5. **COLGANTE_AUSENTE.** Para no esperar 45 min, poner en `include/config.h` de la base
   `#define COLGANTE_AUSENTE_MS 120000UL` (2 min) y cargarla. Con el colgante encendido, mandar un
   mensaje (botón) y después desconectarlo de la alimentación. A los 2 min: `colgante 1: sin
   mensajes por 2 min, evento COLGANTE_AUSENTE`, el evento y "El colgante 1 no se comunica...". No
   debe repetirse en los minutos siguientes. Reconectar el colgante: `colgante 1: volvio a
   comunicarse`. Volver a `(COLGANTE_AUSENTE_S * 1000UL)`.
6. **RADAR_SIN_DATOS.** Escribir `r` en el monitor (simula el radar sin datos). A los 5 s: `radar
   sin datos` y aviso amarillo; a los 30 s: `radar sin datos por 30 s, evento RADAR_SIN_DATOS`, el
   evento y el mensaje en Telegram. Nada más mientras siga así. `r` otra vez: a los 60 s con datos,
   `termina el episodio sin datos`. Repetir `r` antes de esos 60 s: no debe haber un evento nuevo.
7. **Sin red y reinicio con eventos pendientes.** Apagar el router. Presionar el botón: alerta
   local, sin Telegram. Con `e`: `Cola: eventos pendientes 1/32 (Firebase 1, Telegram 1), guardados
   en NVS`. Presionar el botón RST de la base (o desconectarla y conectarla): al arrancar,
   `firebase: 1 eventos pendientes de antes del reinicio, restaurados de NVS`. Encender el router:
   llegan el `BOTON` con "(aviso de antes de un reinicio de la base)" en Telegram y "(de antes de
   un reinicio de la base)" en `detalle`, y las lecturas con `diferida: true` (las de antes del
   reinicio no: se perdieron).
8. **BASE_DESCONECTADA.** Desconectar la base de la alimentación 4 min y volver a conectarla. Tras
   la sesión: `firebase: la base estuvo 4 min sin actualizar su estado: evento
   BASE_DESCONECTADA`, el evento con "sin conexión por 4 min" y "La base base01 volvió a estar en
   línea (sin conexión por 4 min)". Lo mismo apagando el router 4 min con la base encendida. Con
   1 min no debe haber evento.
9. **Reinicio inesperado.** Escribir `w` en el monitor: `loop()` se cuelga y a los 10 s la base se
   reinicia. Al arrancar: `# Arranque: watchdog de tareas (reinicio inesperado)` (o `fallo del
   programa (panic)`, según cómo lo informe ESP-IDF: anotarlo) y en Telegram "La base base01 se
   reinició de forma inesperada: ...".
10. **Límite de frecuencia.** Con `COLGANTE_AUSENTE_MS` corto y varios avisos seguidos, con `e`
    se ve `proximo aviso no urgente en N s`; ningún mensaje se pierde, llegan espaciados.

## Pendiente de confirmar con el módulo

- Voltaje en D10 con el buzzer sonando (prueba de arriba).
- Tono del amarillo y del verde tenue en el WS2812 del kit, y que el brillo sea adecuado de noche.
- RSSI del colgante con la antena externa de la base.
- Fase WiFi en hardware (guía de arriba): conexión, reconexión con espera creciente y en qué
  canal queda la radio entre intentos.
- Que el botón BOOT sea accesible con la placa montada en el kit.
- Firebase en hardware (guía de arriba): handshake TLS con el bundle de CA, keep-alive, heap
  libre con dos conexiones TLS, pila de la tarea de red y pico de la UART durante el TLS.
- Que el handshake TLS con el bundle de CA quepa holgado en `FIREBASE_TIMEOUT_TLS_MS` (10 s) y
  que el corte del vigilante (`shutdown()` del socket desde `loop()`) despierte a la tarea de red
  en hardware (paso 9 de la guía de Firebase).
- Todo lo nuevo de la v0.6.0 (guía de los eventos): Telegram con el certificado de
  `api.telegram.org`, tiempo del botón al mensaje, cola offline de 1 h y su vaciado, eventos en
  NVS tras un reinicio, cada evento una sola vez por episodio, watchdog de tareas y el motivo que
  informa ESP-IDF tras el comando `w`.
- Heap libre con tres conexiones TLS a la vez (Auth, base de datos y Telegram) y pila de la tarea
  de red con Telegram.
- Cuándo cierra Telegram una conexión inactiva (`TELEGRAM_REUSO_MAX_MS`, 30 s).
