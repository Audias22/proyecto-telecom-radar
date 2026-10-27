#include "watchdog_tareas.h"

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "registro.h"

namespace watchdog_tareas {

void iniciar() {
    const esp_task_wdt_config_t configuracion = {
        WDT_TAREAS_TIMEOUT_MS,  // timeout_ms
        0,                      // idle_core_mask: no se vigila la tarea idle
        true,                   // trigger_panic: reiniciar
    };
    if (esp_task_wdt_reconfigure(&configuracion) != ESP_OK) {
        registrar("watchdog", "ERROR: no se pudo configurar el watchdog de tareas");
        return;
    }
    enableLoopWDT();
    registrar("watchdog", "watchdog de tareas activo sobre loop(), %lu s",
              static_cast<unsigned long>(WDT_TAREAS_TIMEOUT_MS / 1000));
}

void suscribirEstaTarea() {
    if (esp_task_wdt_add(nullptr) != ESP_OK) {
        registrar("watchdog", "ERROR: no se pudo suscribir la tarea %s", pcTaskGetName(nullptr));
    }
}

void alimentar() {
    esp_task_wdt_reset();
}

void pausarEstaTarea() {
    esp_task_wdt_delete(nullptr);
}

void reanudarEstaTarea() {
    esp_task_wdt_add(nullptr);
    esp_task_wdt_reset();
}

}  // namespace watchdog_tareas
