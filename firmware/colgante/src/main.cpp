#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <protocolo.h>

#include "bateria.h"
#include "boton.h"
#include "config.h"
#include "despertar.h"
#include "energia.h"
#include "espnow_tx.h"

namespace {

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

    // Solo se espera al monitor USB en un arranque en frio. Al despertar de deep sleep, sin PC
    // conectada la espera duraria siempre REGISTROS_SERIAL_ESPERA_MS y retrasaria la alerta.
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        const unsigned long inicio = millis();
        while (!Serial && millis() - inicio < REGISTROS_SERIAL_ESPERA_MS) {
            delay(10);
        }
    }

    Serial.printf("%s v%s (protocolo v%d)\n", FIRMWARE_NOMBRE, FIRMWARE_VERSION,
                  PROTOCOLO_VERSION);
}

despertar::Reinicio leerReinicio() {
    switch (esp_reset_reason()) {
        case ESP_RST_DEEPSLEEP:
            return despertar::Reinicio::DEEP_SLEEP;
        case ESP_RST_POWERON:
        case ESP_RST_EXT:
        case ESP_RST_SW:
        case ESP_RST_USB:
        case ESP_RST_JTAG:
            return despertar::Reinicio::ENCENDIDO;
        default:
            return despertar::Reinicio::OTRO;
    }
}

despertar::Causa leerCausaDespertar() {
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER:
            return despertar::Causa::TEMPORIZADOR;
        case ESP_SLEEP_WAKEUP_GPIO:
            return despertar::Causa::GPIO;
        default:
            return despertar::Causa::OTRA;
    }
}

// Lee el hardware y delega la decision en despertar::decidir(), que es pura y se prueba en host.
// recuperandoBotonAtascado() se consulta primero, igual que antes, porque limpia su estado RTC.
despertar::Decision determinarAccion() {
    despertar::Entradas entradas{};
    entradas.recuperando_boton_atascado = energia::recuperandoBotonAtascado();
    entradas.reinicio = leerReinicio();
    entradas.causa = leerCausaDespertar();
    entradas.gpio_boton_desperto = boton::despertoPorBoton();
    return despertar::decidir(entradas);
}

void registrarDecision(despertar::Motivo motivo) {
    switch (motivo) {
        case despertar::Motivo::RECUPERACION_BOTON_ATASCADO:
            registrar("main: recuperacion de boton atascado; radio y sonidos omitidos");
            break;
        case despertar::Motivo::ENCENDIDO:
            registrar("main: arranque en frio; heartbeat para registrarse en la base");
            break;
        case despertar::Motivo::REINICIO_POR_FALLO:
            if (REGISTROS_SERIAL_HABILITADOS != 0) {
                Serial.printf("main: reinicio por fallo (motivo %d); no se transmite\n",
                              static_cast<int>(esp_reset_reason()));
            }
            break;
        case despertar::Motivo::GPIO_AJENO:
            registrar("main: despertar GPIO ajeno a GPIO3; no se transmite");
            break;
        case despertar::Motivo::BOTON:
            // Solo informativo: la alerta ya fue aceptada por el wake GPIO retenido en el RTC.
            // energia::dormir() confirma despues la liberacion antes de rehabilitar el wake.
            if (!boton::presionado()) {
                registrar("main: GPIO3 desperto el equipo y ya fue liberado; alerta aceptada");
            }
            break;
        case despertar::Motivo::CAUSA_NO_OPERATIVA:
            registrar("main: causa de despertar no operativa; no se transmite");
            break;
        case despertar::Motivo::TEMPORIZADOR:
            break;
    }
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
    energia::iniciarBuzzer();
    iniciarRegistros();
    boton::iniciar();

    const despertar::Decision decision = determinarAccion();
    registrarDecision(decision.motivo);

    if (decision.accion == despertar::Accion::ALERTA) {
        informarResultado(MSG_ALERTA, transmitir(MSG_ALERTA));
    } else if (decision.accion == despertar::Accion::HEARTBEAT) {
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
