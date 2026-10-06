// config.h
// Configuración del colgante que no es secreta: versión y pines.

#pragma once

#define FIRMWARE_NOMBRE  "colgante"
#define FIRMWARE_VERSION "0.1.0"

// Pines propuestos. Por confirmar con el módulo y el circuito armado.
// - El botón debe estar en GPIO0-GPIO5: son los únicos que despiertan al ESP32-C3 de deep sleep.
//   Se evita GPIO2 porque es pin de arranque (strapping).
// - La medición de batería necesita un pin con ADC1 (GPIO0-GPIO4).
#define PIN_BOTON        3
#define PIN_BATERIA_ADC  1
#define PIN_BUZZER       10
