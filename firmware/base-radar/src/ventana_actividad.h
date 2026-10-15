// ventana_actividad.h
// Criterio de "persona detectada" (docs/modelo-datos.md, 1.1; docs/radar.md, 13.6):
//   actividad = mediana de |fase_total| de las tramas de fase con dato (sin las que vienen en 0)
//               en una ventana deslizante de RADAR_VENTANA_ACTIVIDAD_MS (30 s);
//   objetivo  = hubo al menos una trama de fase con dato en esa ventana;
//   persona   = objetivo y actividad >= RADAR_UMBRAL_PERSONA (0.03).
//
// Lógica pura, sin Arduino: la usan radar.cpp y las pruebas host (pruebas_host/).
// Memoria fija: RADAR_CAPACIDAD_VENTANA muestras. Con más tramas con dato que eso en 30 s, se
// pisan las más antiguas y la ventana queda más corta (ver config.h).

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"

struct EstadoPersona {
    bool     objetivo;   // hubo tramas de fase con dato en la ventana
    bool     persona;    // actividad >= RADAR_UMBRAL_PERSONA
    float    actividad;  // mediana de |fase_total|; 0 sin objetivo
    uint16_t muestras;   // tramas con dato en la ventana
};

class VentanaActividad {
public:
    // Agrega una trama de fase. Las que traen fase_total = 0 (o NaN) no cuentan.
    void agregar(uint32_t ms, float fase_total);

    // Evalúa el criterio sobre las tramas de (ahora_ms - ventana, ahora_ms]. Descarta las viejas.
    EstadoPersona evaluar(uint32_t ahora_ms);

    void vaciar();

private:
    struct Muestra {
        uint32_t ms;
        float    valor;  // |fase_total|
    };

    void descartarViejas(uint32_t ahora_ms);
    float mediana();

    Muestra muestras_[RADAR_CAPACIDAD_VENTANA] = {};
    float   auxiliar_[RADAR_CAPACIDAD_VENTANA] = {};  // copia para la mediana
    size_t  inicio_ = 0;
    size_t  cantidad_ = 0;
};
