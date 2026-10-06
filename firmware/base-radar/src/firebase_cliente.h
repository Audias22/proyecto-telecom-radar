// firebase_cliente.h
// Escritura en Firebase Realtime Database por la API REST sobre HTTPS.
// Rutas y formato en docs/modelo-datos.md.

#pragma once

#include <stdint.h>

#include "radar.h"

enum TipoEvento : uint8_t {
    EVENTO_APNEA,
    EVENTO_BOTON,
    EVENTO_BATERIA_BAJA,
    EVENTO_BASE_DESCONECTADA,
};

namespace firebase_cliente {

// TODO: iniciar sesión con DISPOSITIVO_EMAIL / DISPOSITIVO_CLAVE (API REST de Firebase Auth) y
// guardar el idToken. Renovarlo antes de que venza (1 hora).
void iniciar();

// TODO: renovar token y mantener la referencia de hora del servidor. No debe bloquear loop().
void actualizar();

// Hay token válido y referencia de hora del servidor.
bool listo();

// TODO: hora del servidor estimada en ms (referencia del servidor + millis() transcurridos).
// Se usa como clave de /lecturas. Devuelve 0 si todavía no hay referencia.
uint64_t horaServidorMs();

// TODO: convertir el millis() de una medición a hora del servidor (para lecturas en cola).
uint64_t millisAHoraServidor(uint32_t millis_medicion);

// TODO: PUT /lecturas/{baseId}/{ts}. Devuelve false si falla (la lectura va a la cola).
bool enviarLectura(const LecturaRadar &lectura, uint64_t ts_ms, bool diferida);

// TODO: PATCH /dispositivos/{baseId}/estado con ultimo_contacto = {".sv":"timestamp"}.
bool enviarEstado(int8_t rssi_wifi);

// TODO: PATCH /colgantes/{id}.
bool actualizarColgante(uint8_t id, uint16_t bateria_mv, int8_t rssi);

// TODO: POST /eventos.
bool enviarEvento(TipoEvento tipo, const char *origen);

}  // namespace firebase_cliente
