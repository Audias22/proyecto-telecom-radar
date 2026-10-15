#include "boton_silencio.h"

#include <Arduino.h>

#include "config.h"

namespace boton_silencio {

namespace {

bool nivel_leido = false;    // última lectura cruda (true = presionado)
bool nivel_estable = false;  // nivel aceptado tras el antirrebote
uint32_t cambio_ms = 0;      // millis() del último cambio de la lectura cruda

bool leerPresionado() {
    return digitalRead(PIN_BOTON_SILENCIO) == LOW;  // botón a GND
}

}  // namespace

void iniciar() {
    pinMode(PIN_BOTON_SILENCIO, INPUT_PULLUP);
    nivel_leido = leerPresionado();
    nivel_estable = nivel_leido;  // si ya estaba presionado, no cuenta como pulsación
    cambio_ms = millis();
}

bool pulsado() {
    const uint32_t ahora = millis();
    const bool presionado = leerPresionado();
    if (presionado != nivel_leido) {
        nivel_leido = presionado;
        cambio_ms = ahora;
        return false;
    }
    if (presionado == nivel_estable || ahora - cambio_ms < BOTON_ANTIRREBOTE_MS) {
        return false;
    }
    nivel_estable = presionado;
    return presionado;
}

}  // namespace boton_silencio
