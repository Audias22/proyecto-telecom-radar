// mensajes_telegram.h
// Qué dice cada aviso de Telegram y cuándo se puede mandar el siguiente (telegram.h hace el
// HTTPS). Tiempos en config.h (TELEGRAM_*).
//
// Lógica pura, sin Arduino: la usan firebase_cliente.cpp y telegram.cpp, y las pruebas host.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cola_offline.h"
#include "config.h"

namespace mensajes_telegram {

// Texto corto en español para el cuidador, a partir del evento (tipo, origen y detalle). Si el
// evento es de antes de un reinicio, o salió con más de un minuto de atraso, lo dice. Devuelve la
// longitud escrita.
size_t texto(const PeticionEvento &evento, uint32_t ahora, char *texto, size_t tamano);

// Los avisos que no esperan el tope por hora ni el intervalo normal.
bool esUrgente(TipoEvento tipo);

}  // namespace mensajes_telegram

// Límite de frecuencia y espera tras error de los envíos a Telegram. Las restas de millis() son
// sin signo: aguantan la vuelta del contador.
class LimiteTelegram {
public:
    // true si ahora se puede intentar un envío.
    bool puedeEnviar(uint32_t ahora, bool urgente) const;

    // Milisegundos hasta que se pueda (0 si ya se puede).
    uint32_t espera(uint32_t ahora, bool urgente) const;

    // El mensaje salió (HTTP 200).
    void enviado(uint32_t ahora, bool urgente);

    // Error de red o del servidor: espera creciente, o lo que pidió el servidor si es mayor.
    void fallo(uint32_t ahora, uint32_t pedido_por_servidor_ms);

    // Respuesta definitiva que no es de red (rechazo): termina la espera creciente.
    void sinError() { espera_error_ms_ = 0; }

    uint32_t esperaError() const { return espera_error_ms_; }

private:
    uint32_t esperaEnvios(uint32_t ahora, bool urgente) const;

    bool     hubo_envio_ = false;
    uint32_t ultimo_envio_ms_ = 0;
    // Envíos no urgentes de la última hora, en anillo (el más antiguo en inicio_).
    uint32_t envios_[TELEGRAM_MAX_POR_HORA] = {};
    size_t   inicio_ = 0;
    size_t   cantidad_ = 0;
    uint32_t espera_error_ms_ = 0;  // 0 = sin error pendiente
    uint32_t marca_error_ms_ = 0;
};
