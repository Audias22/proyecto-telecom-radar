// peticion_https.h
// Lo común a las peticiones HTTPS de la tarea de red (Firebase y Telegram):
//   - Certificado verificado con el bundle de CA de ESP-IDF, nunca sin verificar.
//   - Límite de cada espera (FIREBASE_TIMEOUT_* en config.h; docs/fallos.md, escenario 5).
//   - Cuerpo de la respuesta leído con un límite total (cuerpo_http.h), nunca con
//     HTTPClient::getString(), que no tiene límite.
//   - Mientras dura, la petición queda anotada en el vigilante (vigilante_red.h) y fuera del
//     watchdog de tareas (watchdog_tareas.h). Al terminar deja una línea de traza
//     (FIREBASE_TRAZA) sin token ni API key.
// Una petición a la vez: empezar(), leerCuerpo() y terminar() solo los llama la tarea de red.
// vigilar() corre en loop().

#pragma once

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <stddef.h>
#include <stdint.h>

#include "vigilante_red.h"

// Una petición HTTPS terminada, para el comando "e".
struct PeticionTerminada {
    char     metodo[8];
    char     ruta[64];
    int      codigo;
    char     texto[112];   // "HTTP 204", "error -11 read Timeout", ...
    uint32_t duracion_ms;
    uint32_t fin_ms;
};

namespace peticion_https {

// Antes de crear la tarea de red. false si no hubo memoria para el candado.
bool iniciar();

void configurarCliente(NetworkClientSecure &cliente);
void configurarHttp(HTTPClient &http);

// Anota la petición en el vigilante y la saca del watchdog. ruta: sin token ni parámetros.
// Devuelve el último error TLS del cliente antes de la petición (para terminar()).
int empezar(const char *metodo, const char *ruta, NetworkClientSecure &cliente);

// Lee el cuerpo con un límite total de FIREBASE_TIMEOUT_CUERPO_MS. Devuelve codigo, o un error de
// HTTPClient si el cuerpo no llegó completo; entonces cierra la conexión.
int leerCuerpo(HTTPClient &http, NetworkClientSecure &cliente, int codigo, String &respuesta);

// Cierra la anotación, describe el resultado en t y deja la traza con el nombre del módulo.
// Devuelve true si el vigilante la había cortado: el cliente se debe recrear.
bool terminar(const char *modulo, int codigo, const String &cuerpo, NetworkClientSecure &cliente,
              int tls_antes, PeticionTerminada &t);

// Texto del error: el de HTTPClient para códigos negativos, o el que manda el servidor
// ("message" o "error" de Firebase, "description" de Telegram).
String textoError(int codigo, const String &cuerpo);

// La tarea de red dio una vuelta fuera de una petición.
void latir();

// Desde loop(), una vez por segundo como mucho (FIREBASE_VIGILANTE_PERIODO_MS). Corta la petición
// colgada y registra los cambios. Con VIGILANTE_REINICIAR deja en motivo la descripción; quien
// llama reinicia.
AccionVigilante vigilar(bool alerta_urgente, char *motivo, size_t tamano);

// Comando "e": petición en curso y cortes del vigilante.
void imprimirEstado();

}  // namespace peticion_https
