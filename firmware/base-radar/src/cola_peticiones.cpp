#include "cola_peticiones.h"

void ColaPeticiones::agregarLectura(const LecturaNube &lectura) {
    lecturas_.agregar(lectura, ++ultimo_numero_);
}

ResultadoAgregar ColaPeticiones::agregarEvento(const PeticionEvento &evento) {
    return eventos_.agregar(evento, ++ultimo_numero_);
}

// Reemplaza el pendiente del mismo id con un número nuevo: si la tarea de red estaba enviando
// el anterior, su confirmar() ya no saca este.
void ColaPeticiones::agregarColgante(const PeticionColgante &colgante) {
    int i = buscarColgante(colgante.id);
    if (i < 0) {
        i = buscarLibre();
    }
    if (i < 0) {
        i = colganteMasAntiguo();  // 8 colgantes distintos pendientes: el más viejo se pisa
    }
    colgantes_[i] = {true, ++ultimo_numero_, colgante};
}

bool ColaPeticiones::siguiente(bool con_hora, Peticion &p) const {
    p = {};
    if (eventos_.siguiente(DESTINO_FIREBASE, p.evento, p.numero)) {
        p.tipo = PETICION_EVENTO;
        return true;
    }
    const int c = colganteMasAntiguo();
    if (c >= 0) {
        p.tipo = PETICION_COLGANTE;
        p.numero = colgantes_[c].numero;
        p.colgante = colgantes_[c].colgante;
        return true;
    }
    if (con_hora && lecturas_.primera(p.lectura, p.numero)) {
        p.tipo = PETICION_LECTURA;
        return true;
    }
    p = {};
    return false;
}

void ColaPeticiones::confirmar(const Peticion &p) {
    switch (p.tipo) {
        case PETICION_EVENTO:
            eventos_.confirmar(DESTINO_FIREBASE, p.numero);
            break;
        case PETICION_LECTURA:
            lecturas_.confirmar(p.numero);
            break;
        case PETICION_COLGANTE:
            for (auto &entrada : colgantes_) {
                if (entrada.ocupada && entrada.numero == p.numero) {
                    entrada.ocupada = false;
                }
            }
            break;
        default:
            break;
    }
}

bool ColaPeticiones::siguienteTelegram(PeticionEvento &evento, uint32_t &numero) const {
    return eventos_.siguiente(DESTINO_TELEGRAM, evento, numero);
}

void ColaPeticiones::confirmarTelegram(uint32_t numero) {
    eventos_.confirmar(DESTINO_TELEGRAM, numero);
}

size_t ColaPeticiones::serializarEventos(uint8_t *buffer, size_t tamano) {
    const size_t largo = eventos_.serializar(buffer, tamano);
    if (largo > 0) {
        eventos_.limpiarCambio();
    }
    return largo;
}

int ColaPeticiones::restaurarEventos(const uint8_t *datos, size_t largo,
                                     uint8_t destinos_activos) {
    return eventos_.restaurar(datos, largo, destinos_activos, ultimo_numero_);
}

TamanosCola ColaPeticiones::tamanos() const {
    size_t colgantes = 0;
    for (const auto &entrada : colgantes_) {
        colgantes += entrada.ocupada ? 1 : 0;
    }
    return {eventos_.cantidad(),
            eventos_.pendientes(DESTINO_FIREBASE),
            eventos_.pendientes(DESTINO_TELEGRAM),
            colgantes,
            lecturas_.cantidad(),
            lecturas_.descartadas(),
            eventos_.rechazados(),
            eventos_.reemplazados()};
}

// Números crecientes: el menor es el más antiguo. La comparación por diferencia aguanta la vuelta
// del contador (2^32 peticiones).
int ColaPeticiones::colganteMasAntiguo() const {
    int mejor = -1;
    for (size_t k = 0; k < COLA_COLGANTES_CAPACIDAD; k++) {
        if (!colgantes_[k].ocupada) {
            continue;
        }
        if (mejor < 0 ||
            static_cast<int32_t>(colgantes_[k].numero - colgantes_[mejor].numero) < 0) {
            mejor = static_cast<int>(k);
        }
    }
    return mejor;
}

int ColaPeticiones::buscarLibre() const {
    for (size_t k = 0; k < COLA_COLGANTES_CAPACIDAD; k++) {
        if (!colgantes_[k].ocupada) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

int ColaPeticiones::buscarColgante(uint8_t id) const {
    for (size_t k = 0; k < COLA_COLGANTES_CAPACIDAD; k++) {
        if (colgantes_[k].ocupada && colgantes_[k].colgante.id == id) {
            return static_cast<int>(k);
        }
    }
    return -1;
}
