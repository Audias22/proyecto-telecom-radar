// boton_silencio.h
// Botón BOOT del XIAO ESP32-C6 (PIN_BOTON_SILENCIO, GPIO9): una pulsación corta silencia la alerta
// local. Lectura sin bloqueo con antirrebote: la pulsación cuenta cuando el nivel bajo se mantiene
// BOTON_ANTIRREBOTE_MS. No hace falta sostenerlo.
//
// Cuidado: GPIO9 es pin de arranque. Si el botón está presionado al reiniciar o al conectar la
// alimentación, el ESP32-C6 entra en modo de carga (bootloader) y el firmware no arranca: la base
// queda sin alerta local hasta el siguiente reinicio con el botón suelto.

#pragma once

namespace boton_silencio {

// Configura el pin como entrada con pull-up (la placa ya tiene R17 de 10 kΩ a 3V3).
void iniciar();

// Lee el botón y aplica el antirrebote. Devuelve true una sola vez por pulsación, en el momento en
// que se confirma. Llamar en cada vuelta de loop().
bool pulsado();

}  // namespace boton_silencio
