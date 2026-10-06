#include "firebase_cliente.h"

namespace firebase_cliente {

void iniciar() {
    // TODO
}

void actualizar() {
    // TODO
}

bool listo() {
    // TODO
    return false;
}

uint64_t horaServidorMs() {
    // TODO
    return 0;
}

uint64_t millisAHoraServidor(uint32_t millis_medicion) {
    // TODO
    (void)millis_medicion;
    return 0;
}

bool enviarLectura(const LecturaRadar &lectura, uint64_t ts_ms, bool diferida) {
    // TODO
    (void)lectura;
    (void)ts_ms;
    (void)diferida;
    return false;
}

bool enviarEstado(int8_t rssi_wifi) {
    // TODO
    (void)rssi_wifi;
    return false;
}

bool actualizarColgante(uint8_t id, uint16_t bateria_mv, int8_t rssi) {
    // TODO
    (void)id;
    (void)bateria_mv;
    (void)rssi;
    return false;
}

bool enviarEvento(TipoEvento tipo, const char *origen) {
    // TODO
    (void)tipo;
    (void)origen;
    return false;
}

}  // namespace firebase_cliente
