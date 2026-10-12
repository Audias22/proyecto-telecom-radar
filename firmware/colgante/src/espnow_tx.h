// espnow_tx.h
// Envío de ALERTA y HEARTBEAT a la base, espera del ACK, reintentos y barrido de canales.
// Flujo completo en docs/protocolo.md.

#pragma once

#include <stdint.h>
#include <protocolo.h>

enum ResultadoEnvio {
    ENVIO_OK,          // ACK de aplicacion valido recibido en el canal guardado
    ENVIO_OK_BARRIDO,  // ACK valido recibido durante el barrido; se guardo el canal
    ENVIO_SIN_ACK,     // fallo de inicializacion, envio o ningun canal respondio
};

namespace espnow_tx {

// Logica pura, sin dependencias de WiFi ni hardware. Se mantiene separada para permitir pruebas
// unitarias en un entorno host con paquetes sinteticos.
namespace logica {

constexpr bool canalValido(uint8_t canal) {
    return canal >= CANAL_MIN && canal <= CANAL_MAX;
}

constexpr bool ackValido(const PaqueteEspNow& ack, uint8_t id_dispositivo, uint16_t secuencia) {
    return ack.version == PROTOCOLO_VERSION && ack.tipo == MSG_ACK &&
           ack.id_dispositivo == id_dispositivo && ack.secuencia == secuencia;
}

constexpr uint8_t prepararFlags(uint8_t flags, bool reintento, bool barrido) {
    return flags | (reintento ? FLAG_REINTENTO : 0) | (barrido ? FLAG_BARRIDO : 0);
}

constexpr PaqueteEspNow construirPaquete(TipoMensaje tipo, uint8_t id_dispositivo,
                                         uint16_t bateria_mv, uint16_t secuencia,
                                         bool arranque_frio) {
    return {PROTOCOLO_VERSION,
            static_cast<uint8_t>(tipo),
            id_dispositivo,
            static_cast<uint8_t>((bateria_mv < BATERIA_BAJA_MV ? FLAG_BATERIA_BAJA : 0) |
                                 (arranque_frio ? FLAG_ARRANQUE_FRIO : 0)),
            secuencia,
            bateria_mv};
}

}  // namespace logica

// Inicia WiFi en modo STA sin asociarse a un router, fija el canal RTC e inicializa ESP-NOW.
// Los errores se informan por Serial; en ese caso enviar() devuelve ENVIO_SIN_ACK.
void iniciar();

// Arma un paquete con una secuencia nueva, lo envia con reintentos y espera un ACK de aplicacion.
// Los reintentos conservan la secuencia. Solo ALERTA y HEARTBEAT son tipos permitidos.
ResultadoEnvio enviar(TipoMensaje tipo, uint16_t bateria_mv);

// Libera ESP-NOW, quita el peer y apaga WiFi. Es segura si la inicializacion quedo incompleta.
void detener();

}  // namespace espnow_tx
