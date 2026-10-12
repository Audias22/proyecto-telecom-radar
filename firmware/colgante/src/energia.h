// energia.h
// Deep sleep y fuentes de despertar del colgante. También maneja el buzzer de confirmación,
// porque es lo último que pasa antes de dormir.

#pragma once

namespace energia {

// Beep corto de confirmacion (alerta recibida por la base).
void beepConfirmacion();

// Tres beeps largos (la base no respondio en ningun canal).
void beepError();

// True si el ultimo deep sleep se configuro como recuperacion de un boton que seguia presionado
// y este arranque fue causado por su timer. Limpia estados RTC obsoletos tras otros reinicios.
// El futuro flujo principal debe omitir radio y sonidos en ese wake y volver a llamar dormir().
bool recuperandoBotonAtascado();

// Espera de forma limitada la liberacion, configura las fuentes de wake y entra en deep sleep.
// Normalmente no regresa; si alguna API de configuracion falla, informa por Serial y retorna.
void dormir();

}  // namespace energia
