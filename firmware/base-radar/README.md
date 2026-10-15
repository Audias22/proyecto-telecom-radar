# firmware/base-radar

Firmware del XIAO ESP32-C6 que viene en el kit Seeed MR60BHA2.

## Módulos (`src/`)

| Módulo | Responsabilidad |
|---|---|
| `radar` | Leer respiración, latido, presencia y distancia del radar por UART |
| `espnow_rx` | Recibir mensajes del colgante, filtrar duplicados, responder ACK |
| `red_wifi` | Conexión WiFi STA y reconexión |
| `firebase_cliente` | Login del dispositivo y escritura en Realtime Database por REST |
| `telegram` | Mensajes al cuidador por la API de bots |
| `alerta_local` | Buzzer y LED; funciona sin red |
| `boton_silencio` | Botón BOOT de la placa: silencia la alerta local |
| `cola_offline` | Guardar lecturas cuando no hay red y reenviarlas al volver |

Fase WiFi: están implementados `radar` (ver `docs/radar.md`), `espnow_rx`, `alerta_local`,
`boton_silencio` y `red_wifi`. La fase local (sin WiFi) ya funcionó en hardware con el colgante;
la conexión al router está lista pero sin probar en hardware. `firebase_cliente`, `telegram` y
`cola_offline` siguen vacíos con `TODO`.

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

### Comandos por el monitor serial

Una letra y Enter (`pio device monitor -d firmware/base-radar --echo`):

| Comando | Qué hace |
|---|---|
| `b` | Buzzer fijo 10 s (`PRUEBA_BUZZER_MS`), para medir el voltaje en D10 |
| `s` | Silencia la alerta local (igual que el botón BOOT) |
| `e` | Estado: MAC, canal, antena, contadores de mensajes y del radar, y WiFi: estado, SSID, canal, RSSI, IP, intentos, fallidos seguidos y desconexiones |
| `h` | Ayuda |

## Configuración

- `include/config.h`: versión, salida CSV, antena, pines, botón, tiempos y brillo de la alerta
  local, WiFi (`BASE_WIFI_HABILITADO`, esperas de reconexión), canal sin WiFi, rangos válidos del
  radar y tiempo de "radar sin datos".
- `include/secrets.h`: credenciales. No se sube al repo. Con `BASE_WIFI_HABILITADO 1` es
  obligatorio; si falta, la compilación falla con un mensaje que lo pide. Crearlo así:

  ```
  copy firmware\base-radar\include\secrets.example.h firmware\base-radar\include\secrets.h
  ```

  En esta fase solo hay que llenar `WIFI_SSID` y `WIFI_CLAVE` (red de 2.4 GHz). Los de Firebase,
  `BASE_ID` y Telegram pueden quedar con los valores de ejemplo hasta la siguiente fase.

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
   el canal guardado. Si el router está en el canal 1, cambiarlo a otro (por ejemplo 6 u 11) en
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

## Pendiente de confirmar con el módulo

- Voltaje en D10 con el buzzer sonando (prueba de arriba).
- Tono del amarillo y del verde tenue en el WS2812 del kit, y que el brillo sea adecuado de noche.
- RSSI del colgante con la antena externa de la base.
- Fase WiFi en hardware (guía de arriba): conexión, reconexión con espera creciente y en qué
  canal queda la radio entre intentos.
- Que el botón BOOT sea accesible con la placa montada en el kit.
