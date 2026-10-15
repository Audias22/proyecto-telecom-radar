// radar.h
// Lectura del radar MR60BHA2 por UART con la librería Seeed Arduino mmWave.
//
// El radar manda tramas independientes (fases, respiración, latido, distancia, presencia).
// Este módulo guarda el último valor de cada una y arma una LecturaRadar cada vez que llega una
// trama de fases, que es la de mayor tasa: así cada lectura es una muestra de fase con los
// demás valores vigentes en ese momento.
//
// Además lleva la ventana de 30 s de actividad de la que salen "objetivo" y "persona"
// (ventana_actividad.h). La bandera `presencia` del módulo no sirve para eso: sigue en 1 con la
// cama vacía (docs/radar.md, 5 y 11).

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ventana_actividad.h"

struct LecturaRadar {
    uint32_t millis_medicion;  // millis() al recibir la trama de fases
    bool     valida;           // false si falta algún valor o alguno está fuera de rango
    bool     presencia;        // bandera del módulo; NO indica persona (ver estadoPersona())
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

// Objetivo, persona y actividad de los últimos RADAR_VENTANA_ACTIVIDAD_MS (30 s), evaluados ahora.
EstadoPersona estadoPersona();

// true si no llega ninguna trama del radar desde hace RADAR_SIN_DATOS_MS.
bool sinDatos();

// Veces que se reinició la UART por falta de datos desde el arranque.
uint32_t reiniciosUart();

// Tramas procesadas desde el arranque, para medir la tasa de datos.
uint32_t tramasFase();
uint32_t tramasTotales();

// Máximo de bytes que llegaron a esperar en el buffer de la UART desde el arranque. Sirve para
// confirmar que no se pierden tramas mientras la tarea de red hace TLS (ver radar.cpp).
size_t picoBufferUart();

// Bytes en la UART equivalentes a las 120 tramas que guarda la librería (~1.6 s con objetivo).
// Un pico por debajo de esto significa que no se descartó ninguna trama por atraso de loop().
constexpr size_t PICO_UART_SEGURO = 2800;

}  // namespace radar
