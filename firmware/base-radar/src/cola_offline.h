// cola_offline.h
// Cola de lecturas y eventos que no se pudieron enviar a Firebase (sin WiFi o Firebase caído).
// Se vacía en orden cuando vuelve la conexión.

#pragma once

#include <stddef.h>

#include "radar.h"

namespace cola_offline {

// TODO: decidir capacidad y almacenamiento. Opción inicial: buffer circular en RAM; si se llena,
// se descarta la lectura más antigua. Los eventos no se descartan.
void iniciar();

bool encolarLectura(const LecturaRadar &lectura);

bool hayPendientes();

// Devuelve la lectura más antigua sin sacarla de la cola.
bool primera(LecturaRadar &lectura);

// Saca la lectura más antigua, después de confirmar que Firebase la aceptó.
void descartarPrimera();

size_t tamano();

}  // namespace cola_offline
