#include "cola_peticiones.h"

void ColaPeticiones::agregarLectura(const LecturaNube &lectura) {
    if (lecturas_.lleno()) {
        lecturas_.sacar();  // se descarta la más antigua
        lecturas_descartadas_++;
    }
    lecturas_.meter(lectura, ++ultimo_numero_);
}

bool ColaPeticiones::agregarEvento(const PeticionEvento &evento) {
    if (eventos_.lleno()) {
        eventos_rechazados_++;
        return false;
    }
    eventos_.meter(evento, ++ultimo_numero_);
    return true;
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
    if (eventos_.cantidad > 0) {
        p.tipo = PETICION_EVENTO;
        p.numero = eventos_.numeros[eventos_.inicio];
        p.evento = eventos_.datos[eventos_.inicio];
        return true;
    }
    const int c = colganteMasAntiguo();
    if (c >= 0) {
        p.tipo = PETICION_COLGANTE;
        p.numero = colgantes_[c].numero;
        p.colgante = colgantes_[c].colgante;
        return true;
    }
    if (con_hora && lecturas_.cantidad > 0) {
        p.tipo = PETICION_LECTURA;
        p.numero = lecturas_.numeros[lecturas_.inicio];
        p.lectura = lecturas_.datos[lecturas_.inicio];
        return true;
    }
    return false;
}

void ColaPeticiones::confirmar(const Peticion &p) {
    switch (p.tipo) {
        case PETICION_EVENTO:
            if (eventos_.cantidad > 0 && eventos_.numeros[eventos_.inicio] == p.numero) {
                eventos_.sacar();
            }
            break;
        case PETICION_LECTURA:
            if (lecturas_.cantidad > 0 && lecturas_.numeros[lecturas_.inicio] == p.numero) {
                lecturas_.sacar();
            }
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

TamanosCola ColaPeticiones::tamanos() const {
    size_t colgantes = 0;
    for (const auto &entrada : colgantes_) {
        colgantes += entrada.ocupada ? 1 : 0;
    }
    return {eventos_.cantidad, colgantes, lecturas_.cantidad, lecturas_descartadas_,
            eventos_rechazados_};
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
