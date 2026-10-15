#include "red_wifi.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <cstdarg>

#include "config.h"

#if BASE_WIFI_HABILITADO && !defined(DIAGNOSTICO)
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Falta include/secrets.h: copiar include/secrets.example.h como include/secrets.h y llenar WIFI_SSID y WIFI_CLAVE (o poner BASE_WIFI_HABILITADO 0 en config.h para la fase local)"
#endif
#endif

// Fase local o firmware de diagnóstico: no se conecta y no hacen falta credenciales. (Si falta
// secrets.h, esto solo evita errores en cadena después del #error.)
#ifndef WIFI_SSID
#define WIFI_SSID  ""
#define WIFI_CLAVE ""
#endif

namespace red_wifi {

namespace {

enum Estado : uint8_t {
    SIN_WIFI,    // BASE_WIFI_HABILITADO 0, o la radio no arrancó
    ESPERANDO,   // entre intentos: la radio queda quieta en un canal
    INTENTANDO,  // buscando el router; la radio cambia de canal
    CONECTADO,
};

Estado estado = SIN_WIFI;
uint32_t marca_ms = 0;   // inicio de la espera o del intento en curso
uint32_t espera_ms = 0;  // espera antes del próximo intento
uint32_t total_intentos = 0;
uint32_t fallos_seguidos = 0;
uint32_t total_desconexiones = 0;
uint8_t canal_radio = 0;
int8_t ultimo_rssi = 0;
uint32_t ultimo_muestreo_ms = 0;

// Los escribe el callback de eventos (tarea de eventos de red); loop() solo los lee.
volatile uint8_t ultima_razon = 0;
volatile uint32_t eventos_desconexion = 0;
uint32_t eventos_vistos = 0;

void alDesconectar(arduino_event_id_t, arduino_event_info_t info) {
    ultima_razon = info.wifi_sta_disconnected.reason;
    eventos_desconexion = eventos_desconexion + 1;
}

const char *nombreEstado(Estado e) {
    switch (e) {
        case ESPERANDO:
            return "esperando";
        case INTENTANDO:
            return "intentando";
        case CONECTADO:
            return "conectado";
        default:
            return "sin wifi";
    }
}

const char *nombreRazon(uint8_t razon) {
    return WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(razon));
}

uint8_t leerCanal() {
    uint8_t primario = 0;
    wifi_second_chan_t secundario = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&primario, &secundario);
    return primario;
}

unsigned long segundos(uint32_t ms) {
    return static_cast<unsigned long>(ms / 1000);
}

// Registro con el mismo formato que los demás: "# [ms] texto".
void registrar(const char *formato, ...) {
    char texto[192];
    va_list args;
    va_start(args, formato);
    vsnprintf(texto, sizeof(texto), formato, args);
    va_end(args);
    Serial.printf("# [%9lu ms] %s\n", millis(), texto);
}

// Espera creciente: 0 (primer intento), inicial, y luego el doble en cada fallo hasta el máximo.
uint32_t siguienteEspera(uint32_t actual) {
    if (actual == 0) {
        return WIFI_ESPERA_INICIAL_MS;
    }
    return actual >= WIFI_ESPERA_MAXIMA_MS / 2 ? WIFI_ESPERA_MAXIMA_MS : actual * 2;
}

// Corta el intento en curso y el reintento que el framework hace por su cuenta tras la primera
// desconexión (STA.cpp, "first_connect"), y espera espera_ms sin tocar la radio.
void esperar(uint32_t ahora) {
    esp_wifi_disconnect();
    estado = ESPERANDO;
    marca_ms = ahora;
}

void empezarIntento(uint32_t ahora) {
    total_intentos++;
    eventos_vistos = eventos_desconexion;
    estado = INTENTANDO;
    marca_ms = ahora;
    registrar("wifi: intento %lu, buscando \"%s\" (radio en canal %u)",
              static_cast<unsigned long>(total_intentos), WIFI_SSID, leerCanal());
    WiFi.begin(WIFI_SSID, WIFI_CLAVE);  // no bloquea: el resultado llega por eventos
}

void fallarIntento(uint32_t ahora, const char *motivo) {
    fallos_seguidos++;
    espera_ms = siguienteEspera(espera_ms);
    esperar(ahora);
    registrar("wifi: fallo el intento %lu (%s), %lu seguidos; siguiente en %lu s",
              static_cast<unsigned long>(total_intentos), motivo,
              static_cast<unsigned long>(fallos_seguidos), segundos(espera_ms));
}

void marcarConectado() {
    estado = CONECTADO;
    fallos_seguidos = 0;
    ultimo_rssi = static_cast<int8_t>(WiFi.RSSI());
    registrar("wifi: conectado a \"%s\", canal %u, RSSI %d dBm, IP %s (intento %lu)",
              WiFi.SSID().c_str(), leerCanal(), ultimo_rssi, WiFi.localIP().toString().c_str(),
              static_cast<unsigned long>(total_intentos));
}

void perderConexion(uint32_t ahora) {
    total_desconexiones++;
    const uint8_t razon = ultima_razon;
    espera_ms = WIFI_ESPERA_INICIAL_MS;
    esperar(ahora);
    registrar("wifi: desconectado de \"%s\", canal %u, ultimo RSSI %d dBm, motivo %u (%s); "
              "reintento en %lu s",
              WIFI_SSID, canal_radio, ultimo_rssi, razon, nombreRazon(razon),
              segundos(espera_ms));
}

void revisarIntento(uint32_t ahora) {
    if (WiFi.status() == WL_CONNECTED) {
        marcarConectado();
    } else if (eventos_desconexion != eventos_vistos) {
        char motivo[48];
        snprintf(motivo, sizeof(motivo), "motivo %u %s", ultima_razon, nombreRazon(ultima_razon));
        fallarIntento(ahora, motivo);
    } else if (ahora - marca_ms >= WIFI_INTENTO_MAX_MS) {
        fallarIntento(ahora, "tiempo agotado");
    }
}

void revisarConexion(uint32_t ahora) {
    if (WiFi.status() != WL_CONNECTED) {
        perderConexion(ahora);
    }
}

// Registra cada cambio de canal de la radio, para medir en qué canal queda entre intentos.
// Por muestreo: durante un intento se pueden escapar saltos de menos de WIFI_CANAL_MUESTREO_MS.
void vigilarCanal(uint32_t ahora) {
    if (ahora - ultimo_muestreo_ms < WIFI_CANAL_MUESTREO_MS) {
        return;
    }
    ultimo_muestreo_ms = ahora;
    if (estado == CONECTADO && WiFi.status() == WL_CONNECTED) {
        ultimo_rssi = static_cast<int8_t>(WiFi.RSSI());
    }
    const uint8_t actual = leerCanal();
    if (actual != canal_radio) {
        registrar("radio: canal %u -> %u (wifi %s)", canal_radio, actual, nombreEstado(estado));
        canal_radio = actual;
    }
}

}  // namespace

bool iniciar() {
    WiFi.persistent(false);        // credenciales solo en RAM, nada en la flash (NVS)
    WiFi.setAutoReconnect(false);  // la reconexión la maneja este módulo, con espera creciente
    if (!WiFi.mode(WIFI_STA)) {
        return false;
    }
    WiFi.disconnect();
    WiFi.setSleep(false);  // sin ahorro de energía: con el modem dormido se pierden tramas ESP-NOW

    if (!BASE_WIFI_HABILITADO) {
        if (esp_wifi_set_channel(BASE_CANAL_SIN_WIFI, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
            return false;
        }
    } else {
        WiFi.onEvent(alDesconectar, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        estado = ESPERANDO;  // espera_ms = 0: el primer intento sale en el primer actualizar()
        marca_ms = millis();
    }
    canal_radio = leerCanal();
    return true;
}

void actualizar() {
    const uint32_t ahora = millis();
    vigilarCanal(ahora);
    switch (estado) {
        case ESPERANDO:
            if (ahora - marca_ms >= espera_ms) {
                empezarIntento(ahora);
            }
            break;
        case INTENTANDO:
            revisarIntento(ahora);
            break;
        case CONECTADO:
            revisarConexion(ahora);
            break;
        default:
            break;
    }
}

bool conectado() {
    return estado == CONECTADO;
}

int8_t rssi() {
    return conectado() ? ultimo_rssi : 0;
}

uint8_t canal() {
    return leerCanal();
}

void imprimirEstado() {
    if (!BASE_WIFI_HABILITADO) {
        Serial.printf("# WiFi: deshabilitado (fase local), canal fijo %u\n", leerCanal());
        return;
    }
    Serial.printf("# WiFi: %s, SSID \"%s\", canal %u", nombreEstado(estado), WIFI_SSID, leerCanal());
    if (conectado()) {
        Serial.printf(", RSSI %d dBm, IP %s", ultimo_rssi, WiFi.localIP().toString().c_str());
    } else if (estado == ESPERANDO) {
        const uint32_t pasado = millis() - marca_ms;
        Serial.printf(", siguiente intento en %lu s",
                      segundos(pasado < espera_ms ? espera_ms - pasado : 0));
    }
    Serial.printf("\n# WiFi: intentos %lu, fallidos seguidos %lu, desconexiones %lu\n",
                  static_cast<unsigned long>(total_intentos),
                  static_cast<unsigned long>(fallos_seguidos),
                  static_cast<unsigned long>(total_desconexiones));
}

}  // namespace red_wifi
