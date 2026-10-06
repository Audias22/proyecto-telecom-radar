#include "bateria.h"

#include <protocolo.h>

namespace bateria {

uint16_t leerMilivoltios() {
    // TODO
    return 0;
}

bool baja(uint16_t milivoltios) {
    return milivoltios < BATERIA_BAJA_MV;
}

}  // namespace bateria
