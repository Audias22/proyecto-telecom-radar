// energia.h
// Deep sleep y fuentes de despertar del colgante. También maneja el buzzer de confirmación,
// porque es lo último que pasa antes de dormir.

#pragma once

namespace energia {

// TODO: beep corto de confirmación (alerta recibida por la base).
void beepConfirmacion();

// TODO: tres beeps largos (la base no respondió en ningún canal).
void beepError();

// TODO: habilitar despertar por PIN_BOTON en nivel bajo (esp_deep_sleep_enable_gpio_wakeup) y
// por timer cada HEARTBEAT_INTERVALO_S, y entrar en deep sleep. No regresa.
void dormir();

}  // namespace energia
