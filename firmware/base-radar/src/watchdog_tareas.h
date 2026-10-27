// watchdog_tareas.h
// Watchdog de tareas de ESP-IDF (TWDT) sobre loop() y la tarea de red (docs/fallos.md,
// escenario 10). Si una tarea suscrita no avisa en WDT_TAREAS_TIMEOUT_MS (config.h), ESP-IDF hace
// panic y la base se reinicia; el arranque siguiente lo informa (motivo_reinicio.h).
//
//   - loop(): el núcleo de Arduino lo alimenta en cada vuelta (enableLoopWDT()). loop() nunca
//     bloquea, así que solo vence si algo lo cuelga.
//   - Tarea de red: se alimenta en cada vuelta y sale del watchdog mientras hace una petición
//     HTTPS (peticion_https.h), que puede durar hasta ~39 s en DNS, TLS o esperas del servidor.
//     Ese tramo lo vigila vigilante_red, que corta la petición y solo reinicia sin una alerta
//     local urgente activa. El watchdog cubre el resto: la cola, NVS y el bucle de la tarea.

#pragma once

namespace watchdog_tareas {

// En setup(): ajusta el tiempo del TWDT (ESP-IDF lo arranca con 5 s) y suscribe loop().
void iniciar();

// Desde la tarea que lo llama.
void suscribirEstaTarea();
void alimentar();
void pausarEstaTarea();    // antes de una petición HTTPS
void reanudarEstaTarea();  // al terminarla

}  // namespace watchdog_tareas
