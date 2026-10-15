# firmware/base-radar

Firmware del XIAO ESP32-C6 que viene en el kit Seeed MR60BHA2.

## Módulos (`src/`)

| Módulo | Responsabilidad |
|---|---|
| `radar` | Leer respiración, latido, presencia y distancia del radar por UART; ventana de actividad de 30 s |
| `ventana_actividad` | Criterio "persona detectada": mediana de \|`fase_total`\| en 30 s, umbral 0.03 (lógica pura) |
| `espnow_rx` | Recibir mensajes del colgante, filtrar duplicados, responder ACK |
| `red_wifi` | Conexión WiFi STA y reconexión |
| `firebase_cliente` | Tarea de red: sesión de Firebase Auth y escritura en Realtime Database por REST (HTTPS) |
| `formato_nube` | JSON exacto que aceptan las reglas y lectura de las respuestas (lógica pura) |
| `cola_peticiones` | Cola acotada de loop() a la tarea de red (lógica pura) |
| `telegram` | Mensajes al cuidador por la API de bots |
| `alerta_local` | Buzzer y LED; funciona sin red |
| `boton_silencio` | Botón BOOT de la placa: silencia la alerta local |
| `cola_offline` | Guardar lecturas cuando no hay red y reenviarlas al volver |

Nube, parte 1: están implementados `radar` (ver `docs/radar.md`), `espnow_rx`, `alerta_local`,
`boton_silencio`, `red_wifi` y `firebase_cliente`. WiFi, ESP-NOW con el colgante, ACK con canal,
LED, buzzer, botón BOOT y radar ya funcionaron en hardware. Firebase está compilado y probado en
PC (`pruebas_host/`), pero **por confirmar con el módulo** (guía de abajo). `telegram` y
`cola_offline` (la cola sin red completa, parte 2) siguen vacíos con `TODO`.

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

**Cola acotada** (`cola_peticiones`, capacidades en `config.h`): eventos aparte (32), una
entrada por colgante (8) y lecturas (24, 2 min). Llena, se descarta la lectura más antigua; una
lectura nunca desplaza un evento. Sale primero lo más importante: eventos, colgantes, lecturas.
La cola offline completa (horas sin red) es la parte 2.

Registros:

```
# [     2510 ms] firebase: sesion iniciada: base01@... (uid AbC123...), token por 3600 s
# [  3302511 ms] firebase: token renovado, vence en 3600 s
# [    52400 ms] firebase: evento BOTON de colgante1 enviado
# [   120330 ms] firebase: ERROR PUT lecturas/base01/1791234567890: HTTP -11 read Timeout (se reintenta)
# [   130020 ms] firebase: 401 en dispositivos/base01/estado (Permission denied); se inicia sesion de nuevo
```

### Comandos por el monitor serial

Una letra y Enter (`pio device monitor -d firmware/base-radar --echo`):

| Comando | Qué hace |
|---|---|
| `b` | Buzzer fijo 10 s (`PRUEBA_BUZZER_MS`), para medir el voltaje en D10 |
| `s` | Silencia la alerta local (igual que el botón BOOT) |
| `e` | Estado: MAC, canal, antena, contadores de mensajes y del radar, persona/objetivo/actividad y pico de la UART, WiFi (estado, SSID, canal, RSSI, IP, intentos, fallidos seguidos y desconexiones) y Firebase (abajo) |
| `h` | Ayuda |

Con `e`, Firebase se ve así:

```
# Firebase: sesion activa (uid AbC123...), inicios 1, renovaciones 0
# Firebase: ultima escritura OK hace 3 s; OK 128, errores 1 (rechazadas 0), ultimo: HTTP -11 en lecturas/base01/1791234567890
# Firebase: cola eventos 0/32, colgantes 0/8, lecturas 0/24 (lecturas descartadas 0, eventos sin lugar 0)
# Firebase: hora del servidor sincronizada, ahora 1791234612345 ms (referencia de hace 21 s)
# Memoria: heap libre 182340 B (minimo 140112 B), pila libre de la tarea de red 5120 B
```

(Números de ejemplo: los reales salen de la prueba.)

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

  Mientras `FIREBASE_URL`, `FIREBASE_API_KEY` o `DISPOSITIVO_CLAVE` tengan los valores de
  ejemplo, la base funciona igual sin nube y lo avisa (`firebase: deshabilitado: ...`). Telegram
  todavía no se usa.

## Memoria

`pio run` del entorno `base` con Firebase (arduino-esp32 3.3.7):

| | Usado | Total | % |
|---|---|---|---|
| RAM estática | 60 832 B | 327 680 B | 18.6 % |
| Flash (partición de la app) | 1 230 864 B | 1 310 720 B | **93.9 %** |

El TLS, `HTTPClient` y el bundle de CA (~69 kB) suman ~230 kB de flash; quedan ~78 kB libres. La
partición por defecto (`default.csv`) reparte los 4 MB en dos apps de 1.25 MB para OTA, que este
proyecto no usa. Si Telegram (parte 2) no cabe, la salida es `board_build.partitions =
no_ota.csv` en `platformio.ini` (una app de 2 MB). Cada conexión TLS usa además ~40-50 kB de
heap mientras está abierta (dos a la vez durante un inicio de sesión); el comando `e` muestra el
heap libre y el mínimo. Por confirmar con el módulo.

## Pruebas host

`pruebas_host/` compila en el PC la lógica pura de la base (ventana de actividad, JSON de
Firebase, cola de peticiones) con los `.cpp` reales y la ejecuta, sin ESP32:

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
   `ultimo_contacto` (ms), `rssi_wifi` y `version_firmware: "0.5.0"`. `ultimo_contacto` cambia
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
   `(enviado N s despues)` en `detalle`, y las lecturas que esperaban (hasta 2 min) llegan con
   `diferida: true`. Con más de 2 min sin red se pierden las lecturas más viejas (`lecturas
   descartadas` en `e`); la cola completa es la parte 2.
7. **Token**: dejar la base más de 1 h: debe salir `firebase: token renovado` a los ~55 min sin
   errores de escritura.
8. **Sin bloqueos**: con `e`, `pico de la UART` debe quedar por debajo de 2800 B (no se perdieron
   tramas del radar durante el TLS), y `pila libre de la tarea de red` por encima de ~1 kB.

Resultados: anotarlos en `pruebas/resultados/`. Todo por confirmar con el módulo.

## Pendiente de confirmar con el módulo

- Voltaje en D10 con el buzzer sonando (prueba de arriba).
- Tono del amarillo y del verde tenue en el WS2812 del kit, y que el brillo sea adecuado de noche.
- RSSI del colgante con la antena externa de la base.
- Fase WiFi en hardware (guía de arriba): conexión, reconexión con espera creciente y en qué
  canal queda la radio entre intentos.
- Que el botón BOOT sea accesible con la placa montada en el kit.
- Firebase en hardware (guía de arriba): handshake TLS con el bundle de CA, keep-alive, heap
  libre con dos conexiones TLS, pila de la tarea de red y pico de la UART durante el TLS.
