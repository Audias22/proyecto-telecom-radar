// radar.h
// Lectura del radar MR60BHA2 por UART: respiración, latido, presencia y distancia.

#pragma once

#include <stdint.h>

struct LecturaRadar {
    float    resp_rpm;          // respiraciones por minuto
    float    latido_bpm;        // latidos por minuto (dato secundario, menos confiable)
    bool     presencia;         // hay una persona en el cono del radar
    uint16_t distancia_cm;      // distancia a la persona
    bool     valida;            // false si el radar no respondió o los valores están fuera de rango
    uint32_t millis_medicion;   // millis() al momento de la lectura
};

namespace radar {

// TODO: configurar la UART del radar. Por confirmar con el módulo: pines, baudios y si se usa
// la librería Seeed_Arduino_mmWave o se decodifican las tramas a mano.
void iniciar();

// TODO: leer y decodificar las tramas pendientes. Llamar en cada vuelta de loop().
void actualizar();

// TODO: indica si llegó una lectura nueva desde la última llamada a ultimaLectura().
bool hayLecturaNueva();

LecturaRadar ultimaLectura();

}  // namespace radar
