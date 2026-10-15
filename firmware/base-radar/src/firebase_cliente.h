// firebase_cliente.h
// Escritura en Firebase Realtime Database por la API REST sobre HTTPS (docs/modelo-datos.md).
//
// Toda la red corre en una tarea FreeRTOS propia (TAREA_RED_* en config.h). loop() solo llama a
// encolar*(), que copian la petición a una cola acotada (cola_peticiones.h) y vuelven enseguida:
// loop() nunca espera a la red. La alerta local y el ACK al colgante no pasan por aquí.
//
// La tarea:
//   - Inicia sesión con DISPOSITIVO_EMAIL / DISPOSITIVO_CLAVE (Firebase Auth, signInWithPassword),
//     renueva el idToken con el refresh token (securetoken) antes de que venza y vuelve a iniciar
//     sesión ante un 401.
//   - Cada ESTADO_INTERVALO_MS escribe /dispositivos/{BASE_ID}/estado y lee de vuelta
//     ultimo_contacto: es la referencia de hora del servidor para las claves de /lecturas.
//   - Envía lo que haya en la cola: eventos, colgantes y lecturas, en ese orden.
//   - HTTPS con verificación del certificado (bundle de CA de ESP-IDF), nunca sin verificar.
// Solo trabaja con el WiFi conectado (red_wifi). Escribe con ?print=silent para no gastar
// descarga, salvo la lectura de ultimo_contacto.

#pragma once

#include <stdint.h>

#include "formato_nube.h"

namespace firebase_cliente {

// Arranca la tarea de red. No bloquea. Con FIREBASE_HABILITADO 0, o con secrets.h todavía con
// los valores de ejemplo, no arranca nada y encolar*() no hacen nada.
void iniciar();

// Una lectura para /lecturas/{BASE_ID}/{ts}. La clave ts se calcula con millis_medicion al
// enviarla.
void encolarLectura(const LecturaNube &lectura);

// Un mensaje NUEVO del colgante: /colgantes/{id} con bateria_mv, ultimo_heartbeat, rssi y base.
void encolarColgante(uint8_t id, uint16_t bateria_mv, int8_t rssi);

// Un evento para /eventos (POST). origen: BASE_ID o "colganteN". detalle puede ser vacío.
void encolarEvento(TipoEvento tipo, const char *origen, const char *detalle);

// Comando "e": sesión, última escritura correcta, errores, cola y hora del servidor.
void imprimirEstado();

}  // namespace firebase_cliente
