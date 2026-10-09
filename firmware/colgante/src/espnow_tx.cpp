#include <Arduino.h>
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_now.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstring>

#include "espnow_tx.h"
#include "secrets.h"

namespace {

constexpr uint8_t MAC_BASE[ESP_NOW_ETH_ALEN] = BASE_MAC;
constexpr UBaseType_t CAPACIDAD_COLA_ACK = 4;

static_assert(sizeof(PaqueteEspNow) == 8, "El paquete ESP-NOW debe medir exactamente 8 bytes");
static_assert(COLGANTE_ID > 0 && COLGANTE_ID <= UINT8_MAX,
              "COLGANTE_ID debe estar entre 1 y 255");
static_assert(CANAL_MIN >= 1 && CANAL_MIN <= CANAL_MAX,
              "El rango de canales ESP-NOW no es valido");
static_assert(CANAL_POR_DEFECTO >= CANAL_MIN && CANAL_POR_DEFECTO <= CANAL_MAX,
              "El canal por defecto debe pertenecer al rango de barrido");
static_assert(ACK_REINTENTOS > 0, "Debe existir al menos un intento en el canal guardado");
static_assert(ACK_REINTENTOS_BARRIDO > 0,
              "Debe existir al menos un intento por canal durante el barrido");
static_assert(ACK_TIMEOUT_MS > 0 && ACK_TIMEOUT_BARRIDO_MS > 0,
              "Los tiempos de espera del ACK deben ser mayores que cero");

struct AckRecibido {
    PaqueteEspNow paquete;
    uint8_t canal;
};

RTC_DATA_ATTR uint16_t secuencia_rtc = 0;
RTC_DATA_ATTR uint8_t canal_rtc = CANAL_POR_DEFECTO;
RTC_DATA_ATTR bool primer_mensaje_rtc = true;

StaticQueue_t estructura_cola_ack;
uint8_t almacenamiento_cola_ack[CAPACIDAD_COLA_ACK * sizeof(AckRecibido)];
QueueHandle_t cola_ack = nullptr;

bool estado_rtc_revisado = false;
bool espnow_iniciado = false;
bool callback_registrado = false;
bool peer_registrado = false;
bool transmisor_listo = false;

bool tipoValido(TipoMensaje tipo) {
    return tipo == MSG_ALERTA || tipo == MSG_HEARTBEAT;
}

constexpr PaqueteEspNow ACK_PRUEBA = {
    PROTOCOLO_VERSION, MSG_ACK, COLGANTE_ID, 0, 0x1234, 0};
constexpr PaqueteEspNow ACK_VERSION_INVALIDA = {
    PROTOCOLO_VERSION + 1, MSG_ACK, COLGANTE_ID, 0, 0x1234, 0};
constexpr PaqueteEspNow ACK_TIPO_INVALIDO = {
    PROTOCOLO_VERSION, MSG_ALERTA, COLGANTE_ID, 0, 0x1234, 0};
constexpr PaqueteEspNow ACK_ID_INVALIDO = {
    PROTOCOLO_VERSION, MSG_ACK, static_cast<uint8_t>(COLGANTE_ID + 1), 0, 0x1234, 0};
constexpr PaqueteEspNow PAQUETE_PRUEBA =
    espnow_tx::logica::construirPaquete(MSG_ALERTA, COLGANTE_ID, BATERIA_BAJA_MV - 1,
                                        0x4321, true);
static_assert(espnow_tx::logica::ackValido(ACK_PRUEBA, COLGANTE_ID, 0x1234),
              "Un ACK coincidente debe ser valido");
static_assert(!espnow_tx::logica::ackValido(ACK_VERSION_INVALIDA, COLGANTE_ID, 0x1234),
              "Un ACK de otra version debe ser rechazado");
static_assert(!espnow_tx::logica::ackValido(ACK_TIPO_INVALIDO, COLGANTE_ID, 0x1234),
              "Un paquete que no sea ACK debe ser rechazado");
static_assert(!espnow_tx::logica::ackValido(ACK_ID_INVALIDO, COLGANTE_ID, 0x1234),
              "Un ACK de otro colgante debe ser rechazado");
static_assert(!espnow_tx::logica::ackValido(ACK_PRUEBA, COLGANTE_ID, 0x1235),
              "Un ACK de otra secuencia debe ser rechazado");
static_assert((espnow_tx::logica::prepararFlags(0, true, false) & FLAG_REINTENTO) != 0,
              "Un reintento debe marcar FLAG_REINTENTO");
static_assert((espnow_tx::logica::prepararFlags(0, true, true) &
               (FLAG_REINTENTO | FLAG_BARRIDO)) ==
                  (FLAG_REINTENTO | FLAG_BARRIDO),
              "El barrido debe marcar reintento y barrido");
static_assert(PAQUETE_PRUEBA.secuencia == 0x4321 &&
                  (PAQUETE_PRUEBA.flags & (FLAG_BATERIA_BAJA | FLAG_ARRANQUE_FRIO)) ==
                      (FLAG_BATERIA_BAJA | FLAG_ARRANQUE_FRIO),
              "La construccion del paquete debe conservar campos y flags");

void informarError(const char* operacion, esp_err_t error) {
    Serial.printf("espnow_tx: fallo al %s: %s (%d)\n", operacion, esp_err_to_name(error), error);
}

void prepararEstadoRtc() {
    if (estado_rtc_revisado) {
        return;
    }

    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        secuencia_rtc = 0;
        canal_rtc = CANAL_POR_DEFECTO;
        primer_mensaje_rtc = true;
    } else if (!espnow_tx::logica::canalValido(canal_rtc)) {
        canal_rtc = CANAL_POR_DEFECTO;
    }

    estado_rtc_revisado = true;
}

bool macBaseValida() {
    bool todos_cero = true;
    for (uint8_t octeto : MAC_BASE) {
        todos_cero = todos_cero && octeto == 0;
    }

    return !todos_cero && (MAC_BASE[0] & 0x01u) == 0;
}

bool fijarCanal(uint8_t canal) {
    if (!espnow_tx::logica::canalValido(canal)) {
        Serial.printf("espnow_tx: canal fuera de rango: %u\n", canal);
        return false;
    }

    const esp_err_t error = esp_wifi_set_channel(canal, WIFI_SECOND_CHAN_NONE);
    if (error != ESP_OK) {
        informarError("cambiar el canal", error);
        return false;
    }

    return true;
}

void recibir(const esp_now_recv_info_t* informacion, const uint8_t* datos, int longitud) {
    if (cola_ack == nullptr || informacion == nullptr || informacion->src_addr == nullptr ||
        informacion->rx_ctrl == nullptr || datos == nullptr ||
        longitud != static_cast<int>(sizeof(PaqueteEspNow)) ||
        std::memcmp(informacion->src_addr, MAC_BASE, ESP_NOW_ETH_ALEN) != 0) {
        return;
    }

    AckRecibido recibido{};
    std::memcpy(&recibido.paquete, datos, sizeof(recibido.paquete));
    recibido.canal = informacion->rx_ctrl->channel;
    xQueueSend(cola_ack, &recibido, 0);
}

bool esperarAck(uint16_t secuencia, uint8_t canal, uint32_t espera_ms) {
    TickType_t limite = pdMS_TO_TICKS(espera_ms);
    if (limite == 0) {
        limite = 1;
    }

    const TickType_t inicio = xTaskGetTickCount();
    while (true) {
        const TickType_t transcurrido = xTaskGetTickCount() - inicio;
        if (transcurrido >= limite) {
            return false;
        }

        AckRecibido recibido{};
        if (xQueueReceive(cola_ack, &recibido, limite - transcurrido) != pdTRUE) {
            return false;
        }

        if (recibido.canal == canal &&
            espnow_tx::logica::ackValido(recibido.paquete, COLGANTE_ID, secuencia)) {
            return true;
        }
    }
}

PaqueteEspNow construirPaquete(TipoMensaje tipo, uint16_t bateria_mv, uint16_t secuencia) {
    return espnow_tx::logica::construirPaquete(tipo, COLGANTE_ID, bateria_mv, secuencia,
                                               primer_mensaje_rtc);
}

bool enviarIntentos(const PaqueteEspNow& original, uint8_t canal, uint8_t intentos,
                    uint32_t espera_ms, bool barrido) {
    if (!fijarCanal(canal)) {
        return false;
    }

    for (uint8_t intento = 0; intento < intentos; ++intento) {
        PaqueteEspNow paquete = original;
        paquete.flags =
            espnow_tx::logica::prepararFlags(paquete.flags, intento > 0 || barrido, barrido);

        const esp_err_t error =
            esp_now_send(MAC_BASE, reinterpret_cast<const uint8_t*>(&paquete), sizeof(paquete));
        if (error != ESP_OK) {
            informarError("enviar el paquete", error);
        }

        // Solo un ACK de aplicacion valido completa el intento. El resultado de esp_now_send()
        // indica que la trama fue aceptada para transmision, no que la base proceso el paquete.
        if (esperarAck(paquete.secuencia, canal, espera_ms)) {
            return true;
        }
    }

    return false;
}

}  // namespace

namespace espnow_tx {

void iniciar() {
    prepararEstadoRtc();
    if (transmisor_listo) {
        return;
    }

    if (!macBaseValida()) {
        Serial.println("espnow_tx: BASE_MAC no contiene una direccion unicast valida");
        return;
    }

    if (cola_ack == nullptr) {
        cola_ack = xQueueCreateStatic(CAPACIDAD_COLA_ACK, sizeof(AckRecibido),
                                      almacenamiento_cola_ack, &estructura_cola_ack);
    }
    if (cola_ack == nullptr) {
        Serial.println("espnow_tx: no se pudo crear la cola de ACK");
        return;
    }
    xQueueReset(cola_ack);

    if (!WiFi.mode(WIFI_STA)) {
        Serial.println("espnow_tx: no se pudo iniciar WiFi en modo STA");
        detener();
        return;
    }
    const esp_err_t error_desconexion = esp_wifi_disconnect();
    if (error_desconexion != ESP_OK && error_desconexion != ESP_ERR_WIFI_NOT_CONNECT) {
        informarError("desconectar WiFi del router", error_desconexion);
        detener();
        return;
    }
    if (!WiFi.setSleep(false)) {
        Serial.println("espnow_tx: no se pudo desactivar el ahorro de energia de WiFi");
        detener();
        return;
    }
    if (!fijarCanal(canal_rtc)) {
        detener();
        return;
    }

    esp_err_t error = esp_now_init();
    if (error != ESP_OK) {
        informarError("inicializar ESP-NOW", error);
        detener();
        return;
    }
    espnow_iniciado = true;

    error = esp_now_register_recv_cb(recibir);
    if (error != ESP_OK) {
        informarError("registrar el callback de recepcion", error);
        detener();
        return;
    }
    callback_registrado = true;

    esp_now_peer_info_t peer{};
    std::memcpy(peer.peer_addr, MAC_BASE, ESP_NOW_ETH_ALEN);
    peer.channel = 0;  // Sigue el canal actual para permitir el barrido.
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    error = esp_now_add_peer(&peer);
    if (error != ESP_OK) {
        informarError("registrar la base como peer", error);
        detener();
        return;
    }
    peer_registrado = true;
    transmisor_listo = true;
}

ResultadoEnvio enviar(TipoMensaje tipo, uint16_t bateria_mv) {
    prepararEstadoRtc();
    if (!transmisor_listo || cola_ack == nullptr) {
        Serial.println("espnow_tx: envio solicitado sin una inicializacion valida");
        return ENVIO_SIN_ACK;
    }
    if (!tipoValido(tipo)) {
        Serial.printf("espnow_tx: tipo de mensaje no permitido: %u\n", static_cast<unsigned>(tipo));
        return ENVIO_SIN_ACK;
    }

    ++secuencia_rtc;
    const PaqueteEspNow paquete = construirPaquete(tipo, bateria_mv, secuencia_rtc);
    xQueueReset(cola_ack);

    if (enviarIntentos(paquete, canal_rtc, ACK_REINTENTOS, ACK_TIMEOUT_MS, false)) {
        primer_mensaje_rtc = false;
        return ENVIO_OK;
    }

    for (uint8_t canal = CANAL_MIN; canal <= CANAL_MAX; ++canal) {
        if (enviarIntentos(paquete, canal, ACK_REINTENTOS_BARRIDO,
                           ACK_TIMEOUT_BARRIDO_MS, true)) {
            canal_rtc = canal;
            primer_mensaje_rtc = false;
            return ENVIO_OK_BARRIDO;
        }
    }

    return ENVIO_SIN_ACK;
}

void detener() {
    transmisor_listo = false;

    if (callback_registrado) {
        const esp_err_t error = esp_now_unregister_recv_cb();
        if (error != ESP_OK) {
            informarError("quitar el callback de recepcion", error);
        }
        callback_registrado = false;
    }

    if (peer_registrado) {
        const esp_err_t error = esp_now_del_peer(MAC_BASE);
        if (error != ESP_OK) {
            informarError("eliminar el peer de la base", error);
        }
        peer_registrado = false;
    }

    if (espnow_iniciado) {
        const esp_err_t error = esp_now_deinit();
        if (error != ESP_OK) {
            informarError("detener ESP-NOW", error);
        }
        espnow_iniciado = false;
    }

    if (cola_ack != nullptr) {
        xQueueReset(cola_ack);
    }
    if (!WiFi.mode(WIFI_OFF)) {
        Serial.println("espnow_tx: no se pudo apagar WiFi");
    }
}

}  // namespace espnow_tx
