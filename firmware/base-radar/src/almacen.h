// almacen.h
// Datos que sobreviven a un reinicio, en la partición NVS (espacio de nombres "base"):
//   - "eventos":    eventos pendientes de Firebase y Telegram (cola_offline.h). Los escribe la
//                   tarea de red cuando cambian.
//   - "detectores": colgantes conocidos, batería avisada, ausencias y radar sin datos
//                   (detectores.h). loop() los deja con pedirEscritura() y la tarea de red los
//                   escribe, así loop() no espera a la flash.
// Son pocos bytes y cambian solo con un evento, así que el desgaste de la flash es despreciable.
// Funciones seguras desde cualquier tarea (un mutex propio).

#pragma once

#include <stddef.h>

namespace almacen {

// En setup(), antes de leer. false si NVS no está disponible (se sigue sin guardar).
bool iniciar();

// Devuelve los bytes leídos (0 si no hay datos o no caben en tamano).
size_t leer(const char *clave, void *datos, size_t tamano);

// Escribe ahora (bloquea a quien llama lo que tarde la flash). largo 0 borra la clave.
bool escribir(const char *clave, const void *datos, size_t largo);

// Copia los datos para escribirlos después con escribirPendientes() (hasta 64 bytes).
void pedirEscritura(const char *clave, const void *datos, size_t largo);
void escribirPendientes();

}  // namespace almacen
