// duplicados.h
// Regla de la base para decidir si un mensaje del colgante es nuevo o un duplicado.
// Especificación en docs/protocolo.md, sección 5.
//
// Lógica pura (sin ESP-IDF ni Arduino): la usa la base al recibir y se prueba en un PC.

#pragma once

#include <stdint.h>

#include "protocolo.h"

namespace duplicados {

// Lo que la base recuerda de cada colgante (en RAM; se pierde si la base se reinicia).
struct Estado {
    bool conocido;    // ya se procesó al menos un mensaje de este colgante
    uint16_t ultima;  // última secuencia procesada
};

// True si el mensaje es nuevo y hay que procesarlo; false si es un duplicado (se responde ACK
// con FLAG_ACK_DUPLICADO y no se procesa). El orden importa:
//   1. Un reintento con la misma secuencia que la última procesada es duplicado, aunque traiga
//      FLAG_ARRANQUE_FRIO: el colgante reenvía el primer mensaje tras arrancar con ese flag si
//      se perdió el ACK, y sin esta regla la base lo procesaría dos veces.
//   2. FLAG_ARRANQUE_FRIO: la secuencia del colgante volvió a empezar; se acepta como nuevo.
//   3. Sin historia de este colgante: nuevo.
//   4. Comparación de 16 bits con signo, que tolera el desborde de 65535 a 0.
constexpr bool esNuevo(const Estado& estado, uint8_t flags, uint16_t secuencia) {
    if (estado.conocido && (flags & FLAG_REINTENTO) != 0 && secuencia == estado.ultima) {
        return false;
    }
    if ((flags & FLAG_ARRANQUE_FRIO) != 0 || !estado.conocido) {
        return true;
    }
    return static_cast<int16_t>(static_cast<uint16_t>(secuencia - estado.ultima)) > 0;
}

// Estado después de recibir un mensaje: solo cambia si el mensaje era nuevo.
constexpr Estado actualizar(const Estado& estado, bool nuevo, uint16_t secuencia) {
    return nuevo ? Estado{true, secuencia} : estado;
}

}  // namespace duplicados
