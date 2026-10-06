// alerta_local.h
// Buzzer y LED de la base. Funciona sin red: es la alerta que nunca depende de WiFi ni Firebase.

#pragma once

enum PatronAlerta {
    PATRON_NINGUNO,
    PATRON_BOTON,      // alerta manual desde el colgante
    PATRON_APNEA,      // alerta automática del radar
    PATRON_AVISO,      // avisos no urgentes (batería baja, sin red)
};

namespace alerta_local {

// TODO: configurar PIN_BUZZER y PIN_LED de config.h.
void iniciar();

// TODO: arrancar un patrón. Uno urgente reemplaza a uno de menor prioridad.
void activar(PatronAlerta patron);

void silenciar();

// TODO: avanzar el patrón sin delay(). Llamar en cada vuelta de loop().
void actualizar();

}  // namespace alerta_local
