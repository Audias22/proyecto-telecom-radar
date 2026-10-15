#include "cuerpo_http.h"

namespace {

// 7 dígitos hexadecimales: chunks de hasta 256 MB, de sobra para Firebase.
constexpr uint8_t MAX_DIGITOS_CHUNK = 7;

int valorHex(uint8_t c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

namespace cuerpo_http {

ModoCuerpo modo(int codigo, int32_t largo, bool chunked) {
    if ((codigo >= 100 && codigo < 200) || codigo == 204 || codigo == 304) {
        return CUERPO_VACIO;
    }
    if (chunked) {
        return CUERPO_CHUNKED;
    }
    return largo >= 0 ? CUERPO_LARGO : CUERPO_HASTA_CIERRE;
}

}  // namespace cuerpo_http

CuerpoHttp::CuerpoHttp(char *destino, size_t capacidad) : destino_(destino), capacidad_(capacidad) {
    if (capacidad_ > 0) {
        destino_[0] = '\0';
    }
}

void CuerpoHttp::iniciar(ModoCuerpo modo, int32_t largo) {
    modo_ = modo;
    largo_ = 0;
    recortado_ = false;
    digitos_ = 0;
    linea_vacia_ = true;
    restante_ = 0;
    if (capacidad_ > 0) {
        destino_[0] = '\0';
    }
    switch (modo) {
        case CUERPO_LARGO:
            restante_ = largo > 0 ? static_cast<uint32_t>(largo) : 0;
            estado_ = restante_ > 0 ? DATOS : LISTO;
            break;
        case CUERPO_CHUNKED:
            estado_ = TAMANO;
            break;
        case CUERPO_HASTA_CIERRE:
            estado_ = DATOS;
            break;
        default:
            estado_ = LISTO;
            break;
    }
}

size_t CuerpoHttp::agregar(const uint8_t *datos, size_t cantidad) {
    size_t i = 0;
    while (i < cantidad && estado_ != LISTO && estado_ != INVALIDO) {
        const uint8_t byte = datos[i++];
        if (modo_ == CUERPO_CHUNKED) {
            procesarChunked(byte);
            continue;
        }
        guardar(byte);
        if (modo_ == CUERPO_LARGO && --restante_ == 0) {
            estado_ = LISTO;
        }
    }
    return i;
}

void CuerpoHttp::cerrada() {
    if (modo_ == CUERPO_HASTA_CIERRE && estado_ == DATOS) {
        estado_ = LISTO;
    }
}

void CuerpoHttp::guardar(uint8_t byte) {
    if (largo_ + 1 >= capacidad_) {
        recortado_ = true;
        return;
    }
    destino_[largo_++] = static_cast<char>(byte);
    destino_[largo_] = '\0';
}

// Formato (RFC 9112, 7.1): tamaño en hexadecimal [;extensión] CRLF, datos, CRLF; el último chunk
// tiene tamaño 0 y lo siguen líneas de trailer hasta una línea vacía. Se aceptan LF sin CR.
void CuerpoHttp::procesarChunked(uint8_t byte) {
    switch (estado_) {
        case TAMANO:
            procesarTamano(byte);
            break;
        case EXTENSION:
            if (byte == '\n') {
                estado_ = restante_ > 0 ? DATOS : TRAILER;
                linea_vacia_ = true;
            }
            break;
        case DATOS:
            guardar(byte);
            if (--restante_ == 0) {
                estado_ = FIN_CHUNK;
            }
            break;
        case FIN_CHUNK:
            if (byte == '\n') {
                estado_ = TAMANO;
                digitos_ = 0;
            } else if (byte != '\r') {
                estado_ = INVALIDO;
            }
            break;
        case TRAILER:
            if (byte == '\n') {
                if (linea_vacia_) {
                    estado_ = LISTO;
                }
                linea_vacia_ = true;
            } else if (byte != '\r') {
                linea_vacia_ = false;
            }
            break;
        default:
            break;
    }
}

void CuerpoHttp::procesarTamano(uint8_t byte) {
    const int valor = valorHex(byte);
    if (valor >= 0) {
        if (++digitos_ > MAX_DIGITOS_CHUNK) {
            estado_ = INVALIDO;
            return;
        }
        restante_ = restante_ * 16 + static_cast<uint32_t>(valor);
        return;
    }
    if (digitos_ == 0) {
        estado_ = INVALIDO;  // línea sin tamaño
        return;
    }
    if (byte == '\r' || byte == ';' || byte == ' ' || byte == '\t') {
        estado_ = EXTENSION;  // el resto de la línea no importa
    } else if (byte == '\n') {
        estado_ = restante_ > 0 ? DATOS : TRAILER;
        linea_vacia_ = true;
    } else {
        estado_ = INVALIDO;
    }
}
