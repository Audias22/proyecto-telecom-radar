// red_wifi.h
// Radio WiFi de la base: modo STA, conexión al router y reconexión con espera creciente.
//
// Orden de arranque (verificado en arduino-esp32 3.3.7 / ESP-IDF):
//   1. red_wifi::iniciar(): WiFi.mode(WIFI_STA) arranca la radio (esp_wifi_start). Sin ahorro de
//      energía y sin la reconexión automática del framework.
//   2. espnow_rx::iniciar(): esp_now_init() necesita la radio ya arrancada.
//   3. red_wifi::actualizar() en loop(): el primer llamado hace WiFi.begin(). No bloquea.
//
// Con BASE_WIFI_HABILITADO 1 la base no fija canal: queda en el del router. Los peers de ESP-NOW
// se agregan con channel 0 ("el canal actual de la interfaz", esp_now.h), así que siguen
// funcionando cuando el canal cambia. Con 0, canal fijo BASE_CANAL_SIN_WIFI y nunca se conecta.
//
// Registra por serial cada conexión, desconexión, intento fallido y cambio de canal de la radio.

#pragma once

#include <stdint.h>

namespace red_wifi {

// Arranca la radio en modo STA. Devuelve false si no se pudo.
bool iniciar();

// Avanza la conexión: intentos, espera creciente y registro de cambios de canal. No bloquea.
// Llamar en cada vuelta de loop().
void actualizar();

bool conectado();
int8_t rssi();     // dBm del router; 0 si no está conectado
uint8_t canal();   // canal actual de la radio (con o sin conexión)

// Imprime por serial el estado del WiFi (comando "e").
void imprimirEstado();

}  // namespace red_wifi
