#include <Arduino.h>
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_sleep.h>
#include <protocolo.h>

#include "boton.h"
#include "config.h"
#include "energia.h"

namespace {

constexpr uint64_t MICROSEGUNDOS_POR_SEGUNDO = 1000000ULL;
constexpr uint64_t MASCARA_BOTON = 1ULL << PIN_BOTON;

static_assert(PIN_BUZZER != PIN_BOTON, "El buzzer y el boton no pueden compartir GPIO");
static_assert(HEARTBEAT_INTERVALO_S > 0, "El intervalo de heartbeat debe ser mayor que cero");
static_assert(BOTON_REVISION_ATASCADO_S > 0,
              "El intervalo de revision del boton debe ser mayor que cero");
static_assert(BOTON_REVISION_LECTURA_MS > BOTON_ANTIRREBOTE_MS,
              "La revision debe permitir confirmar una liberacion estable");
static_assert(BUZZER_ERROR_REPETICIONES > 0, "El patron de error necesita al menos un sonido");

RTC_DATA_ATTR bool recuperacion_boton_atascado = false;

void prepararBuzzer() {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
}

void emitirSonido(unsigned long duracion_ms) {
    prepararBuzzer();
    digitalWrite(PIN_BUZZER, HIGH);
    delay(duracion_ms);
    digitalWrite(PIN_BUZZER, LOW);
}

void informarError(const char* operacion, esp_err_t error) {
    Serial.printf("energia: fallo al %s: %s (%d)\n", operacion, esp_err_to_name(error), error);
}

void esperarTrasError() {
    delay(ENERGIA_ERROR_REINTENTO_MS);
}

bool limpiarFuentesDespertar() {
    const esp_err_t error = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (error == ESP_OK || error == ESP_ERR_INVALID_STATE) {
        return true;
    }

    informarError("limpiar fuentes de despertar", error);
    return false;
}

bool habilitarTemporizador(uint32_t intervalo_s) {
    const uint64_t intervalo_us = static_cast<uint64_t>(intervalo_s) * MICROSEGUNDOS_POR_SEGUNDO;
    const esp_err_t error = esp_sleep_enable_timer_wakeup(intervalo_us);
    if (error == ESP_OK) {
        return true;
    }

    informarError("habilitar el temporizador", error);
    return false;
}

bool habilitarBoton() {
    const esp_err_t error =
        esp_deep_sleep_enable_gpio_wakeup(MASCARA_BOTON, ESP_GPIO_WAKEUP_GPIO_LOW);
    if (error == ESP_OK) {
        return true;
    }

    informarError("habilitar el boton como fuente de despertar", error);
    return false;
}

}  // namespace

namespace energia {

void beepConfirmacion() {
    emitirSonido(BUZZER_CONFIRMACION_MS);
}

void beepError() {
    for (unsigned int intento = 0; intento < BUZZER_ERROR_REPETICIONES; ++intento) {
        emitirSonido(BUZZER_ERROR_MS);
        if (intento + 1 < BUZZER_ERROR_REPETICIONES) {
            delay(BUZZER_PAUSA_ERROR_MS);
        }
    }
}

bool recuperandoBotonAtascado() {
    if (!recuperacion_boton_atascado) {
        return false;
    }

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        return true;
    }

    recuperacion_boton_atascado = false;
    return false;
}

void dormir() {
    prepararBuzzer();

    const bool en_recuperacion = recuperandoBotonAtascado();
    const unsigned long espera_liberacion_ms =
        en_recuperacion ? BOTON_REVISION_LECTURA_MS : BOTON_ESPERA_LIBERACION_MS;
    const bool boton_liberado = boton::esperarSoltar(espera_liberacion_ms);
    const uint32_t intervalo_s =
        boton_liberado ? HEARTBEAT_INTERVALO_S : BOTON_REVISION_ATASCADO_S;

    if (!limpiarFuentesDespertar() || !habilitarTemporizador(intervalo_s)) {
        esperarTrasError();
        return;
    }

    if (boton_liberado && !habilitarBoton()) {
        esperarTrasError();
        return;
    }

    recuperacion_boton_atascado = !boton_liberado;
    if (!boton_liberado) {
        Serial.println("energia: boton presionado; se reintentara la liberacion por timer");
    }

    esp_deep_sleep_start();
}

}  // namespace energia
