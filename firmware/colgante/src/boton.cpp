#include <Arduino.h>
#include <esp_sleep.h>

#include "boton.h"

namespace {

static_assert(PIN_BOTON >= 0 && PIN_BOTON <= 5,
              "PIN_BOTON debe permitir despertar al ESP32-C3 desde deep sleep");

bool iniciado = false;

bool nivelPresionado() {
    return digitalRead(PIN_BOTON) == LOW;
}

void asegurarInicio() {
    if (!iniciado) {
        boton::iniciar();
    }
}

}  // namespace

namespace boton {

void iniciar() {
    pinMode(PIN_BOTON, INPUT_PULLUP);
    iniciado = true;
}

bool despertoPorBoton() {
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_GPIO) {
        return false;
    }

    const uint64_t gpio_despertador = esp_sleep_get_gpio_wakeup_status();
    return (gpio_despertador & (1ULL << PIN_BOTON)) != 0;
}

bool presionado() {
    asegurarInicio();

    if (!nivelPresionado()) {
        return false;
    }

    const unsigned long inicio = millis();
    while (millis() - inicio < BOTON_ANTIRREBOTE_MS) {
        if (!nivelPresionado()) {
            return false;
        }
        delay(1);
    }

    return nivelPresionado();
}

bool esperarSoltar(unsigned long limite_ms) {
    asegurarInicio();

    const unsigned long inicio = millis();
    unsigned long inicio_liberacion = 0;
    bool liberacion_en_curso = false;

    while (millis() - inicio < limite_ms) {
        if (nivelPresionado()) {
            liberacion_en_curso = false;
        } else if (!liberacion_en_curso) {
            liberacion_en_curso = true;
            inicio_liberacion = millis();

            if (BOTON_ANTIRREBOTE_MS == 0) {
                return true;
            }
        } else if (millis() - inicio_liberacion >= BOTON_ANTIRREBOTE_MS) {
            return true;
        }

        delay(1);
    }

    return false;
}

}  // namespace boton
