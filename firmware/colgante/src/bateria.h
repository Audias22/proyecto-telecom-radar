// bateria.h
// Medición del voltaje de la batería Li-ion por ADC.

#pragma once

#include <stdint.h>

namespace bateria {

// TODO: leer PIN_BATERIA_ADC con analogReadMilliVolts() (promedio de varias muestras) y
// multiplicar por el factor del divisor resistivo.
// Por confirmar con el módulo: valores del divisor y su consumo en reposo, que se suma al del
// deep sleep.
uint16_t leerMilivoltios();

bool baja(uint16_t milivoltios);

}  // namespace bateria
