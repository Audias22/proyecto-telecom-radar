// firebase_cliente.h
// Escritura en Firebase Realtime Database por la API REST sobre HTTPS (docs/modelo-datos.md), y
// dueño de la tarea de red, que también manda los avisos de Telegram (telegram.h).
//
// Toda la red corre en una tarea FreeRTOS propia (TAREA_RED_* en config.h). loop() solo llama a
// encolar*(), que copian la petición a una cola acotada (cola_peticiones.h) y vuelven enseguida:
// loop() nunca espera a la red. La alerta local y el ACK al colgante no pasan por aquí. La tarea
// corre si Firebase o Telegram están configurados; cada uno funciona sin el otro.
//
// La tarea:
//   - Inicia sesión con DISPOSITIVO_EMAIL / DISPOSITIVO_CLAVE (Firebase Auth, signInWithPassword),
//     renueva el idToken con el refresh token (securetoken) antes de que venza y vuelve a iniciar
//     sesión ante un 401.
//   - Cada ESTADO_INTERVALO_MS escribe /dispositivos/{BASE_ID}/estado y lee de vuelta
//     ultimo_contacto: es la referencia de hora del servidor para las claves de /lecturas.
//   - Envía lo que haya en la cola: eventos, colgantes y lecturas, en ese orden. Antes, en cada
//     vuelta, un aviso de Telegram si hay (BOTON primero).
//   - Guarda en NVS los eventos pendientes (almacen.h) y los restaura al arrancar.
//   - Al arrancar y al volver tras un corte compara ultimo_contacto con el anterior: si pasaron
//     BASE_DESCONECTADA_MIN_MS o más, crea el evento BASE_DESCONECTADA con la duración.
//   - HTTPS con verificación del certificado (bundle de CA de ESP-IDF), nunca sin verificar
//     (peticion_https.h).
// Solo trabaja con el WiFi conectado (red_wifi). Escribe con ?print=silent para no gastar
// descarga, salvo la lectura de ultimo_contacto.
//
// Cada espera de la red tiene un límite corto (FIREBASE_TIMEOUT_* en config.h) y el cuerpo de
// la respuesta se lee con cuerpo_http.h, nunca con HTTPClient::getString(). Además, un vigilante
// (vigilante_red.h) que corre desde loop() corta una petición que pasa de
// FIREBASE_PETICION_MAX_MS y, como último recurso, reinicia la base (docs/fallos.md, escenario 5)
// después de guardar los eventos pendientes en NVS. Fuera de las peticiones, la tarea está bajo
// el watchdog de tareas (watchdog_tareas.h). Con FIREBASE_TRAZA cada petición deja una línea por
// serial: método, ruta, código y duración.

#pragma once

#include <stdint.h>

#include "formato_nube.h"

namespace firebase_cliente {

// Revisa Firebase y Telegram (telegram::iniciar()), restaura los eventos guardados en NVS y
// arranca la tarea de red. No espera a la red. Llamar después de almacen::iniciar(). Sin Firebase
// ni Telegram configurados (FIREBASE_HABILITADO 0 o secrets.h con los valores de ejemplo) no
// arranca nada y encolar*() no hacen nada.
void iniciar();

// La tarea de red corre (Firebase, Telegram o los dos).
bool tareaActiva();

// Una lectura para /lecturas/{BASE_ID}/{ts}. La clave ts se calcula con millis_medicion al
// enviarla.
void encolarLectura(const LecturaNube &lectura);

// Un mensaje NUEVO del colgante: /colgantes/{id} con bateria_mv, ultimo_heartbeat, rssi y base.
void encolarColgante(uint8_t id, uint16_t bateria_mv, int8_t rssi);

// Un evento para /eventos (POST) y para Telegram. origen: "colganteN", o nullptr para la base
// (BASE_ID). detalle puede ser vacío. EVENTO_REINICIO va solo a Telegram.
void encolarEvento(TipoEvento tipo, const char *origen, const char *detalle);

// Vigilante de la tarea de red. Llamar en cada vuelta de loop(); revisa una vez por segundo y
// no bloquea. alerta_urgente: hay una alerta local urgente activa, que pospone el reinicio.
void vigilar(bool alerta_urgente);

// Comando "e": sesión, última escritura correcta, errores (también los de inicio de sesión),
// última petición, último error con su texto, hora del servidor, Telegram, petición en curso y
// cuánto lleva, vigilante, eventos pendientes, cola offline y memoria.
void imprimirEstado();

}  // namespace firebase_cliente
