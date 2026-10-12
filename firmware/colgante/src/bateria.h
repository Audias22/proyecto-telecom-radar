// bateria.h
// Medicion real o simulada del voltaje de la bateria Li-ion.

#pragma once

#include <stdint.h>
#include <protocolo.h>

#include "config.h"

namespace bateria {

// Logica pura que puede probarse sin ADC ni hardware.
namespace logica {

constexpr bool lecturaAdcValida(uint32_t adc_mv) {
    return adc_mv <= BATERIA_ADC_LIMITE_MV;
}

constexpr uint32_t convertirAdcABateria(uint32_t adc_mv) {
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(adc_mv) *
             (BATERIA_RESISTENCIA_SUPERIOR_OHM + BATERIA_RESISTENCIA_INFERIOR_OHM) +
         BATERIA_RESISTENCIA_INFERIOR_OHM / 2U) /
        BATERIA_RESISTENCIA_INFERIOR_OHM);
}

constexpr bool esBaja(uint16_t milivoltios) {
    return milivoltios < BATERIA_BAJA_MV;
}

constexpr bool lecturaBateriaValida(uint16_t milivoltios) {
    return milivoltios != BATERIA_LECTURA_INVALIDA_MV;
}

}  // namespace logica

// Devuelve BATERIA_SIMULADA_MV cuando el modo simulado esta activo. En modo real configura el
// ADC, promedia lecturas calibradas y aplica el factor del divisor resistivo. El centinela
// BATERIA_LECTURA_INVALIDA_MV indica un error y no es una medicion fisica de cero voltios.
uint16_t leerMilivoltios();

// Permite impedir que el valor simulado se presente como una medicion fisica.
bool modoSimulado();

// Distingue el centinela de error de una medicion valida. Para afirmar que existe una medicion
// fisica se requiere ademas que modoSimulado() sea false.
bool lecturaValida(uint16_t milivoltios);

bool baja(uint16_t milivoltios);

}  // namespace bateria
