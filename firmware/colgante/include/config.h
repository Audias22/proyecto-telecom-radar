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

// Tiempos del boton. La espera de liberacion siempre termina al alcanzar el limite,
// aunque el boton permanezca presionado o el contacto siga rebotando.
#define BOTON_ANTIRREBOTE_MS          30UL
#define BOTON_ESPERA_LIBERACION_MS  5000UL

// Buzzer activo en nivel alto.
#define BUZZER_CONFIRMACION_MS       120UL
#define BUZZER_ERROR_MS              500UL
#define BUZZER_PAUSA_ERROR_MS        250UL
#define BUZZER_ERROR_REPETICIONES      3U

// Si el boton sigue bajo al dormir, se usa temporalmente solo el timer. Este intervalo corto
// permite comprobar de nuevo la liberacion sin entrar en un ciclo de wake inmediato.
#define BOTON_REVISION_ATASCADO_S      5U
#define BOTON_REVISION_LECTURA_MS    100UL

// Evita que un error persistente de configuracion provoque reintentos en un bucle caliente.
#define ENERGIA_ERROR_REINTENTO_MS  1000UL
