// boton.h
// Lectura del botón de auxilio y causa del despertar.

#pragma once

#include "config.h"

namespace boton {

// Configura el boton activo en bajo con el pull-up interno.
void iniciar();

// True solamente si PIN_BOTON fue el GPIO que causo el ultimo despertar.
bool despertoPorBoton();

// Devuelve el estado estable actual. No representa una pulsacion nueva en cada llamada.
bool presionado();

// Espera una liberacion estable sin superar limite_ms. Si se omite el argumento usa el limite
// configurado. Devuelve false si el boton sigue presionado al agotar el tiempo; en ese caso no se
// debe habilitar inmediatamente el despertar en nivel bajo porque provocaria un bucle.
bool esperarSoltar(unsigned long limite_ms = BOTON_ESPERA_LIBERACION_MS);

}  // namespace boton
