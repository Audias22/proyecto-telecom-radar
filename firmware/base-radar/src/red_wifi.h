// red_wifi.h
// Conexión WiFi en modo STA y reconexión automática.

#pragma once

#include <stdint.h>

namespace red_wifi {

// TODO: conectar con WIFI_SSID / WIFI_CLAVE de secrets.h. Desactivar el ahorro de energía para
// no perder tramas ESP-NOW.
void iniciar();

// TODO: vigilar la conexión y reintentar con espera creciente si se cae. No debe bloquear loop().
void actualizar();

bool conectado();
int8_t rssi();
uint8_t canal();

}  // namespace red_wifi
