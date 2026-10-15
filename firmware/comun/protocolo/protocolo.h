// protocolo.h
// Formato de los mensajes ESP-NOW entre el colgante y la base, y constantes del enlace.
// Este archivo es la única definición del protocolo: lo incluyen ambos firmwares.
// Especificación completa en docs/protocolo.md.
//
// Si se cambia el struct, hay que subir PROTOCOLO_VERSION y actualizar docs/protocolo.md.

#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Versión
// ---------------------------------------------------------------------------

// Versión del formato de paquete. La base descarta paquetes con otra versión.
#define PROTOCOLO_VERSION 1

// ---------------------------------------------------------------------------
// Tipos de mensaje
// ---------------------------------------------------------------------------

enum TipoMensaje : uint8_t {
    MSG_ALERTA    = 1,  // colgante -> base: se presionó el botón
    MSG_HEARTBEAT = 2,  // colgante -> base: aviso periódico de que sigue vivo
    MSG_ACK       = 3,  // base -> colgante: confirma la recepción de un mensaje
};

// ---------------------------------------------------------------------------
// Flags (campo "flags", bits independientes)
// ---------------------------------------------------------------------------

// En mensajes del colgante:
#define FLAG_BATERIA_BAJA  (1u << 0)  // voltaje por debajo de BATERIA_BAJA_MV
#define FLAG_REINTENTO     (1u << 1)  // retransmisión de un mensaje ya enviado (misma secuencia)
#define FLAG_BARRIDO       (1u << 2)  // enviado durante el barrido de canales
#define FLAG_ARRANQUE_FRIO (1u << 3)  // primer mensaje tras encender o reiniciar (secuencia reiniciada)

// En mensajes ACK de la base:
#define FLAG_ACK_DUPLICADO (1u << 0)  // la base ya había procesado esta secuencia

// ---------------------------------------------------------------------------
// Paquete (8 bytes, little-endian, sin relleno)
// ---------------------------------------------------------------------------

struct __attribute__((packed)) PaqueteEspNow {
    uint8_t  version;         // PROTOCOLO_VERSION
    uint8_t  tipo;            // valor de TipoMensaje
    uint8_t  id_dispositivo;  // id del colgante (en un ACK: id del colgante al que se responde)
    uint8_t  flags;           // combinación de FLAG_*
    uint16_t secuencia;       // contador del colgante; el ACK repite la secuencia que confirma
    uint16_t bateria_mv;      // ALERTA/HEARTBEAT: batería del colgante en mV.
                              // ACK: canal de la base (ver ackConCanal() y canalDeAck()).
};

static_assert(sizeof(PaqueteEspNow) == 8, "PaqueteEspNow debe medir 8 bytes");

// ---------------------------------------------------------------------------
// Canal de radio
// ---------------------------------------------------------------------------

// La base usa el canal de su router WiFi; el colgante debe transmitir en ese mismo canal.
#define CANAL_POR_DEFECTO 1   // canal inicial si el colgante no tiene uno guardado
#define CANAL_MIN         1
#define CANAL_MAX         13  // último canal que barre el colgante (Por confirmar con el router de la casa)

// En un MSG_ACK el campo bateria_mv lleva el canal en el que está la radio de la base (1-13).
// El colgante guarda ese canal y no el canal en el que oyó el ACK: por el solape de los canales
// de 2.4 GHz puede oírlo estando en un canal vecino (docs/protocolo.md, sección 6).
// 0 = sin dato (base anterior a este cambio o canal desconocido); el colgante usa entonces el
// canal en el que recibió el ACK. No cambia el formato, así que PROTOCOLO_VERSION sigue en 1.
#define ACK_CANAL_SIN_DATO 0

// ACK para el mensaje (id_dispositivo, secuencia), con el canal actual de la base.
constexpr PaqueteEspNow ackConCanal(uint8_t id_dispositivo, uint16_t secuencia, bool duplicado,
                                    uint8_t canal_base) {
    return {PROTOCOLO_VERSION,
            MSG_ACK,
            id_dispositivo,
            static_cast<uint8_t>(duplicado ? FLAG_ACK_DUPLICADO : 0),
            secuencia,
            canal_base};
}

// Canal que anuncia la base en un ACK. Se devuelve con 16 bits para que un valor fuera de rango
// (por ejemplo 263) no se trunque a un canal válido; quien lo usa debe validarlo.
constexpr uint16_t canalDeAck(const PaqueteEspNow& ack) {
    return ack.bateria_mv;
}

// ---------------------------------------------------------------------------
// Tiempos y reintentos (valores iniciales, se ajustan con mediciones)
// ---------------------------------------------------------------------------

#define ACK_TIMEOUT_MS          100  // espera del ACK en el canal guardado
#define ACK_REINTENTOS          3    // envíos en el canal guardado antes de barrer
#define ACK_TIMEOUT_BARRIDO_MS  50   // espera del ACK por canal durante el barrido
#define ACK_REINTENTOS_BARRIDO  2    // envíos por canal durante el barrido

// Intervalo entre heartbeats del colgante.
#define HEARTBEAT_INTERVALO_S   (15u * 60u)

// La base marca un colgante como ausente si no recibe nada en este tiempo (3 heartbeats).
#define COLGANTE_AUSENTE_S      (3u * HEARTBEAT_INTERVALO_S)

// ---------------------------------------------------------------------------
// Batería del colgante
// ---------------------------------------------------------------------------

// Umbral de batería baja para la celda Li-ion de 240 mAh.
// Por confirmar con una curva de descarga medida en el colgante.
#define BATERIA_BAJA_MV 3500
