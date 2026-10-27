#include "cola_offline.h"

#include <string.h>

namespace {

constexpr uint8_t FORMATO_EVENTOS = 1;

// Números crecientes: a es más antiguo que b. La diferencia con signo aguanta la vuelta del
// contador (2^32 peticiones).
bool anterior(uint32_t a, uint32_t b) {
    return static_cast<int32_t>(a - b) < 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Lecturas
// ---------------------------------------------------------------------------

void ColaLecturas::agregar(const LecturaNube &lectura, uint32_t numero) {
    if (cantidad_ == COLA_OFFLINE_LECTURAS) {
        inicio_ = (inicio_ + 1) % COLA_OFFLINE_LECTURAS;  // se descarta la más antigua
        cantidad_--;
        descartadas_++;
    }
    const size_t i = (inicio_ + cantidad_) % COLA_OFFLINE_LECTURAS;
    datos_[i] = lectura;
    numeros_[i] = numero;
    cantidad_++;
}

bool ColaLecturas::primera(LecturaNube &lectura, uint32_t &numero) const {
    if (cantidad_ == 0) {
        return false;
    }
    lectura = datos_[inicio_];
    numero = numeros_[inicio_];
    return true;
}

void ColaLecturas::confirmar(uint32_t numero) {
    if (cantidad_ > 0 && numeros_[inicio_] == numero) {
        inicio_ = (inicio_ + 1) % COLA_OFFLINE_LECTURAS;
        cantidad_--;
    }
}

// ---------------------------------------------------------------------------
// Eventos
// ---------------------------------------------------------------------------

ResultadoAgregar ColaEventos::agregar(const PeticionEvento &evento, uint32_t numero) {
    if (evento.destinos == 0) {
        rechazados_++;
        return EVENTO_RECHAZADO;
    }
    ResultadoAgregar resultado = EVENTO_AGREGADO;
    int i = libre();
    if (i < 0 && evento.tipo == EVENTO_BOTON) {
        i = masAntiguoNoBoton();
        resultado = EVENTO_AGREGADO_REEMPLAZANDO;
    }
    if (i < 0) {
        rechazados_++;
        return EVENTO_RECHAZADO;
    }
    reemplazados_ += resultado == EVENTO_AGREGADO_REEMPLAZANDO ? 1 : 0;
    entradas_[i] = {true, numero, evento};
    cambio_ = true;
    return resultado;
}

bool ColaEventos::siguiente(DestinoEvento destino, PeticionEvento &evento,
                            uint32_t &numero) const {
    int i = destino == DESTINO_TELEGRAM ? masAntiguo(destino, true) : -1;
    if (i < 0) {
        i = masAntiguo(destino, false);
    }
    if (i < 0) {
        return false;
    }
    evento = entradas_[i].evento;
    numero = entradas_[i].numero;
    return true;
}

void ColaEventos::confirmar(DestinoEvento destino, uint32_t numero) {
    const int i = buscar(numero);
    if (i < 0 || (entradas_[i].evento.destinos & destino) == 0) {
        return;
    }
    entradas_[i].evento.destinos &= static_cast<uint8_t>(~destino);
    entradas_[i].ocupada = entradas_[i].evento.destinos != 0;
    cambio_ = true;
}

size_t ColaEventos::cantidad() const {
    size_t n = 0;
    for (const Entrada &e : entradas_) {
        n += e.ocupada ? 1 : 0;
    }
    return n;
}

size_t ColaEventos::pendientes(DestinoEvento destino) const {
    size_t n = 0;
    for (const Entrada &e : entradas_) {
        n += e.ocupada && (e.evento.destinos & destino) != 0 ? 1 : 0;
    }
    return n;
}

// Formato: versión, cantidad, sizeof(PeticionEvento) en 2 bytes y las entradas tal cual. El
// tamaño protege contra un firmware nuevo que cambió el struct: sus datos se ignoran.
size_t ColaEventos::serializar(uint8_t *buffer, size_t tamano) const {
    const size_t n = cantidad();
    const size_t largo = 4 + n * sizeof(PeticionEvento);
    if (largo > tamano) {
        return 0;
    }
    buffer[0] = FORMATO_EVENTOS;
    buffer[1] = static_cast<uint8_t>(n);
    buffer[2] = static_cast<uint8_t>(sizeof(PeticionEvento) & 0xFF);
    buffer[3] = static_cast<uint8_t>(sizeof(PeticionEvento) >> 8);
    uint8_t *destino = buffer + 4;
    int i = siguienteEnOrden(false, 0);
    while (i >= 0) {
        memcpy(destino, &entradas_[i].evento, sizeof(PeticionEvento));
        destino += sizeof(PeticionEvento);
        i = siguienteEnOrden(true, entradas_[i].numero);
    }
    return largo;
}

int ColaEventos::restaurar(const uint8_t *datos, size_t largo, uint8_t destinos_activos,
                           uint32_t &ultimo_numero) {
    if (largo < 4 || datos[0] != FORMATO_EVENTOS ||
        (datos[2] | (datos[3] << 8)) != static_cast<int>(sizeof(PeticionEvento)) ||
        largo != 4 + datos[1] * sizeof(PeticionEvento)) {
        return -1;
    }
    int agregados = 0;
    for (size_t k = 0; k < datos[1]; k++) {
        PeticionEvento e;
        memcpy(&e, datos + 4 + k * sizeof(PeticionEvento), sizeof(e));
        e.origen[sizeof(e.origen) - 1] = '\0';
        e.detalle[sizeof(e.detalle) - 1] = '\0';
        e.destinos &= destinos_activos;
        e.previo_reinicio = true;
        if (e.destinos != 0 && agregar(e, ++ultimo_numero) != EVENTO_RECHAZADO) {
            agregados++;
        }
    }
    return agregados;
}

int ColaEventos::buscar(uint32_t numero) const {
    for (size_t k = 0; k < COLA_EVENTOS_CAPACIDAD; k++) {
        if (entradas_[k].ocupada && entradas_[k].numero == numero) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

int ColaEventos::libre() const {
    for (size_t k = 0; k < COLA_EVENTOS_CAPACIDAD; k++) {
        if (!entradas_[k].ocupada) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

int ColaEventos::masAntiguo(DestinoEvento destino, bool solo_boton) const {
    int mejor = -1;
    for (size_t k = 0; k < COLA_EVENTOS_CAPACIDAD; k++) {
        const Entrada &e = entradas_[k];
        if (!e.ocupada || (e.evento.destinos & destino) == 0 ||
            (solo_boton && e.evento.tipo != EVENTO_BOTON)) {
            continue;
        }
        if (mejor < 0 || anterior(e.numero, entradas_[mejor].numero)) {
            mejor = static_cast<int>(k);
        }
    }
    return mejor;
}

int ColaEventos::masAntiguoNoBoton() const {
    int mejor = -1;
    for (size_t k = 0; k < COLA_EVENTOS_CAPACIDAD; k++) {
        const Entrada &e = entradas_[k];
        if (!e.ocupada || e.evento.tipo == EVENTO_BOTON) {
            continue;
        }
        if (mejor < 0 || anterior(e.numero, entradas_[mejor].numero)) {
            mejor = static_cast<int>(k);
        }
    }
    return mejor;
}

// La entrada con el menor número mayor que `anterior_numero` (o la más antigua de todas).
int ColaEventos::siguienteEnOrden(bool hay_anterior, uint32_t anterior_numero) const {
    int mejor = -1;
    for (size_t k = 0; k < COLA_EVENTOS_CAPACIDAD; k++) {
        const Entrada &e = entradas_[k];
        if (!e.ocupada || (hay_anterior && !anterior(anterior_numero, e.numero))) {
            continue;
        }
        if (mejor < 0 || anterior(e.numero, entradas_[mejor].numero)) {
            mejor = static_cast<int>(k);
        }
    }
    return mejor;
}
