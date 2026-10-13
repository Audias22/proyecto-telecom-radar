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
| `cola_offline` | Guardar lecturas cuando no hay red y reenviarlas al volver |

Fase local: están implementados `radar` (ver `docs/radar.md`), `espnow_rx` y `alerta_local`. La
base todavía no se conecta a WiFi; `red_wifi`, `firebase_cliente`, `telegram` y `cola_offline`
siguen vacíos con `TODO`.

## Qué hace hoy

- **Colgante**: WiFi en modo STA sin conectarse, ahorro de energía desactivado y canal fijo
  `BASE_CANAL_SIN_WIFI` (1). Cuando `red_wifi` conecte la base al router, el canal será el del
  router y el colgante lo encontrará con su barrido (`docs/protocolo.md`, sección 6). El callback de
  recepción valida el paquete, aplica la regla de `duplicados.h` con el estado de cada id de
  colgante, responde el ACK en el mismo callback y copia el mensaje a una cola FreeRTOS. Es la
  misma lógica del receptor de prueba (`firmware/pruebas/receptor_espnow`), ya validada con el
  colgante.
- **Alerta local**: LED WS2812 de la placa y buzzer, sin `delay()`.

  | Situación | LED | Buzzer |
  |---|---|---|
  | En funcionamiento | verde tenue fijo | no |
  | ALERTA nueva del colgante | rojo intermitente, 60 s (`ALERTA_LOCAL_DURACION_MS`) | intermitente, 60 s |
  | Aviso: batería baja del colgante, radar sin datos, ESP-NOW no inició | amarillo intermitente, 10 s | no |

  Un duplicado no vuelve a disparar la alerta. Una ALERTA nueva durante otra reinicia los 60 s.
  `PATRON_APNEA` está definido pero el radar no lo dispara: la apnea por radar no se usa como
  alarma (`docs/radar.md`, sección 13.6). Brillos en `LED_BRILLO_ALERTA` y `LED_BRILLO_REPOSO`.
- **Serial** (115200): por defecto, registros legibles. Todos empiezan con `#`:

  ```
  # [    52310 ms] AA:BB:CC:DD:EE:FF ALERTA    id=1 seq=301 flags=- bat=3900 mV rssi=-48 dBm canal=1 NUEVO ack=OK
  # [    52420 ms] AA:BB:CC:DD:EE:FF ALERTA    id=1 seq=301 flags=REINTENTO bat=3900 mV rssi=-49 dBm canal=1 DUPLICADO ack=OK
  # [    60001 ms] radar: persona detectada
  ```

  `ack=OK` significa que `esp_now_send()` aceptó el ACK, no que llegó al colgante. Con
  `BASE_SALIDA_CSV 1` en `config.h` se imprime además una línea CSV por lectura del radar, para
  `analisis/capturar_serial.py`, que ignora las líneas con `#`.

### Comandos por el monitor serial

Una letra y Enter (`pio device monitor -d firmware/base-radar --echo`):

| Comando | Qué hace |
|---|---|
| `b` | Buzzer fijo 10 s (`PRUEBA_BUZZER_MS`), para medir el voltaje en D10 |
| `s` | Silencia la alerta local |
| `e` | Estado: MAC, canal, antena, contadores de mensajes y del radar |
| `h` | Ayuda |

## Configuración

- `include/config.h`: versión, salida CSV, antena, pines, tiempos y brillo de la alerta local,
  canal sin WiFi, rangos válidos del radar y tiempo de "radar sin datos".
- `include/secrets.h`: credenciales. Copiar de `include/secrets.example.h`. No se sube al repo.
  En la fase local todavía no se usa.

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

## Pendiente de confirmar con el módulo

- Voltaje en D10 con el buzzer sonando (prueba de arriba).
- Tono del amarillo y del verde tenue en el WS2812 del kit, y que el brillo sea adecuado de noche.
- RSSI del colgante con la antena externa de la base.
