// vigilante_red.h
// Vigilante de la tarea de red: qué petición HTTPS está en curso y desde cuándo, y qué hacer si
// la tarea deja de avanzar (docs/fallos.md, escenario 5). loop() llama a revisar() una vez por
// segundo:
//   1. CORTAR: una petición pasa de FIREBASE_PETICION_MAX_MS. loop() cierra su socket; la tarea
//      de red recibe un error, lo anota y recrea el cliente TLS. Es la recuperación normal.
//   2. REINICIAR: FIREBASE_REINICIO_TRAS_CORTE_MS después del corte la petición sigue sin
//      terminar (bloqueada donde el socket no la despierta, p. ej. en el DNS de lwIP), o la tarea
//      no da vueltas fuera de una petición por más de FIREBASE_PETICION_MAX_MS (no hay socket
//      que cortar). Es el último recurso: se pierde la cola en RAM. Con una alerta local urgente
//      activa no se reinicia: se pospone hasta que termine.
//
// Lógica pura, sin Arduino ni candados: firebase_cliente.cpp la protege con su mutex.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"

enum AccionVigilante : uint8_t {
    VIGILANTE_NADA,
    VIGILANTE_CORTAR,
    VIGILANTE_REINICIAR,
};

struct PeticionEnCurso {
    bool     activa;
    char     metodo[8];
    char     ruta[64];   // sin token ni parámetros
    uint32_t inicio_ms;
};

class VigilanteRed {
public:
    // La tarea de red dio una vuelta fuera de una petición (también al arrancar).
    void latir(uint32_t ahora);

    void empezar(const char *metodo, const char *ruta, uint32_t ahora);

    // Devuelve true si el vigilante la había cortado.
    bool terminar(uint32_t ahora);

    AccionVigilante revisar(uint32_t ahora, bool alerta_activa);

    const PeticionEnCurso &peticion() const { return peticion_; }
    uint32_t cortes() const { return cortes_; }
    bool reinicioPospuesto() const { return pospuesto_; }

private:
    PeticionEnCurso peticion_ = {};
    uint32_t latido_ms_ = 0;
    bool     cortada_ = false;
    uint32_t corte_ms_ = 0;
    uint32_t cortes_ = 0;
    bool     pospuesto_ = false;
};
