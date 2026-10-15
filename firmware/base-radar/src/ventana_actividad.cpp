#include "ventana_actividad.h"

#include <algorithm>
#include <cmath>

void VentanaActividad::agregar(uint32_t ms, float fase_total) {
    if (fase_total == 0.0f || std::isnan(fase_total)) {
        return;  // los ceros exactos vienen del radar y no son dato (docs/radar.md, 12.1)
    }
    if (cantidad_ == RADAR_CAPACIDAD_VENTANA) {
        inicio_ = (inicio_ + 1) % RADAR_CAPACIDAD_VENTANA;  // se pisa la más antigua
        cantidad_--;
    }
    muestras_[(inicio_ + cantidad_) % RADAR_CAPACIDAD_VENTANA] = {ms, std::fabs(fase_total)};
    cantidad_++;
}

EstadoPersona VentanaActividad::evaluar(uint32_t ahora_ms) {
    descartarViejas(ahora_ms);
    EstadoPersona estado{};
    estado.muestras = static_cast<uint16_t>(cantidad_);
    estado.objetivo = cantidad_ > 0;
    if (estado.objetivo) {
        estado.actividad = mediana();
        estado.persona = estado.actividad >= RADAR_UMBRAL_PERSONA;
    }
    return estado;
}

void VentanaActividad::vaciar() {
    inicio_ = 0;
    cantidad_ = 0;
}

// La resta en uint32_t sigue funcionando cuando millis() da la vuelta (cada ~49 días).
void VentanaActividad::descartarViejas(uint32_t ahora_ms) {
    while (cantidad_ > 0 && ahora_ms - muestras_[inicio_].ms >= RADAR_VENTANA_ACTIVIDAD_MS) {
        inicio_ = (inicio_ + 1) % RADAR_CAPACIDAD_VENTANA;
        cantidad_--;
    }
}

// Igual que numpy.median: con una cantidad par, el promedio de los dos del medio.
float VentanaActividad::mediana() {
    for (size_t i = 0; i < cantidad_; i++) {
        auxiliar_[i] = muestras_[(inicio_ + i) % RADAR_CAPACIDAD_VENTANA].valor;
    }
    float *fin = auxiliar_ + cantidad_;
    float *medio = auxiliar_ + cantidad_ / 2;
    std::nth_element(auxiliar_, medio, fin);
    if (cantidad_ % 2 == 1) {
        return *medio;
    }
    const float anterior = *std::max_element(auxiliar_, medio);
    return (anterior + *medio) / 2.0f;
}
