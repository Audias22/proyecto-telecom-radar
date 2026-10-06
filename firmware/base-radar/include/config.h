// config.h
// Configuración de la base que no es secreta: versión, pines y tiempos.

#pragma once

#define FIRMWARE_NOMBRE  "base-radar"
#define FIRMWARE_VERSION "0.2.0"

// Pines del buzzer y LED de alerta local.
// Según el mapa de pines de Seeed para el kit (README de la librería, v1.1.0-rc1), el kit ocupa:
//   GPIO16/17 (D6/D7) UART del radar, GPIO1 (D1) LED RGB WS2812, GPIO22/23 (SDA/SCL) BH1750.
// Quedan libres las dos líneas del puerto Grove: D0 (GPIO0) y D10 (GPIO18).
// La UART se confirmó con el kit; los pines del Grove faltan por probar con el buzzer conectado.
// Para el LED de alerta se puede usar el WS2812 del kit (D1) en lugar de un LED externo.
#define PIN_BUZZER  0   // D0, Grove SIG1
#define PIN_LED     18  // D10, Grove SIG2 (o el WS2812 en D1)

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

// Periodo de envío de lecturas a Firebase.
#define LECTURA_INTERVALO_MS  5000

// Periodo de actualización de /dispositivos/{baseId}/estado.
#define ESTADO_INTERVALO_MS   60000
