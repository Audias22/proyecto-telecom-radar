// alerta_local.h
// Buzzer y LED de la base. Funciona sin red: es la alerta que nunca depende de WiFi ni Firebase.
//
// LED: WS2812 de la placa del kit (PIN_LED). Buzzer: PIN_BUZZER, activo en alto.
//   PATRON_NINGUNO: verde tenue fijo, "en funcionamiento".
//   PATRON_BOTON:   rojo y buzzer intermitentes durante ALERTA_LOCAL_DURACION_MS.
//   PATRON_APNEA:   igual que PATRON_BOTON. Queda definido, pero el radar no lo dispara: la apnea
//                   por radar no se usa como alarma (docs/radar.md, sección 13.6).
//   PATRON_AVISO:   amarillo intermitente, sin buzzer, durante AVISO_LOCAL_DURACION_MS.
// Al terminar un patrón se vuelve al verde tenue.

#pragma once

#include <stdint.h>

enum PatronAlerta {
    PATRON_NINGUNO,
    PATRON_BOTON,      // alerta manual desde el colgante
    PATRON_APNEA,      // alerta automática del radar (sin uso, ver arriba)
    PATRON_AVISO,      // avisos no urgentes (batería baja, radar sin datos, sin red)
};

namespace alerta_local {

// Configura el buzzer (apagado, fuerza de salida máxima) y enciende el LED en verde tenue.
void iniciar();

// Arranca un patrón. Uno urgente (BOTON, APNEA) reemplaza a un AVISO; un AVISO no interrumpe a
// uno urgente. Activar de nuevo el mismo patrón reinicia su duración.
void activar(PatronAlerta patron);

// Termina el patrón activo y la prueba del buzzer.
void silenciar();

// Buzzer fijo durante duracion_ms, para medir el voltaje en D10 bajo carga (comando "b").
void probarBuzzer(uint32_t duracion_ms);

PatronAlerta patronActivo();

// Avanza el patrón sin delay(). Llamar en cada vuelta de loop().
void actualizar();

}  // namespace alerta_local
