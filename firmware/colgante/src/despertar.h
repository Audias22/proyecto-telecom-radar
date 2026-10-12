// despertar.h
// Decision de que hacer al arrancar: transmitir ALERTA, HEARTBEAT o nada.
//
// Logica pura, sin dependencias de ESP-IDF ni Arduino. main.cpp lee el motivo de reset, la causa
// del despertar, el GPIO que desperto al equipo y el estado de recuperacion del boton atascado,
// los convierte a estos tipos y llama a decidir(). Asi la decision se puede probar en un PC.

#pragma once

namespace despertar {

enum class Accion {
    NINGUNA,
    ALERTA,
    HEARTBEAT,
};

// Motivo de la decision. main.cpp lo usa solamente para el registro por Serial.
enum class Motivo {
    RECUPERACION_BOTON_ATASCADO,  // wake por timer de recuperacion: radio y sonidos omitidos
    ENCENDIDO,                     // arranque en frio: heartbeat para que la base lo registre
    REINICIO_POR_FALLO,            // watchdog, brownout, panic, causa desconocida: no transmite
    TEMPORIZADOR,                  // wake por timer: heartbeat periodico
    BOTON,                         // wake GPIO causado por el pin del boton
    GPIO_AJENO,                    // wake GPIO que no corresponde al pin del boton
    CAUSA_NO_OPERATIVA,            // cualquier otra causa de despertar
};

// Equivalente host de esp_reset_reason().
enum class Reinicio {
    DEEP_SLEEP,  // despertar normal
    ENCENDIDO,   // encendido, boton EN/RST, reinicio por USB o por software
    OTRO,        // brownout, watchdog, panic o desconocido
};

// Equivalente host de esp_sleep_get_wakeup_cause().
enum class Causa {
    TEMPORIZADOR,
    GPIO,
    OTRA,
};

struct Entradas {
    bool recuperando_boton_atascado;  // energia::recuperandoBotonAtascado()
    Reinicio reinicio;
    Causa causa;
    bool gpio_boton_desperto;  // boton::despertoPorBoton(): el bit de PIN_BOTON en el wake GPIO
};

struct Decision {
    Accion accion;
    Motivo motivo;
};

// No recibe el nivel actual del boton a proposito. El estado del wake GPIO queda retenido por el
// RTC; si la pulsacion termino durante el arranque, exigir que el pin siga bajo descartaria una
// alerta legitima.
//
// Al encender se envia un HEARTBEAT (con FLAG_ARRANQUE_FRIO) para que la base registre el
// colgante de inmediato. Tras un reinicio por fallo no se transmite: con la bateria al limite, el
// pico de corriente del radio podria provocar otro brownout y un ciclo de reinicios.
constexpr Decision decidir(const Entradas& entradas) {
    if (entradas.recuperando_boton_atascado) {
        return {Accion::NINGUNA, Motivo::RECUPERACION_BOTON_ATASCADO};
    }
    if (entradas.reinicio == Reinicio::ENCENDIDO) {
        return {Accion::HEARTBEAT, Motivo::ENCENDIDO};
    }
    if (entradas.reinicio != Reinicio::DEEP_SLEEP) {
        return {Accion::NINGUNA, Motivo::REINICIO_POR_FALLO};
    }
    if (entradas.causa == Causa::TEMPORIZADOR) {
        return {Accion::HEARTBEAT, Motivo::TEMPORIZADOR};
    }
    if (entradas.causa == Causa::GPIO) {
        return entradas.gpio_boton_desperto ? Decision{Accion::ALERTA, Motivo::BOTON}
                                            : Decision{Accion::NINGUNA, Motivo::GPIO_AJENO};
    }
    return {Accion::NINGUNA, Motivo::CAUSA_NO_OPERATIVA};
}

}  // namespace despertar
