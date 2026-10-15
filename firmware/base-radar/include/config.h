// config.h
// Configuración de la base que no es secreta: versión, pines y tiempos.

#pragma once

#define FIRMWARE_NOMBRE  "base-radar"
#define FIRMWARE_VERSION "0.4.0"

// ---------------------------------------------------------------------------
// Salida por serial
// ---------------------------------------------------------------------------

// 1: una línea CSV por lectura del radar, para analisis/capturar_serial.py (formato en main.cpp).
// 0: solo registros legibles (mensajes del colgante y cambios de estado del radar).
// En los dos modos los registros empiezan con '#', así el script de captura los ignora.
#define BASE_SALIDA_CSV  0

// ---------------------------------------------------------------------------
// Antena
// ---------------------------------------------------------------------------

// El XIAO ESP32-C6 del kit tiene una antena externa conectada por U.FL (confirmado en el kit).
// La placa tiene un conmutador de RF: GPIO3 (WIFI_ENABLE) en bajo lo habilita y GPIO14
// (WIFI_ANT_CONFIG) elige la antena: bajo = antena de chip, alto = conector U.FL.
// initVariant() de arduino-esp32 3.3.7 (variants/XIAO_ESP32C6/variant.cpp) deja GPIO14 en bajo,
// así que hay que ponerlo en alto antes de iniciar WiFi.
// 1: antena externa (U.FL). 0: antena de chip de la placa.
#define BASE_ANTENA_EXTERNA  1

// ---------------------------------------------------------------------------
// Pines
// ---------------------------------------------------------------------------

// Tiras hembra de 2.54 mm de la placa del kit (rotuladas en la placa):
//   izquierda: TXD, D8, D9, D10, 3V3, GND, 5V
//   derecha:   RXD, SCL, SDA, RST, D2, D1, D0
// Grove: GND, VCC, D10, D0.
// Equivalencias del XIAO ESP32-C6 (pins_arduino.h de la variante):
//   D0 = GPIO0, D1 = GPIO1, D2 = GPIO2, D8 = GPIO19, D9 = GPIO20, D10 = GPIO18,
//   TXD/RXD = D6/D7 = GPIO16/GPIO17 (UART del radar), SDA/SCL = D4/D5 = GPIO22/GPIO23 (BH1750).
// El kit ocupa: UART del radar, I2C del BH1750 y D1 (LED RGB WS2812 de la placa).

// Buzzer de la base: módulo de buzzer activo sin transistor (negro, tipo KY-012).
// S a D10 (GPIO18), "-" a GND. Suena con el pin en alto y la corriente sale directo del GPIO.
#define PIN_BUZZER              18  // D10, también disponible en el Grove
#define BASE_BUZZER_HABILITADO  1   // 0: el buzzer no suena nunca (el LED sí)

// Fuerza de salida del GPIO del buzzer. Como el módulo no tiene transistor, el pin entrega toda
// la corriente del buzzer. Con la fuerza por defecto (GPIO_DRIVE_CAP_2, ~20 mA) la resistencia
// interna del pin es mayor y el voltaje en D10 cae más bajo carga: el buzzer suena más bajo o no
// arranca. GPIO_DRIVE_CAP_3 (~40 mA en el ESP32-C6) es la de menor resistencia de salida.
// No sube la corriente por sí misma: el buzzer toma lo que pide, pero con menos caída en el pin.
// La corriente del buzzer no se pudo medir (fusible de mA del multímetro quemado); la prueba de
// voltaje con el comando "b" (README) confirma si el pin aguanta la carga: 3.0 V o más en D10.
// Si da menos, poner un transistor entre el pin y el buzzer.
#define BUZZER_FUERZA_GPIO  GPIO_DRIVE_CAP_3

// LED RGB WS2812 de la placa del kit. Se escribe con rgbLedWrite() de arduino-esp32 3.x.
#define PIN_LED  1  // D1

// ---------------------------------------------------------------------------
// Alerta local (LED y buzzer)
// ---------------------------------------------------------------------------

// Duración de la alerta local por una ALERTA nueva del colgante: LED rojo y buzzer intermitentes.
#define ALERTA_LOCAL_DURACION_MS  60000UL
// Duración de un aviso (LED amarillo, sin buzzer): batería baja del colgante, radar sin datos.
#define AVISO_LOCAL_DURACION_MS   10000UL

// Medio periodo del parpadeo (encendido y apagado) de cada patrón.
#define ALERTA_PARPADEO_MS  250UL
#define AVISO_PARPADEO_MS   500UL

// Brillo del LED (0-255 por canal). El WS2812 a 255 deslumbra en un dormitorio de noche.
#define LED_BRILLO_ALERTA   64  // rojo de alerta y amarillo de aviso
#define LED_BRILLO_REPOSO   6   // verde tenue: "en funcionamiento"

// Duración del comando "b" del monitor serial: buzzer fijo para medir el voltaje en D10.
#define PRUEBA_BUZZER_MS  10000UL

// Botón para silenciar la alerta local: el botón BOOT del XIAO ESP32-C6.
// Confirmado en el esquemático oficial (XIAO_ESP32_C6_v1.0_SCH_260114.pdf, hoja 4): la red BOOT
// va a GPIO9 (pin 15 del ESP32-C6), con R17 de 10 kΩ a 3V3 y el pulsador K2 a GND, así que
// presionado se lee en bajo. La wiki de Seeed también lo lista como "Boot: GPIO9".
// GPIO9 es pin de arranque: si está presionado al reiniciar, el chip entra en modo de carga
// (bootloader) y el firmware no corre. Solo se lee después del arranque.
#define PIN_BOTON_SILENCIO  9
// Tiempo que el nivel debe quedar estable para aceptar la pulsación.
#define BOTON_ANTIRREBOTE_MS  30UL

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------

// 1: la base se conecta al router (WIFI_SSID y WIFI_CLAVE de secrets.h). El canal de ESP-NOW es
//    el del router; el colgante lo encuentra con su barrido (docs/protocolo.md, sección 6).
// 0: fase local, sin WiFi: canal fijo BASE_CANAL_SIN_WIFI y no hace falta secrets.h.
#define BASE_WIFI_HABILITADO  1

// Reconexión propia con espera creciente (docs/fallos.md, "Reconexión WiFi y ESP-NOW"): tras
// perder la conexión se espera WIFI_ESPERA_INICIAL_MS, y cada intento fallido duplica la espera
// hasta WIFI_ESPERA_MAXIMA_MS (5 s, 10 s, 20 s, 40 s, 80 s, 120 s, 120 s...). Mientras busca el
// router la radio cambia de canal; entre intentos queda quieta y el colgante la puede encontrar.
#define WIFI_ESPERA_INICIAL_MS  5000UL
#define WIFI_ESPERA_MAXIMA_MS   120000UL
// Un intento que no termina (ni conecta ni falla) en este tiempo se corta y cuenta como fallido.
#define WIFI_INTENTO_MAX_MS     20000UL
// Cada cuánto se lee el canal de la radio para registrar sus cambios.
#define WIFI_CANAL_MUESTREO_MS  100UL

// ---------------------------------------------------------------------------
// ESP-NOW
// ---------------------------------------------------------------------------

// Canal de radio con BASE_WIFI_HABILITADO 0 (fase local).
// Es CANAL_POR_DEFECTO del colgante, el primero que prueba tras encender.
// Con WiFi el canal lo fija el router y este valor no se usa (docs/protocolo.md, sección 6).
#define BASE_CANAL_SIN_WIFI  1

// ---------------------------------------------------------------------------
// Radar
// ---------------------------------------------------------------------------

// Radar MR60BHA2: UART0 del ESP32-C6 (RX = GPIO17/D7, TX = GPIO16/D6), 115200 baudios, que son
// los valores por defecto de la librería Seeed Arduino mmWave y de su ejemplo.
#define RADAR_UART_NUM  0

// Si no llega ninguna trama válida del radar en este tiempo, se considera "sin datos" y se
// reinicia la UART. Se vuelve a intentar cada RADAR_SIN_DATOS_MS mientras siga sin datos.
#define RADAR_SIN_DATOS_MS  5000

// Rangos aceptados. Una lectura con algún valor fuera de rango se marca como no válida.
#define RADAR_RESP_MIN_RPM     0.0f
#define RADAR_RESP_MAX_RPM     60.0f
#define RADAR_LATIDO_MIN_BPM   30.0f
#define RADAR_LATIDO_MAX_BPM   200.0f
#define RADAR_DISTANCIA_MIN_CM 0.0f
#define RADAR_DISTANCIA_MAX_CM 300.0f

// Criterio de "persona detectada" (docs/modelo-datos.md, 1.1; docs/radar.md, 13.6): mediana de
// |fase_total| sin las tramas en 0, en una ventana deslizante de 30 s, umbral 0.03.
#define RADAR_VENTANA_ACTIVIDAD_MS  30000UL
#define RADAR_UMBRAL_PERSONA        0.03f
// Muestras guardadas para la ventana. Con objetivo llegan ~16.2 tramas de fase por segundo
// (docs/radar.md, 4 y 12.1): 30 s son ~490. 640 deja ~30 % de margen (21 tramas/s). Si llegaran
// más, se pisan las más antiguas y la ventana queda algo más corta. Memoria: 640 x 8 B de muestras
// + 640 x 4 B de copia para la mediana = 7.5 kB.
#define RADAR_CAPACIDAD_VENTANA     640

// ---------------------------------------------------------------------------
// Nube: Firebase Realtime Database (docs/modelo-datos.md)
// ---------------------------------------------------------------------------

// La base escribe en Firebase solo con WiFi. Credenciales en secrets.h (FIREBASE_URL,
// FIREBASE_API_KEY, DISPOSITIVO_EMAIL, DISPOSITIVO_CLAVE, BASE_ID).
#define FIREBASE_HABILITADO  BASE_WIFI_HABILITADO

// Periodo de envío de lecturas a /lecturas/{baseId}/{ts}.
#define LECTURA_INTERVALO_MS  5000UL

// Periodo de actualización de /dispositivos/{baseId}/estado. Tras cada envío se lee de vuelta
// ultimo_contacto como referencia de hora del servidor (docs/modelo-datos.md, sección 2).
#define ESTADO_INTERVALO_MS   60000UL

// El idToken de Firebase Auth dura 1 h; se renueva con el refresh token 5 min antes.
#define FIREBASE_MARGEN_RENOVACION_MS  300000UL

// Tras un error de red o de servidor se espera antes de reintentar: 5 s y el doble en cada fallo
// seguido, hasta 60 s. Un inicio de sesión rechazado (clave mala) espera el máximo.
#define FIREBASE_ESPERA_ERROR_MS         5000UL
#define FIREBASE_ESPERA_ERROR_MAXIMA_MS  60000UL

// Tiempos de espera de cada petición HTTPS (la tarea de red espera; loop() nunca).
#define FIREBASE_TIMEOUT_CONEXION_MS  10000
#define FIREBASE_TIMEOUT_RESPUESTA_MS 10000

// Una lectura que sale más de este tiempo después de medirse se marca diferida: true.
#define LECTURA_DIFERIDA_MS  (2 * LECTURA_INTERVALO_MS)

// Cola de peticiones de loop() a la tarea de red (src/cola_peticiones.h). Acotada:
// - lecturas: 24 (2 min a una cada 5 s). Llena, se descarta la lectura más antigua.
// - eventos: 32, aparte. Una lectura nunca desplaza a un evento.
// - colgantes: una entrada por id (8 ids a la vez); un mensaje nuevo reemplaza al pendiente del
//   mismo colgante, porque solo importa el último valor.
// La cola offline completa (lecturas de horas sin red) es la parte 2 (src/cola_offline.h).
#define COLA_LECTURAS_CAPACIDAD   24
#define COLA_EVENTOS_CAPACIDAD    32
#define COLA_COLGANTES_CAPACIDAD  8

// Tarea FreeRTOS de la red (HTTPS). Misma prioridad que loopTask (1): el ESP32-C6 tiene un solo
// núcleo y, con la misma prioridad, FreeRTOS reparte la CPU por turnos de 1 ms, así que el TLS
// nunca deja a loop() sin correr. Pila: el handshake de mbedTLS usa unos 6-8 kB.
#define TAREA_RED_PRIORIDAD  1
#define TAREA_RED_PILA       12288
