// espnow_rx.h
// Recepción de mensajes del colgante por ESP-NOW, filtro de duplicados y envío del ACK.
// Formato y reglas en docs/protocolo.md.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <protocolo.h>

struct MensajeColgante {
    PaqueteEspNow paquete;
    uint8_t       mac[6];
    int8_t        rssi;
    uint8_t       canal;      // canal en el que llegó
    uint32_t      ms;         // millis() al recibirlo
    bool          duplicado;  // la secuencia ya se había procesado; no repetir la alerta
    bool          ack_ok;     // esp_now_send() del ACK devolvió ESP_OK
};

namespace espnow_rx {

// Inicia WiFi en modo STA sin conectarse (ahorro de energía desactivado), fija el canal
// BASE_CANAL_SIN_WIFI, inicia ESP-NOW y registra el callback de recepción. El callback valida el
// paquete, aplica la regla de duplicados, responde el ACK y copia el mensaje a una cola; el resto
// del trabajo se hace en loop() con siguienteMensaje(). Devuelve false si algo falló.
bool iniciar();

// Saca el siguiente mensaje de la cola. Ya respondió el ACK y marcó si es duplicado.
bool siguienteMensaje(MensajeColgante &mensaje);

// MAC de la base en modo STA (la que va en BASE_MAC del secrets.h del colgante).
void macPropia(uint8_t mac[6]);

uint8_t canalActual();

// Paquetes descartados por longitud, versión o tipo inválidos (sin ACK).
uint32_t invalidos();

// Mensajes que no cupieron en la cola (el ACK sí se respondió).
uint32_t perdidos();

// Texto para los registros.
const char *nombreTipo(uint8_t tipo);
void textoFlags(uint8_t flags, char *texto, size_t tamano);

}  // namespace espnow_rx
