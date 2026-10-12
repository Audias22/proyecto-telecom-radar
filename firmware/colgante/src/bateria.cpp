#include "bateria.h"

#include <Arduino.h>

#include <limits.h>

namespace {

constexpr uint32_t ADC_MAXIMO_ESPERADO_MV = static_cast<uint32_t>(
    (static_cast<uint64_t>(BATERIA_TENSION_MAXIMA_MV) *
         BATERIA_RESISTENCIA_INFERIOR_OHM +
     (BATERIA_RESISTENCIA_SUPERIOR_OHM + BATERIA_RESISTENCIA_INFERIOR_OHM) / 2U) /
    (BATERIA_RESISTENCIA_SUPERIOR_OHM + BATERIA_RESISTENCIA_INFERIOR_OHM));

static_assert(BATERIA_MODO_SIMULADO == 0 || BATERIA_MODO_SIMULADO == 1,
              "BATERIA_MODO_SIMULADO debe valer 0 o 1");
static_assert(PIN_BATERIA_ADC == 1, "La medicion prevista para el ESP32-C3 usa GPIO1/ADC1_CH1");
static_assert(BATERIA_RESISTENCIA_SUPERIOR_OHM > 0 &&
                  BATERIA_RESISTENCIA_INFERIOR_OHM > 0,
              "Las resistencias del divisor deben ser mayores que cero");
static_assert(BATERIA_ADC_MUESTRAS > 0, "Debe configurarse al menos una muestra ADC");
static_assert(BATERIA_ADC_MUESTRAS <= UINT8_MAX,
              "La cantidad de muestras debe caber en el contador");
static_assert(BATERIA_ADC_RESOLUCION_BITS >= 9 && BATERIA_ADC_RESOLUCION_BITS <= 12,
              "La resolucion ADC debe estar entre 9 y 12 bits");
static_assert(BATERIA_SIMULADA_MV <= UINT16_MAX,
              "El voltaje simulado debe caber en el campo del protocolo");
static_assert(ADC_MAXIMO_ESPERADO_MV < BATERIA_ADC_LIMITE_MV,
              "El limite ADC debe superar el maximo esperado del divisor");
static_assert(bateria::logica::convertirAdcABateria(BATERIA_ADC_LIMITE_MV) <= UINT16_MAX,
              "La conversion maxima debe caber en uint16_t");
static_assert(!bateria::logica::esBaja(BATERIA_SIMULADA_MV),
              "3900 mV simulados no deben activar bateria baja");
static_assert(bateria::logica::esBaja(BATERIA_BAJA_MV - 1U),
              "Un valor bajo el umbral debe activar bateria baja");
static_assert(!bateria::logica::esBaja(BATERIA_BAJA_MV),
              "El umbral exacto no se considera bateria baja");
static_assert(!bateria::logica::lecturaBateriaValida(BATERIA_LECTURA_INVALIDA_MV),
              "El centinela no debe considerarse una medicion valida");
static_assert(bateria::logica::lecturaBateriaValida(BATERIA_SIMULADA_MV),
              "El valor simulado debe pertenecer al dominio numerico valido");
static_assert(bateria::logica::convertirAdcABateria(1343U) == 4200U,
              "El factor del divisor debe reconstruir aproximadamente 4.2 V");

uint16_t leerMilivoltiosReales() {
    // En Arduino ESP32 3.3.7 la calibracion de analogReadMilliVolts() usa la atenuacion global.
    // Se configura antes de inicializar el canal para que lectura y calibracion coincidan.
    analogReadResolution(BATERIA_ADC_RESOLUCION_BITS);
    analogSetAttenuation(ADC_11db);
    delay(BATERIA_ADC_ESTABILIZACION_MS);

    // La primera conversion carga el circuito de muestreo; no se incorpora al promedio.
    (void)analogReadMilliVolts(PIN_BATERIA_ADC);

    uint64_t suma_adc_mv = 0;
    for (uint8_t muestra = 0; muestra < BATERIA_ADC_MUESTRAS; ++muestra) {
        const uint32_t adc_mv = analogReadMilliVolts(PIN_BATERIA_ADC);
        if (!bateria::logica::lecturaAdcValida(adc_mv)) {
            Serial.printf("bateria: lectura ADC fuera de rango: %lu mV\n",
                          static_cast<unsigned long>(adc_mv));
            return BATERIA_LECTURA_INVALIDA_MV;
        }
        suma_adc_mv += adc_mv;
    }

    const uint32_t promedio_adc_mv = static_cast<uint32_t>(
        (suma_adc_mv + BATERIA_ADC_MUESTRAS / 2U) / BATERIA_ADC_MUESTRAS);
    const uint32_t bateria_mv = bateria::logica::convertirAdcABateria(promedio_adc_mv);
    if (bateria_mv > UINT16_MAX) {
        Serial.println("bateria: conversion fuera del rango del protocolo");
        return BATERIA_LECTURA_INVALIDA_MV;
    }

    return static_cast<uint16_t>(bateria_mv);
}

}  // namespace

namespace bateria {

uint16_t leerMilivoltios() {
    if (BATERIA_MODO_SIMULADO != 0) {
        Serial.printf("bateria: SIMULADO, se usaran %u mV; no es una medicion fisica\n",
                      BATERIA_SIMULADA_MV);
        return BATERIA_SIMULADA_MV;
    }

    return leerMilivoltiosReales();
}

bool modoSimulado() {
    return BATERIA_MODO_SIMULADO != 0;
}

bool lecturaValida(uint16_t milivoltios) {
    return logica::lecturaBateriaValida(milivoltios);
}

bool baja(uint16_t milivoltios) {
    return logica::esBaja(milivoltios);
}

}  // namespace bateria
