#include "vigilante_red.h"

#include <string.h>

namespace {

void copiar(char *destino, size_t tamano, const char *origen) {
    strncpy(destino, origen != nullptr ? origen : "", tamano - 1);
    destino[tamano - 1] = '\0';
}

}  // namespace

void VigilanteRed::latir(uint32_t ahora) {
    latido_ms_ = ahora;
    pospuesto_ = false;
}

void VigilanteRed::empezar(const char *metodo, const char *ruta, uint32_t ahora) {
    peticion_.activa = true;
    copiar(peticion_.metodo, sizeof(peticion_.metodo), metodo);
    copiar(peticion_.ruta, sizeof(peticion_.ruta), ruta);
    peticion_.inicio_ms = ahora;
    cortada_ = false;
    pospuesto_ = false;
}

bool VigilanteRed::terminar(uint32_t ahora) {
    const bool cortada = cortada_;
    peticion_.activa = false;
    cortada_ = false;
    latir(ahora);
    return cortada;
}

// Las restas de millis() son sin signo: aguantan la vuelta del contador (~49.7 días).
AccionVigilante VigilanteRed::revisar(uint32_t ahora, bool alerta_activa) {
    if (peticion_.activa) {
        if (!cortada_) {
            if (ahora - peticion_.inicio_ms < FIREBASE_PETICION_MAX_MS) {
                return VIGILANTE_NADA;
            }
            cortada_ = true;
            corte_ms_ = ahora;
            cortes_++;
            return VIGILANTE_CORTAR;
        }
        if (ahora - corte_ms_ < FIREBASE_REINICIO_TRAS_CORTE_MS) {
            return VIGILANTE_NADA;
        }
    } else if (ahora - latido_ms_ < FIREBASE_PETICION_MAX_MS) {
        return VIGILANTE_NADA;
    }
    if (alerta_activa) {
        pospuesto_ = true;
        return VIGILANTE_NADA;
    }
    return VIGILANTE_REINICIAR;
}
