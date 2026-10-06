// radar.h
// Lectura del radar MR60BHA2 por UART con la librería Seeed Arduino mmWave.
//
// El radar manda tramas independientes (fases, respiración, latido, distancia, presencia).
// Este módulo guarda el último valor de cada una y arma una LecturaRadar cada vez que llega una
// trama de fases, que es la de mayor tasa: así cada lectura es una muestra de fase con los
// demás valores vigentes en ese momento.

#pragma once

#include <stdint.h>

struct LecturaRadar {
    uint32_t millis_medicion;  // millis() al recibir la trama de fases
    bool     valida;           // false si falta algún valor o alguno está fuera de rango
    bool     presencia;        // hay una persona en el cono del radar
    float    distancia_cm;     // distancia a la persona
    float    resp_rpm;         // respiraciones por minuto
    float    latido_bpm;       // latidos por minuto (dato secundario, menos confiable)
    float    fase_total;       // fase total de la señal reflejada
    float    fase_resp;        // componente de fase de la respiración
    float    fase_latido;      // componente de fase del latido
};

namespace radar {

// Abre la UART del radar. No bloquea.
void iniciar();

// Procesa las tramas que hayan llegado. No bloquea: solo lee lo que ya está en el buffer.
// Llamar en cada vuelta de loop().
void actualizar();

// Saca la siguiente lectura pendiente (en orden de llegada). Devuelve false si no hay.
// Se guardan hasta 32 lecturas; si no se sacan a tiempo, se pierden las más antiguas.
bool siguienteLectura(LecturaRadar &lectura);

// Última lectura armada, aunque ya se haya sacado con siguienteLectura().
LecturaRadar ultimaLectura();

// true si no llega ninguna trama del radar desde hace RADAR_SIN_DATOS_MS.
bool sinDatos();

// Veces que se reinició la UART por falta de datos desde el arranque.
uint32_t reiniciosUart();

// Tramas procesadas desde el arranque, para medir la tasa de datos.
uint32_t tramasFase();
uint32_t tramasTotales();

}  // namespace radar
