// Receptor ESP-NOW de prueba para el colgante (ESP32 clásico, DevKitC WROOM-32).
//
// Hace de base en el banco de pruebas: recibe ALERTA y HEARTBEAT con el formato de protocolo.h,
// aplica la regla de duplicados.h y responde MSG_ACK en el mismo callback de recepción, antes de
// cualquier otra cosa. El registro por Serial se hace después, desde loop().
//
// No es la base real: no se conecta a WiFi, no usa Firebase ni Telegram y queda en un canal fijo
// que se cambia desde el monitor serial. Comandos en ayuda() o en README.md.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstring>

#include <duplicados.h>
#include <protocolo.h>

namespace {

// Canal de arranque. El colgante empieza en CANAL_POR_DEFECTO (1) tras encenderse.
constexpr uint8_t CANAL_INICIAL = CANAL_POR_DEFECTO;
constexpr UBaseType_t CAPACIDAD_COLA = 16;

enum class Resultado : uint8_t {
    NUEVO,
    DUPLICADO,
    INVALIDO,
};

// Lo que el callback deja para imprimir en loop().
struct Registro {
    uint32_t ms;
    uint8_t mac[ESP_NOW_ETH_ALEN];
    int longitud;
    PaqueteEspNow paquete;
    int rssi;
    uint8_t canal;
    Resultado resultado;
    bool ack_omitido;   // comando "p": se dejó sin ACK a propósito
    esp_err_t error_ack;
    uint32_t ack_us;    // desde la entrada al callback hasta que esp_now_send() devolvió
};

QueueHandle_t cola = nullptr;
volatile uint32_t registros_perdidos = 0;

// Estado de duplicados por id de colgante, como lo guardaría la base (en RAM).
duplicados::Estado estados[256] = {};
volatile bool olvidar_pedido = false;
volatile uint8_t acks_a_omitir = 0;

uint8_t canal_actual = CANAL_INICIAL;
uint32_t total_nuevos = 0;
uint32_t total_duplicados = 0;
uint32_t total_invalidos = 0;

bool asegurarPeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }
    esp_now_peer_info_t peer{};
    std::memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
    peer.channel = 0;  // canal actual: sigue a los cambios de canal desde el monitor
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

void encolar(const Registro& registro) {
    if (xQueueSend(cola, &registro, 0) != pdTRUE) {
        registros_perdidos = registros_perdidos + 1;
    }
}

// Corre en la tarea de WiFi: solo decide, responde el ACK y encola. Nada de Serial aquí.
void recibir(const esp_now_recv_info_t* info, const uint8_t* datos, int longitud) {
    const uint32_t inicio_us = micros();
    if (info == nullptr || info->src_addr == nullptr || datos == nullptr) {
        return;
    }

    Registro r{};
    r.ms = millis();
    std::memcpy(r.mac, info->src_addr, ESP_NOW_ETH_ALEN);
    r.longitud = longitud;
    r.rssi = info->rx_ctrl != nullptr ? info->rx_ctrl->rssi : 0;
    r.canal = info->rx_ctrl != nullptr ? info->rx_ctrl->channel : 0;
    r.resultado = Resultado::INVALIDO;
    r.error_ack = ESP_OK;

    if (longitud != static_cast<int>(sizeof(PaqueteEspNow))) {
        encolar(r);
        return;
    }
    std::memcpy(&r.paquete, datos, sizeof(r.paquete));
    const PaqueteEspNow& p = r.paquete;
    if (p.version != PROTOCOLO_VERSION || (p.tipo != MSG_ALERTA && p.tipo != MSG_HEARTBEAT)) {
        encolar(r);
        return;
    }

    if (olvidar_pedido) {
        std::memset(estados, 0, sizeof(estados));
        olvidar_pedido = false;
    }
    duplicados::Estado& estado = estados[p.id_dispositivo];
    const bool nuevo = duplicados::esNuevo(estado, p.flags, p.secuencia);
    estado = duplicados::actualizar(estado, nuevo, p.secuencia);
    r.resultado = nuevo ? Resultado::NUEVO : Resultado::DUPLICADO;

    if (nuevo && acks_a_omitir > 0) {
        acks_a_omitir = acks_a_omitir - 1;
        r.ack_omitido = true;
    } else {
        const PaqueteEspNow ack = {PROTOCOLO_VERSION,
                                   MSG_ACK,
                                   p.id_dispositivo,
                                   static_cast<uint8_t>(nuevo ? 0 : FLAG_ACK_DUPLICADO),
                                   p.secuencia,
                                   0};
        r.error_ack = asegurarPeer(r.mac)
                          ? esp_now_send(r.mac, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack))
                          : ESP_ERR_ESPNOW_NOT_FOUND;
        r.ack_us = micros() - inicio_us;
    }
    encolar(r);
}

const char* nombreTipo(uint8_t tipo) {
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

void textoFlags(uint8_t flags, char* texto, size_t tamano) {
    texto[0] = '\0';
    const struct {
        uint8_t bit;
        const char* nombre;
    } nombres[] = {
        {FLAG_BATERIA_BAJA, "BAT_BAJA"},
        {FLAG_REINTENTO, "REINTENTO"},
        {FLAG_BARRIDO, "BARRIDO"},
        {FLAG_ARRANQUE_FRIO, "ARRANQUE_FRIO"},
    };
    uint8_t restantes = flags;
    for (const auto& n : nombres) {
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

void imprimir(const Registro& r) {
    Serial.printf("[%9lu ms] %02X:%02X:%02X:%02X:%02X:%02X ", static_cast<unsigned long>(r.ms),
                  r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5]);

    if (r.resultado == Resultado::INVALIDO) {
        ++total_invalidos;
        if (r.longitud != static_cast<int>(sizeof(PaqueteEspNow))) {
            Serial.printf("INVALIDO longitud=%d rssi=%d canal=%u; sin ACK\n", r.longitud, r.rssi,
                          r.canal);
        } else {
            Serial.printf("INVALIDO version=%u tipo=%u rssi=%d canal=%u; sin ACK\n",
                          r.paquete.version, r.paquete.tipo, r.rssi, r.canal);
        }
        return;
    }

    const PaqueteEspNow& p = r.paquete;
    char flags[64];
    textoFlags(p.flags, flags, sizeof(flags));
    const bool nuevo = r.resultado == Resultado::NUEVO;
    if (nuevo) {
        ++total_nuevos;
    } else {
        ++total_duplicados;
    }

    Serial.printf("%-9s id=%u seq=%u flags=%s bat=%u mV rssi=%d dBm canal=%u %s ",
                  nombreTipo(p.tipo), p.id_dispositivo, p.secuencia, flags, p.bateria_mv, r.rssi,
                  r.canal, nuevo ? "NUEVO" : "DUPLICADO");
    if (r.ack_omitido) {
        Serial.println("ack=OMITIDO (comando p)");
    } else if (r.error_ack != ESP_OK) {
        Serial.printf("ack=ERROR %s\n", esp_err_to_name(r.error_ack));
    } else {
        Serial.printf("ack=%s en %lu us\n", nuevo ? "OK" : "OK+DUPLICADO",
                      static_cast<unsigned long>(r.ack_us));
    }
}

void imprimirMac() {
    uint8_t mac[ESP_NOW_ETH_ALEN] = {};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    Serial.printf("MAC STA: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3],
                  mac[4], mac[5]);
    Serial.printf("Para include/secrets.h del colgante:\n"
                  "  #define BASE_MAC  { 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X }\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void ayuda() {
    Serial.println("Comandos (escribir y Enter):");
    Serial.println("  1..13  cambiar de canal");
    Serial.println("  p      no responder el ACK del proximo mensaje nuevo (prueba de duplicados)");
    Serial.println("  o      olvidar las secuencias guardadas (simula reinicio de la base)");
    Serial.println("  e      estado: MAC, canal y contadores");
    Serial.println("  h      esta ayuda");
}

void estado() {
    imprimirMac();
    Serial.printf("Canal: %u. Nuevos: %lu, duplicados: %lu, invalidos: %lu, registros perdidos: %lu\n",
                  canal_actual, static_cast<unsigned long>(total_nuevos),
                  static_cast<unsigned long>(total_duplicados),
                  static_cast<unsigned long>(total_invalidos),
                  static_cast<unsigned long>(registros_perdidos));
    if (acks_a_omitir > 0) {
        Serial.println("Pendiente: se omitira el ACK del proximo mensaje nuevo");
    }
}

bool fijarCanal(uint8_t canal) {
    const esp_err_t error = esp_wifi_set_channel(canal, WIFI_SECOND_CHAN_NONE);
    if (error != ESP_OK) {
        Serial.printf("No se pudo cambiar al canal %u: %s\n", canal, esp_err_to_name(error));
        return false;
    }
    canal_actual = canal;
    Serial.printf("Canal: %u\n", canal);
    return true;
}

void ejecutarComando(const char* linea) {
    if (linea[0] == '\0') {
        return;
    }
    if (linea[0] >= '0' && linea[0] <= '9') {
        const long canal = strtol(linea, nullptr, 10);
        if (canal < CANAL_MIN || canal > CANAL_MAX) {
            Serial.printf("Canal fuera de rango (%d-%d)\n", CANAL_MIN, CANAL_MAX);
            return;
        }
        fijarCanal(static_cast<uint8_t>(canal));
        return;
    }
    switch (linea[0]) {
        case 'p':
        case 'P':
            acks_a_omitir = 1;
            Serial.println("Se omitira el ACK del proximo mensaje nuevo");
            break;
        case 'o':
        case 'O':
            olvidar_pedido = true;
            Serial.println("Secuencias olvidadas (se aplica al proximo mensaje)");
            break;
        case 'e':
        case 'E':
            estado();
            break;
        default:
            ayuda();
            break;
    }
}

void leerComandos() {
    static char linea[16];
    static size_t largo = 0;
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            linea[largo] = '\0';
            ejecutarComando(linea);
            largo = 0;
        } else if (largo + 1 < sizeof(linea)) {
            linea[largo++] = c;
        }
    }
}

void detenerPorError(const char* que) {
    Serial.printf("ERROR: %s. Revisar y reiniciar el receptor.\n", que);
    while (true) {
        delay(1000);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\nReceptor ESP-NOW de prueba (protocolo v%d)\n", PROTOCOLO_VERSION);

    cola = xQueueCreate(CAPACIDAD_COLA, sizeof(Registro));
    if (cola == nullptr) {
        detenerPorError("no se pudo crear la cola de registros");
    }

    // Modo STA sin asociarse a ningún router: así el canal se puede fijar a mano.
    WiFi.persistent(false);
    if (!WiFi.mode(WIFI_STA)) {
        detenerPorError("no se pudo iniciar WiFi en modo STA");
    }
    WiFi.disconnect();
    WiFi.setSleep(false);  // sin ahorro de energía: el radio siempre escucha
    if (!fijarCanal(CANAL_INICIAL)) {
        detenerPorError("no se pudo fijar el canal inicial");
    }

    if (esp_now_init() != ESP_OK) {
        detenerPorError("no se pudo inicializar ESP-NOW");
    }
    if (esp_now_register_recv_cb(recibir) != ESP_OK) {
        detenerPorError("no se pudo registrar el callback de recepcion");
    }

    imprimirMac();
    ayuda();
    Serial.println("Esperando mensajes del colgante...");
}

void loop() {
    Registro registro{};
    while (xQueueReceive(cola, &registro, 0) == pdTRUE) {
        imprimir(registro);
    }
    leerComandos();
    delay(5);
}
