#include "motivo_reinicio.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>

#include <cstring>

namespace motivo_reinicio {

namespace {

// Motivo del último reinicio del vigilante. RTC_NOINIT sobrevive a esp_restart(), no a un corte
// de alimentación.
constexpr uint32_t MARCA_VIGILANTE = 0x56524544;  // "VRED"
RTC_NOINIT_ATTR uint32_t marca;
RTC_NOINIT_ATTR char motivo_vigilante[112];

struct Descripcion {
    esp_reset_reason_t razon;
    const char        *texto;
    bool               inesperado;
};

const Descripcion DESCRIPCIONES[] = {
    {ESP_RST_POWERON, "encendido", false},
    {ESP_RST_EXT, "boton RST", false},
    {ESP_RST_USB, "USB (carga del firmware o monitor serial)", false},
    {ESP_RST_JTAG, "JTAG", false},
    {ESP_RST_DEEPSLEEP, "salida de deep sleep", false},
    {ESP_RST_SW, "reinicio por software", true},
    {ESP_RST_PANIC, "fallo del programa (panic)", true},
    {ESP_RST_INT_WDT, "watchdog de interrupciones", true},
    {ESP_RST_TASK_WDT, "watchdog de tareas", true},
    {ESP_RST_WDT, "watchdog del sistema", true},
    {ESP_RST_BROWNOUT, "caida de voltaje (brownout)", true},
    {ESP_RST_PWR_GLITCH, "falla de alimentacion", true},
    {ESP_RST_CPU_LOCKUP, "CPU bloqueada", true},
    {ESP_RST_EFUSE, "error de eFuse", true},
    {ESP_RST_SDIO, "SDIO", true},
};

}  // namespace

MotivoReinicio leer() {
    const esp_reset_reason_t razon = esp_reset_reason();
    MotivoReinicio m = {};
    snprintf(m.texto, sizeof(m.texto), "motivo desconocido (%d)", static_cast<int>(razon));
    m.inesperado = true;
    for (const Descripcion &d : DESCRIPCIONES) {
        if (d.razon == razon) {
            strlcpy(m.texto, d.texto, sizeof(m.texto));
            m.inesperado = d.inesperado;
        }
    }
    if (razon == ESP_RST_SW && marca == MARCA_VIGILANTE) {
        motivo_vigilante[sizeof(motivo_vigilante) - 1] = '\0';
        snprintf(m.texto, sizeof(m.texto), "vigilante de la red: %s", motivo_vigilante);
    }
    marca = 0;
    return m;
}

void anotarVigilante(const char *motivo) {
    strlcpy(motivo_vigilante, motivo, sizeof(motivo_vigilante));
    marca = MARCA_VIGILANTE;
}

}  // namespace motivo_reinicio
