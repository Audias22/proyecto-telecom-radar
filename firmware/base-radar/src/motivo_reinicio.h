// motivo_reinicio.h
// Por qué arrancó la base (esp_reset_reason()) y si fue inesperado (docs/fallos.md, escenario 10).
//
// Inesperados, y se avisan por Telegram: watchdog de tareas o de interrupciones, panic, caída de
// voltaje (brownout), falla de alimentación, CPU bloqueada y el reinicio del vigilante de la red
// (su motivo queda en memoria RTC, que sobrevive a esp_restart()).
// Normales, solo por serial: encendido, botón RST, USB (carga del firmware o el monitor serial
// con DTR/RTS), JTAG y salida de deep sleep. Un corte de luz es un encendido normal: lo cubre el
// evento BASE_DESCONECTADA si duró 3 min o más.

#pragma once

struct MotivoReinicio {
    char texto[136];  // "watchdog de tareas", "vigilante de la red: PATCH ... lleva 75 s ..."
    bool inesperado;
};

namespace motivo_reinicio {

// En setup(), una vez: lee el motivo y borra la marca del vigilante.
MotivoReinicio leer();

// Antes de esp_restart() del vigilante: deja el motivo para el arranque siguiente.
void anotarVigilante(const char *motivo);

}  // namespace motivo_reinicio
