// config.h
// Configuración de la base que no es secreta: versión, pines y tiempos.

#pragma once

#define FIRMWARE_NOMBRE  "base-radar"
#define FIRMWARE_VERSION "0.3.0"

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

// ---------------------------------------------------------------------------
// ESP-NOW
// ---------------------------------------------------------------------------

// Canal de radio mientras la base no se conecta a WiFi (fase local, sin red_wifi).
// Es CANAL_POR_DEFECTO del colgante, el primero que prueba tras encender.
// Con WiFi conectado el canal lo fija el router y este valor deja de usarse (docs/protocolo.md,
// sección 6): el colgante lo encuentra con el barrido.
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

// ---------------------------------------------------------------------------
// Nube (todavía sin usar en la fase local)
// ---------------------------------------------------------------------------

// Periodo de envío de lecturas a Firebase.
#define LECTURA_INTERVALO_MS  5000

// Periodo de actualización de /dispositivos/{baseId}/estado.
#define ESTADO_INTERVALO_MS   60000
