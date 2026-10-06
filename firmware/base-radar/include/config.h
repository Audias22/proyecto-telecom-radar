// config.h
// Configuración de la base que no es secreta: versión, pines y tiempos.

#pragma once

#define FIRMWARE_NOMBRE  "base-radar"
#define FIRMWARE_VERSION "0.1.0"

// Pines del buzzer y LED de alerta local.
// Por confirmar con el módulo: qué pines del XIAO quedan libres en el kit MR60BHA2
// (el radar usa la UART y el kit trae sensor de luz y LED propios). -1 = sin asignar.
#define PIN_BUZZER  -1
#define PIN_LED     -1

// Periodo de envío de lecturas a Firebase.
#define LECTURA_INTERVALO_MS  5000

// Periodo de actualización de /dispositivos/{baseId}/estado.
#define ESTADO_INTERVALO_MS   60000
