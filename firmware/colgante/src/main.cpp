#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <protocolo.h>

#include "bateria.h"
#include "boton.h"
#include "config.h"
#include "energia.h"
#include "espnow_tx.h"

namespace {

enum class AccionDespertar {
    NINGUNA,
    ALERTA,
    HEARTBEAT,
};

void registrar(const char* mensaje) {
    if (REGISTROS_SERIAL_HABILITADOS != 0) {
        Serial.println(mensaje);
    }
}

void iniciarRegistros() {
    if (REGISTROS_SERIAL_HABILITADOS == 0) {
        return;
    }

    Serial.begin(115200);
    const unsigned long inicio = millis();
    while (!Serial && millis() - inicio < REGISTROS_SERIAL_ESPERA_MS) {
        delay(10);
    }

    Serial.printf("%s v%s (protocolo v%d)\n", FIRMWARE_NOMBRE, FIRMWARE_VERSION,
                  PROTOCOLO_VERSION);
}

AccionDespertar determinarAccion() {
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        registrar("main: arranque o reinicio fuera de deep sleep; no se transmite");
        return AccionDespertar::NINGUNA;
    }

    const esp_sleep_wakeup_cause_t causa = esp_sleep_get_wakeup_cause();
    if (causa == ESP_SLEEP_WAKEUP_TIMER) {
        return AccionDespertar::HEARTBEAT;
    }

    if (causa == ESP_SLEEP_WAKEUP_GPIO) {
        if (!boton::despertoPorBoton()) {
            registrar("main: despertar GPIO ajeno a GPIO3; no se transmite");
            return AccionDespertar::NINGUNA;
        }

        // El estado de wake queda retenido por el RTC. Si la pulsacion termino durante el
        // arranque, exigir que el pin siga bajo perderia una alerta legitima. La lectura estable
        // solo distingue si continua presionado; energia::dormir() confirma despues la liberacion
        // antes de volver a habilitar el wake en nivel bajo.
        if (!boton::presionado()) {
            registrar("main: GPIO3 desperto el equipo y ya fue liberado; alerta aceptada");
        }
        return AccionDespertar::ALERTA;
    }

    registrar("main: causa de despertar no operativa; no se transmite");
    return AccionDespertar::NINGUNA;
}

ResultadoEnvio transmitir(TipoMensaje tipo) {
    const uint16_t bateria_mv = bateria::leerMilivoltios();
    if (bateria::modoSimulado()) {
        registrar("main: bateria simulada; el valor no es una medicion fisica");
    } else if (!bateria::lecturaValida(bateria_mv)) {
        registrar("main: lectura de bateria no disponible; se transmite el centinela 0");
    }

    espnow_tx::iniciar();
    const ResultadoEnvio resultado = espnow_tx::enviar(tipo, bateria_mv);
    espnow_tx::detener();
    return resultado;
}

void informarResultado(TipoMensaje tipo, ResultadoEnvio resultado) {
    const bool entregado = resultado == ENVIO_OK || resultado == ENVIO_OK_BARRIDO;

    if (tipo == MSG_HEARTBEAT) {
        registrar(entregado ? "main: heartbeat confirmado" : "main: heartbeat sin ACK");
        return;
    }

    if (entregado) {
        registrar("main: alerta confirmada por ACK de aplicacion");
        energia::beepConfirmacion();
    } else {
        registrar("main: alerta sin ACK de aplicacion");
        energia::beepError();
    }
}

void intentarDormir() {
    energia::dormir();

    // Solo se alcanza si energia::dormir() no pudo configurar o iniciar deep sleep.
    registrar("main: la suspension fallo; se reintentara sin retransmitir ni emitir sonidos");
}

}  // namespace

void setup() {
    iniciarRegistros();
    boton::iniciar();

    if (energia::recuperandoBotonAtascado()) {
        registrar("main: recuperacion de boton atascado; radio y sonidos omitidos");
        intentarDormir();
        return;
    }

    const AccionDespertar accion = determinarAccion();
    if (accion == AccionDespertar::ALERTA) {
        informarResultado(MSG_ALERTA, transmitir(MSG_ALERTA));
    } else if (accion == AccionDespertar::HEARTBEAT) {
        informarResultado(MSG_HEARTBEAT, transmitir(MSG_HEARTBEAT));
    }

    intentarDormir();
}

void loop() {
    // Si deep sleep fallo, nunca se repite el evento procesado. Solo se reintenta dormir con una
    // pausa adicional para evitar un bucle caliente y conservar la posibilidad de recuperacion.
    delay(ENERGIA_ERROR_REINTENTO_MS);
    intentarDormir();
}
