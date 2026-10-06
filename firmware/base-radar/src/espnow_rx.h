// espnow_rx.h
// Recepción de mensajes del colgante por ESP-NOW, filtro de duplicados y envío del ACK.
// Formato y reglas en docs/protocolo.md.

#pragma once

#include <stdint.h>
#include <protocolo.h>

struct MensajeColgante {
    PaqueteEspNow paquete;
    uint8_t       mac[6];
    int8_t        rssi;
    bool          duplicado;  // la secuencia ya se había procesado; no repetir la alerta
};

namespace espnow_rx {

// TODO: iniciar ESP-NOW después de WiFi (modo STA, WiFi.setSleep(false)) y registrar el
// callback de recepción. El callback solo copia el paquete a una cola; el trabajo se hace en loop().
void iniciar();

// TODO: saca el siguiente mensaje de la cola. Ya respondió el ACK y marcó si es duplicado.
bool siguienteMensaje(MensajeColgante &mensaje);

}  // namespace espnow_rx
