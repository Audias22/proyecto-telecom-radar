#include "espnow_rx.h"

#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstring>

#include <duplicados.h>

namespace espnow_rx {

namespace {

constexpr UBaseType_t CAPACIDAD_COLA = 16;

QueueHandle_t cola = nullptr;
volatile uint32_t total_invalidos = 0;
volatile uint32_t total_perdidos = 0;

// Última secuencia procesada de cada colgante, por id. Solo la usa el callback de recepción.
duplicados::Estado estados[256] = {};

bool asegurarPeer(const uint8_t *mac) {
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }
    esp_now_peer_info_t peer{};
    std::memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
    peer.channel = 0;  // canal actual de la radio: sigue al del router si cambia
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

bool esPaqueteValido(const uint8_t *datos, int longitud) {
    if (longitud != static_cast<int>(sizeof(PaqueteEspNow))) {
        return false;
    }
    const uint8_t version = datos[0];
    const uint8_t tipo = datos[1];
    return version == PROTOCOLO_VERSION && (tipo == MSG_ALERTA || tipo == MSG_HEARTBEAT);
}

// El ACK lleva el canal de la radio para que el colgante guarde ese y no el canal vecino en el que
// pudo oírlo (docs/protocolo.md, sección 6).
bool responderAck(const uint8_t *mac, const PaqueteEspNow &p, bool nuevo) {
    const PaqueteEspNow ack = ackConCanal(p.id_dispositivo, p.secuencia, !nuevo, canalActual());
    return asegurarPeer(mac) &&
           esp_now_send(mac, reinterpret_cast<const uint8_t *>(&ack), sizeof(ack)) == ESP_OK;
}

// Corre en la tarea de WiFi: decide, responde el ACK lo antes posible y encola. Nada de Serial.
void recibir(const esp_now_recv_info_t *info, const uint8_t *datos, int longitud) {
    if (info == nullptr || info->src_addr == nullptr || datos == nullptr) {
        return;
    }
    if (!esPaqueteValido(datos, longitud)) {
        total_invalidos = total_invalidos + 1;
        return;
    }

    MensajeColgante m{};
    std::memcpy(&m.paquete, datos, sizeof(m.paquete));
    std::memcpy(m.mac, info->src_addr, ESP_NOW_ETH_ALEN);
    m.ms = millis();
    m.rssi = info->rx_ctrl != nullptr ? static_cast<int8_t>(info->rx_ctrl->rssi) : 0;
    m.canal = info->rx_ctrl != nullptr ? static_cast<uint8_t>(info->rx_ctrl->channel) : 0;

    duplicados::Estado &estado = estados[m.paquete.id_dispositivo];
    const bool nuevo = duplicados::esNuevo(estado, m.paquete.flags, m.paquete.secuencia);
    estado = duplicados::actualizar(estado, nuevo, m.paquete.secuencia);
    m.duplicado = !nuevo;
    m.ack_ok = responderAck(m.mac, m.paquete, nuevo);

    if (xQueueSend(cola, &m, 0) != pdTRUE) {
        total_perdidos = total_perdidos + 1;
    }
}

}  // namespace

bool iniciar() {
    cola = xQueueCreate(CAPACIDAD_COLA, sizeof(MensajeColgante));
    if (cola == nullptr) {
        return false;
    }
    return esp_now_init() == ESP_OK && esp_now_register_recv_cb(recibir) == ESP_OK;
}

bool siguienteMensaje(MensajeColgante &mensaje) {
    return cola != nullptr && xQueueReceive(cola, &mensaje, 0) == pdTRUE;
}

void macPropia(uint8_t mac[6]) {
    std::memset(mac, 0, ESP_NOW_ETH_ALEN);
    esp_wifi_get_mac(WIFI_IF_STA, mac);
}

uint8_t canalActual() {
    uint8_t canal = 0;
    wifi_second_chan_t secundario = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&canal, &secundario);
    return canal;
}

uint32_t invalidos() {
    return total_invalidos;
}

uint32_t perdidos() {
    return total_perdidos;
}

const char *nombreTipo(uint8_t tipo) {
    switch (tipo) {
        case MSG_ALERTA:
            return "ALERTA";
        case MSG_HEARTBEAT:
            return "HEARTBEAT";
        case MSG_ACK:
            return "ACK";
        default:
            return "?";
    }
}

void textoFlags(uint8_t flags, char *texto, size_t tamano) {
    texto[0] = '\0';
    const struct {
        uint8_t bit;
        const char *nombre;
    } nombres[] = {
        {FLAG_BATERIA_BAJA, "BAT_BAJA"},
        {FLAG_REINTENTO, "REINTENTO"},
        {FLAG_BARRIDO, "BARRIDO"},
        {FLAG_ARRANQUE_FRIO, "ARRANQUE_FRIO"},
    };
    uint8_t restantes = flags;
    for (const auto &n : nombres) {
        if ((flags & n.bit) != 0) {
            if (texto[0] != '\0') {
                strlcat(texto, "|", tamano);
            }
            strlcat(texto, n.nombre, tamano);
            restantes &= static_cast<uint8_t>(~n.bit);
        }
    }
    if (restantes != 0) {
        char extra[12];
        snprintf(extra, sizeof(extra), "%s0x%02X", texto[0] != '\0' ? "|" : "", restantes);
        strlcat(texto, extra, tamano);
    }
    if (texto[0] == '\0') {
        strlcpy(texto, "-", tamano);
    }
}

}  // namespace espnow_rx
