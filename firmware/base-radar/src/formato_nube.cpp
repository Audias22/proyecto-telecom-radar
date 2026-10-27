#include "formato_nube.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace formato_nube {

namespace {

// Rangos de web/database.rules.json.
constexpr float ACTIVIDAD_MAX = 10.0f;
constexpr float DISTANCIA_MAX_CM = 1000.0f;
constexpr float RESP_MAX_RPM = 60.0f;
constexpr float LATIDO_MAX_BPM = 250.0f;
constexpr int RSSI_MIN = -127;
constexpr uint16_t BATERIA_MAX_MV = 5000;
constexpr uint64_t TS_MIN = 1000000000000ULL;   // 13 dígitos
constexpr uint64_t TS_MAX = 9999999999999ULL;

// Agrega texto con formato al final de json. Devuelve false (y deja de escribir) si no cabe.
class Escritor {
public:
    Escritor(char *json, size_t tamano) : json_(json), tamano_(tamano) {
        if (tamano_ > 0) {
            json_[0] = '\0';
        }
    }

    void agregar(const char *formato, ...) {
        if (!ok_) {
            return;
        }
        va_list args;
        va_start(args, formato);
        const int n = vsnprintf(json_ + largo_, tamano_ - largo_, formato, args);
        va_end(args);
        if (n < 0 || static_cast<size_t>(n) >= tamano_ - largo_) {
            ok_ = false;
            return;
        }
        largo_ += static_cast<size_t>(n);
    }

    // Texto JSON entre comillas, escapando comillas, barras y caracteres de control.
    void agregarTexto(const char *texto) {
        agregar("\"");
        for (const char *c = texto; *c != '\0' && ok_; c++) {
            const unsigned char u = static_cast<unsigned char>(*c);
            if (u == '"' || u == '\\') {
                agregar("\\%c", *c);
            } else if (u < 0x20) {
                agregar("\\u%04x", u);
            } else {
                agregar("%c", *c);
            }
        }
        agregar("\"");
    }

    size_t terminar() {
        if (!ok_) {
            if (tamano_ > 0) {
                json_[0] = '\0';
            }
            return 0;
        }
        return largo_;
    }

private:
    char *json_;
    size_t tamano_;
    size_t largo_ = 0;
    bool ok_ = true;
};

bool enRango(float valor, float maximo) {
    return !std::isnan(valor) && valor > 0.0f && valor <= maximo;
}

float redondear(float valor, float escala) {
    return std::round(valor * escala) / escala;
}

void agregarOpcionales(Escritor &e, const LecturaNube &l) {
    if (!std::isnan(l.actividad) && l.actividad >= 0.0f) {
        e.agregar(",\"actividad\":%.3f", static_cast<double>(std::fmin(l.actividad, ACTIVIDAD_MAX)));
    }
    if (!std::isnan(l.distancia_cm) && l.distancia_cm >= 0.0f &&
        std::round(l.distancia_cm) <= DISTANCIA_MAX_CM) {
        e.agregar(",\"distancia_cm\":%.0f", static_cast<double>(std::round(l.distancia_cm)));
    }
}

void agregarModulo(Escritor &e, const LecturaNube &l) {
    const float resp = redondear(l.resp_rpm, 10.0f);
    if (enRango(resp, RESP_MAX_RPM)) {
        e.agregar(",\"resp_rpm\":%.1f", static_cast<double>(resp));
    }
    const float latido = redondear(l.latido_bpm, 1.0f);
    if (enRango(latido, LATIDO_MAX_BPM)) {
        e.agregar(",\"latido_bpm\":%.0f", static_cast<double>(latido));
    }
}

const char *saltarEspacios(const char *c) {
    while (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r') {
        c++;
    }
    return c;
}

// Inicio del valor de "clave" (después de los dos puntos y los espacios), o nullptr.
const char *buscarValor(const char *json, const char *clave) {
    char buscado[48];
    const int n = snprintf(buscado, sizeof(buscado), "\"%s\"", clave);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(buscado)) {
        return nullptr;
    }
    const char *c = std::strstr(json, buscado);
    if (c == nullptr) {
        return nullptr;
    }
    c = saltarEspacios(c + n);
    return *c == ':' ? saltarEspacios(c + 1) : nullptr;
}

}  // namespace

uint64_t millisAHoraServidor(uint64_t hora_ref_ms, uint32_t millis_ref, uint32_t millis_medicion) {
    const int32_t diferencia = static_cast<int32_t>(millis_medicion - millis_ref);
    return static_cast<uint64_t>(static_cast<int64_t>(hora_ref_ms) + diferencia);
}

bool claveLectura(uint64_t ts_ms, char *clave, size_t tamano) {
    if (ts_ms < TS_MIN || ts_ms > TS_MAX || tamano < 14) {
        return false;
    }
    snprintf(clave, tamano, "%013llu", static_cast<unsigned long long>(ts_ms));
    return true;
}

size_t jsonLectura(const LecturaNube &l, bool diferida, char *json, size_t tamano) {
    Escritor e(json, tamano);
    const bool persona = l.persona && l.objetivo;
    e.agregar("{\"persona\":%s,\"objetivo\":%s", persona ? "true" : "false",
              l.objetivo ? "true" : "false");
    if (l.objetivo) {
        agregarOpcionales(e, l);
    }
    agregarModulo(e, l);
    if (diferida) {
        e.agregar(",\"diferida\":true");
    }
    e.agregar("}");
    return e.terminar();
}

size_t jsonEstado(int rssi_wifi, const char *version, char *json, size_t tamano) {
    const int rssi = rssi_wifi > 0 ? 0 : (rssi_wifi < RSSI_MIN ? RSSI_MIN : rssi_wifi);
    Escritor e(json, tamano);
    e.agregar("{\"online\":true,\"ultimo_contacto\":%s,\"rssi_wifi\":%d,\"version_firmware\":",
              HORA_SERVIDOR, rssi);
    e.agregarTexto(version);
    e.agregar("}");
    return e.terminar();
}

size_t jsonColgante(uint16_t bateria_mv, int rssi, const char *base, char *json, size_t tamano) {
    const unsigned bateria = bateria_mv > BATERIA_MAX_MV ? 0 : bateria_mv;
    const int r = rssi > 0 ? 0 : (rssi < RSSI_MIN ? RSSI_MIN : rssi);
    Escritor e(json, tamano);
    e.agregar("{\"bateria_mv\":%u,\"ultimo_heartbeat\":%s,\"rssi\":%d,\"base\":", bateria,
              HORA_SERVIDOR, r);
    e.agregarTexto(base);
    e.agregar("}");
    return e.terminar();
}

size_t jsonEvento(TipoEvento tipo, const char *origen, const char *detalle, char *json,
                  size_t tamano) {
    Escritor e(json, tamano);
    e.agregar("{\"tipo\":\"%s\",\"origen\":", nombreEvento(tipo));
    e.agregarTexto(origen);
    e.agregar(",\"timestamp\":%s,\"atendido\":false", HORA_SERVIDOR);
    if (detalle != nullptr && detalle[0] != '\0') {
        e.agregar(",\"detalle\":");
        e.agregarTexto(detalle);
    }
    e.agregar("}");
    return e.terminar();
}

const char *nombreEvento(TipoEvento tipo) {
    switch (tipo) {
        case EVENTO_BOTON:
            return "BOTON";
        case EVENTO_BATERIA_BAJA:
            return "BATERIA_BAJA";
        case EVENTO_BASE_DESCONECTADA:
            return "BASE_DESCONECTADA";
        case EVENTO_COLGANTE_AUSENTE:
            return "COLGANTE_AUSENTE";
        case EVENTO_RADAR_SIN_DATOS:
            return "RADAR_SIN_DATOS";
        case EVENTO_REINICIO:
            return "REINICIO";
        default:
            return "APNEA";
    }
}

size_t jsonInicioSesion(const char *correo, const char *clave, char *json, size_t tamano) {
    Escritor e(json, tamano);
    e.agregar("{\"email\":");
    e.agregarTexto(correo);
    e.agregar(",\"password\":");
    e.agregarTexto(clave);
    e.agregar(",\"returnSecureToken\":true}");
    return e.terminar();
}

size_t jsonTelegram(const char *chat_id, const char *texto, char *json, size_t tamano) {
    Escritor e(json, tamano);
    e.agregar("{\"chat_id\":");
    e.agregarTexto(chat_id);
    e.agregar(",\"text\":");
    e.agregarTexto(texto);
    e.agregar("}");
    return e.terminar();
}

bool extraerTexto(const char *json, const char *clave, char *destino, size_t tamano) {
    const char *c = buscarValor(json, clave);
    if (c == nullptr || tamano == 0 || *c != '"') {
        return false;
    }
    c++;
    size_t i = 0;
    for (; *c != '"'; c++) {
        if (*c == '\0' || *c == '\\' || i + 1 >= tamano) {
            return false;
        }
        destino[i++] = *c;
    }
    destino[i] = '\0';
    return true;
}

bool extraerEntero(const char *json, const char *clave, uint64_t &valor) {
    const char *c = buscarValor(json, clave);
    return c != nullptr && *c != '"' && leerEntero(c, valor);
}

bool leerEntero(const char *texto, uint64_t &valor) {
    const char *c = saltarEspacios(texto);
    if (*c == '"') {
        c++;
    }
    if (*c < '0' || *c > '9') {
        return false;
    }
    uint64_t v = 0;
    for (; *c >= '0' && *c <= '9'; c++) {
        v = v * 10 + static_cast<uint64_t>(*c - '0');
    }
    valor = v;
    return true;
}

}  // namespace formato_nube
