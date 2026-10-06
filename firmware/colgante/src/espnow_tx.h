// espnow_tx.h
// Envío de ALERTA y HEARTBEAT a la base, espera del ACK, reintentos y barrido de canales.
// Flujo completo en docs/protocolo.md.

#pragma once

#include <stdint.h>
#include <protocolo.h>

enum ResultadoEnvio {
    ENVIO_OK,            // ACK recibido en el canal guardado
    ENVIO_OK_BARRIDO,    // ACK recibido después de barrer canales (se guardó el canal nuevo)
    ENVIO_SIN_ACK,       // ningún canal respondió
};

namespace espnow_tx {

// TODO: WiFi en modo STA sin conectar, fijar el canal guardado en RTC, iniciar ESP-NOW y
// registrar la base (BASE_MAC de secrets.h) como peer.
void iniciar();

// TODO: arma el paquete con la siguiente secuencia (guardada en RTC), lo envía y espera el ACK
// con reintentos y barrido. Marca FLAG_ARRANQUE_FRIO en el primer mensaje tras un reinicio.
ResultadoEnvio enviar(TipoMensaje tipo, uint16_t bateria_mv);

// TODO: apagar ESP-NOW y WiFi antes de dormir.
void detener();

}  // namespace espnow_tx
