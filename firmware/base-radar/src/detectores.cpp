#include "detectores.h"

#include <stdio.h>

namespace {

constexpr uint8_t FORMATO_COLGANTES = 1;
constexpr uint8_t FORMATO_ESTADO = 1;
constexpr uint8_t BIT_AVISADA = 1u << 0;
constexpr uint8_t BIT_AUSENTE = 1u << 1;

}  // namespace

// ---------------------------------------------------------------------------
// Batería
// ---------------------------------------------------------------------------

// bateria_mv 0 es "sin dato" (protocolo.md): no rearma.
bool DetectorBateria::revisar(bool bandera_baja, uint16_t bateria_mv) {
    if (bandera_baja) {
        const bool nueva = !avisada_;
        avisada_ = true;
        return nueva;
    }
    if (bateria_mv >= BATERIA_RECUPERADA_MV) {
        avisada_ = false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Episodio
// ---------------------------------------------------------------------------

// Mientras la condición está en el valor opuesto al estado, se cuenta cuánto dura; al volver
// al valor del estado, la cuenta se reinicia.
CambioEpisodio Episodio::actualizar(bool condicion, uint32_t ahora) {
    if (condicion == activo_) {
        contando_ = false;
        return EPISODIO_SIN_CAMBIO;
    }
    if (!contando_) {
        contando_ = true;
        desde_ms_ = ahora;
    }
    const uint32_t espera = activo_ ? espera_fin_ : espera_inicio_;
    if (ahora - desde_ms_ < espera) {
        return EPISODIO_SIN_CAMBIO;
    }
    activo_ = condicion;
    contando_ = false;
    return activo_ ? EPISODIO_EMPIEZA : EPISODIO_TERMINA;
}

// ---------------------------------------------------------------------------
// Colgantes
// ---------------------------------------------------------------------------

ResultadoMensaje RegistroColgantes::mensaje(uint8_t id, bool bandera_baja, uint16_t bateria_mv,
                                            uint32_t ahora) {
    int i = buscar(id);
    if (i < 0) {
        i = lugarPara(ahora);
        colgantes_[i] = {true, id, ahora, false, DetectorBateria()};
        cambio_ = true;
    }
    Colgante &c = colgantes_[i];
    c.ultimo_ms = ahora;
    ResultadoMensaje r = {false, c.ausente};
    const bool avisada_antes = c.bateria.avisada();
    r.bateria_baja = c.bateria.revisar(bandera_baja, bateria_mv);
    cambio_ = cambio_ || c.ausente || c.bateria.avisada() != avisada_antes;
    c.ausente = false;
    return r;
}

size_t RegistroColgantes::revisarAusentes(uint32_t ahora, uint8_t *ids, size_t maximo) {
    size_t n = 0;
    for (Colgante &c : colgantes_) {
        if (!c.conocido || c.ausente || ahora - c.ultimo_ms < COLGANTE_AUSENTE_MS || n >= maximo) {
            continue;
        }
        c.ausente = true;
        cambio_ = true;
        ids[n++] = c.id;
    }
    return n;
}

size_t RegistroColgantes::cantidad() const {
    size_t n = 0;
    for (const Colgante &c : colgantes_) {
        n += c.conocido ? 1 : 0;
    }
    return n;
}

bool RegistroColgantes::ausente(uint8_t id) const {
    const int i = buscar(id);
    return i >= 0 && colgantes_[i].ausente;
}

// Formato: versión, cantidad y, por colgante, id y bits (BIT_AVISADA, BIT_AUSENTE).
size_t RegistroColgantes::serializar(uint8_t *buffer, size_t tamano) const {
    const size_t n = cantidad();
    if (2 + n * 2 > tamano) {
        return 0;
    }
    buffer[0] = FORMATO_COLGANTES;
    buffer[1] = static_cast<uint8_t>(n);
    size_t k = 2;
    for (const Colgante &c : colgantes_) {
        if (!c.conocido) {
            continue;
        }
        buffer[k++] = c.id;
        buffer[k++] = static_cast<uint8_t>((c.bateria.avisada() ? BIT_AVISADA : 0) |
                                           (c.ausente ? BIT_AUSENTE : 0));
    }
    return k;
}

bool RegistroColgantes::restaurar(const uint8_t *datos, size_t largo, uint32_t ahora) {
    if (largo < 2 || datos[0] != FORMATO_COLGANTES || datos[1] > MAXIMO ||
        largo != 2 + static_cast<size_t>(datos[1]) * 2) {
        return false;
    }
    for (Colgante &c : colgantes_) {
        c = {};
    }
    for (size_t k = 0; k < datos[1]; k++) {
        Colgante &c = colgantes_[k];
        const uint8_t bits = datos[2 + k * 2 + 1];
        c = {true, datos[2 + k * 2], ahora, (bits & BIT_AUSENTE) != 0, DetectorBateria()};
        c.bateria.restaurar((bits & BIT_AVISADA) != 0);
    }
    cambio_ = false;
    return true;
}

int RegistroColgantes::buscar(uint8_t id) const {
    for (size_t k = 0; k < MAXIMO; k++) {
        if (colgantes_[k].conocido && colgantes_[k].id == id) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

// Un lugar libre o, con la lista llena, el colgante que lleva más tiempo sin mensajes.
int RegistroColgantes::lugarPara(uint32_t ahora) const {
    int mejor = 0;
    for (size_t k = 0; k < MAXIMO; k++) {
        if (!colgantes_[k].conocido) {
            return static_cast<int>(k);
        }
        if (ahora - colgantes_[k].ultimo_ms > ahora - colgantes_[mejor].ultimo_ms) {
            mejor = static_cast<int>(k);
        }
    }
    return mejor;
}

// ---------------------------------------------------------------------------
// Estado guardado, desconexión y duraciones
// ---------------------------------------------------------------------------

size_t serializarEstado(const RegistroColgantes &colgantes, bool radar_sin_datos, uint8_t *buffer,
                        size_t tamano) {
    if (tamano < 2) {
        return 0;
    }
    buffer[0] = FORMATO_ESTADO;
    buffer[1] = radar_sin_datos ? 1 : 0;
    const size_t n = colgantes.serializar(buffer + 2, tamano - 2);
    return n > 0 ? n + 2 : 0;
}

bool restaurarEstado(const uint8_t *datos, size_t largo, uint32_t ahora,
                     RegistroColgantes &colgantes, bool &radar_sin_datos) {
    if (largo < 2 || datos[0] != FORMATO_ESTADO ||
        !colgantes.restaurar(datos + 2, largo - 2, ahora)) {
        return false;
    }
    radar_sin_datos = datos[1] != 0;
    return true;
}

uint64_t duracionDesconexion(bool hay_anterior, uint64_t anterior_ms, uint64_t nuevo_ms) {
    if (!hay_anterior || nuevo_ms <= anterior_ms) {
        return 0;
    }
    const uint64_t duracion = nuevo_ms - anterior_ms;
    return duracion >= BASE_DESCONECTADA_MIN_MS ? duracion : 0;
}

size_t textoDuracion(uint64_t ms, char *texto, size_t tamano) {
    const unsigned long s = static_cast<unsigned long>(ms / 1000);
    int n;
    if (s < 60) {
        n = snprintf(texto, tamano, "%lu s", s);
    } else if (s < 3600) {
        n = snprintf(texto, tamano, "%lu min", s / 60);
    } else if (s < 86400) {
        n = snprintf(texto, tamano, "%lu h %lu min", s / 3600, s % 3600 / 60);
    } else {
        n = snprintf(texto, tamano, "%lu d %lu h", s / 86400, s % 86400 / 3600);
    }
    return n > 0 ? static_cast<size_t>(n) : 0;
}
