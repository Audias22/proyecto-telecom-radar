// formato_nube.h
// JSON que la base escribe en Firebase y lectura de las respuestas, con los formatos exactos que
// acepta web/database.rules.json (docs/modelo-datos.md; mismos campos que web/simulador).
//
// Lógica pura, sin Arduino: la usan firebase_cliente.cpp y las pruebas host (pruebas_host/).
// Las funciones json*() devuelven la longitud escrita, o 0 si no cupo en el buffer.

#pragma once

#include <stddef.h>
#include <stdint.h>

enum TipoEvento : uint8_t {
    EVENTO_APNEA,  // reservado: la base no lo crea y las reglas lo rechazan (docs/fallos.md)
    EVENTO_BOTON,
    EVENTO_BATERIA_BAJA,
    EVENTO_BASE_DESCONECTADA,
    EVENTO_COLGANTE_AUSENTE,
    EVENTO_RADAR_SIN_DATOS,
    EVENTO_REINICIO,  // solo Telegram: las reglas no lo aceptan en /eventos (docs/fallos.md, 10)
};

// Una lectura de /lecturas/{baseId}/{ts} (docs/modelo-datos.md, 1.1). NAN = sin dato.
struct LecturaNube {
    uint32_t millis_medicion;  // millis() al armarla; la clave ts sale de aquí
    bool     persona;
    bool     objetivo;
    float    actividad;
    float    distancia_cm;
    float    resp_rpm;
    float    latido_bpm;
};

namespace formato_nube {

// Valor de Firebase que el servidor reemplaza por su hora (ms) al guardar.
constexpr const char *HORA_SERVIDOR = "{\".sv\":\"timestamp\"}";

// Hora del servidor de una medición: referencia + (millis_medicion - millis_ref). La resta es con
// signo: funciona igual para mediciones de antes de la referencia (docs/modelo-datos.md, 2).
uint64_t millisAHoraServidor(uint64_t hora_ref_ms, uint32_t millis_ref, uint32_t millis_medicion);

// Clave de /lecturas: exactamente 13 dígitos. false si ts no tiene 13 dígitos.
bool claveLectura(uint64_t ts_ms, char *clave, size_t tamano);

// persona y objetivo siempre; actividad (3 decimales) y distancia_cm (entera) solo con objetivo;
// resp_rpm (1 decimal) y latido_bpm (entero) solo si, redondeados, son > 0 y están en el rango
// de las reglas. persona sin objetivo se escribe false (las reglas lo exigen).
size_t jsonLectura(const LecturaNube &lectura, bool diferida, char *json, size_t tamano);

// online, ultimo_contacto (hora del servidor), rssi_wifi y version_firmware.
size_t jsonEstado(int rssi_wifi, const char *version, char *json, size_t tamano);

// bateria_mv (fuera de 0-5000 se escribe 0 = sin dato), ultimo_heartbeat (hora del servidor),
// rssi y base.
size_t jsonColgante(uint16_t bateria_mv, int rssi, const char *base, char *json, size_t tamano);

// tipo, origen, timestamp (hora del servidor), atendido = false y detalle (si no es vacío).
size_t jsonEvento(TipoEvento tipo, const char *origen, const char *detalle, char *json,
                  size_t tamano);

// "BOTON", "BATERIA_BAJA", ... ("APNEA" para el tipo reservado, "REINICIO" para el aviso que
// solo va a Telegram).
const char *nombreEvento(TipoEvento tipo);

// Cuerpo de signInWithPassword: {"email":...,"password":...,"returnSecureToken":true}.
size_t jsonInicioSesion(const char *correo, const char *clave, char *json, size_t tamano);

// Cuerpo de sendMessage de Telegram: {"chat_id":"...","text":"..."}. chat_id va como texto, que
// la API acepta igual que un número.
size_t jsonTelegram(const char *chat_id, const char *texto, char *json, size_t tamano);

// Copia el valor de texto de "clave" en un JSON plano (respuestas de Firebase Auth). false si no
// está, no es texto, trae escapes o no cabe en destino.
bool extraerTexto(const char *json, const char *clave, char *destino, size_t tamano);

// Valor entero sin signo de "clave" en un JSON plano, p. ej. retry_after en la respuesta 429 de
// Telegram ({"ok":false,"parameters":{"retry_after":5}}). false si no está o no es un entero.
bool extraerEntero(const char *json, const char *clave, uint64_t &valor);

// Lee un entero sin signo (con espacios o comillas alrededor), p. ej. la respuesta de
// GET .../ultimo_contacto.json o el "3600" de expiresIn. false si no hay dígitos.
bool leerEntero(const char *texto, uint64_t &valor);

}  // namespace formato_nube
