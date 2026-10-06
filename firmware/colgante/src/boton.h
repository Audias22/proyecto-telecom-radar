// boton.h
// Lectura del botón de auxilio y causa del despertar.

#pragma once

namespace boton {

// TODO: configurar PIN_BOTON como entrada. Por confirmar con el módulo si basta el pull-up
// interno durante el deep sleep o hace falta una resistencia externa.
void iniciar();

// TODO: true si el despertar fue por el botón (y no por el timer del heartbeat).
bool despertoPorBoton();

bool presionado();

// TODO: esperar a que se suelte, con límite de tiempo, para no volver a despertar en bucle.
void esperarSoltar(unsigned long limite_ms);

}  // namespace boton
