// test_logica.cpp
// Pruebas unitarias en host (PC) de la logica pura del colgante: protocolo, paquetes, ACK,
// canales, bateria y decision de despertar (ALERTA / HEARTBEAT / ninguna).
//
// Incluye los headers reales del firmware y del protocolo compartido; no duplica su logica.
// No requiere ESP32, base fisica, Arduino ni WiFi. No prueba radio, ADC, RTC ni deep sleep.
// Ver pruebas_host/README.md para compilar y ejecutar.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <protocolo.h>

#include "bateria.h"
#include "despertar.h"
#include "espnow_tx.h"

namespace {

int verificaciones = 0;
int fallos_verificacion = 0;
int casos = 0;
int casos_fallidos = 0;
bool caso_actual_ok = true;

#define VERIFICAR(condicion)                                                        \
    do {                                                                            \
        ++verificaciones;                                                           \
        if (!(condicion)) {                                                         \
            ++fallos_verificacion;                                                  \
            caso_actual_ok = false;                                                 \
            std::printf("    FALLO %s:%d: %s\n", __FILE__, __LINE__, #condicion);   \
        }                                                                           \
    } while (0)

#define VERIFICAR_IGUAL(obtenido, esperado)                                         \
    do {                                                                            \
        ++verificaciones;                                                           \
        const unsigned long o_ = static_cast<unsigned long>(obtenido);              \
        const unsigned long e_ = static_cast<unsigned long>(esperado);              \
        if (o_ != e_) {                                                             \
            ++fallos_verificacion;                                                  \
            caso_actual_ok = false;                                                 \
            std::printf("    FALLO %s:%d: %s == %lu, esperado %lu\n", __FILE__,     \
                        __LINE__, #obtenido, o_, e_);                               \
        }                                                                           \
    } while (0)

void ejecutar(const char* nombre, void (*caso)()) {
    ++casos;
    caso_actual_ok = true;
    caso();
    if (!caso_actual_ok) {
        ++casos_fallidos;
    }
    std::printf("[%s] %s\n", caso_actual_ok ? " OK  " : "FALLO", nombre);
}

using espnow_tx::logica::ackValido;
using espnow_tx::logica::canalValido;
using espnow_tx::logica::construirPaquete;
using espnow_tx::logica::prepararFlags;

constexpr uint8_t ID = 2;
constexpr uint8_t FLAGS_COLGANTE =
    FLAG_BATERIA_BAJA | FLAG_REINTENTO | FLAG_BARRIDO | FLAG_ARRANQUE_FRIO;

PaqueteEspNow ack(uint8_t version, uint8_t tipo, uint8_t id, uint8_t flags, uint16_t secuencia,
                  uint16_t bateria_mv = 0) {
    return {version, tipo, id, flags, secuencia, bateria_mv};
}

void aBytes(const PaqueteEspNow& p, uint8_t (&bytes)[8]) {
    std::memcpy(bytes, &p, sizeof(bytes));
}

// --------------------------------------------------------------------------
// 1. Tamano y formato binario
// --------------------------------------------------------------------------

void tamanoYDesplazamientos() {
    VERIFICAR_IGUAL(sizeof(PaqueteEspNow), 8);
    VERIFICAR_IGUAL(offsetof(PaqueteEspNow, version), 0);
    VERIFICAR_IGUAL(offsetof(PaqueteEspNow, tipo), 1);
    VERIFICAR_IGUAL(offsetof(PaqueteEspNow, id_dispositivo), 2);
    VERIFICAR_IGUAL(offsetof(PaqueteEspNow, flags), 3);
    VERIFICAR_IGUAL(offsetof(PaqueteEspNow, secuencia), 4);
    VERIFICAR_IGUAL(offsetof(PaqueteEspNow, bateria_mv), 6);
}

void bytesEjemploAlertaDocumentado() {
    // docs/protocolo.md: alerta del colgante 2, secuencia 300, 3950 mV, sin flags.
    const PaqueteEspNow p = construirPaquete(MSG_ALERTA, 2, 3950, 300, false);
    const uint8_t esperado[8] = {0x01, 0x01, 0x02, 0x00, 0x2C, 0x01, 0x6E, 0x0F};
    uint8_t bytes[8];
    aBytes(p, bytes);
    VERIFICAR(std::memcmp(bytes, esperado, sizeof(esperado)) == 0);
}

void bytesEjemploAckDocumentadoEsValido() {
    // docs/protocolo.md: ACK correspondiente 01 03 02 00 2C 01 00 00.
    const uint8_t recibido[8] = {0x01, 0x03, 0x02, 0x00, 0x2C, 0x01, 0x00, 0x00};
    PaqueteEspNow p{};
    std::memcpy(&p, recibido, sizeof(p));
    VERIFICAR_IGUAL(p.secuencia, 300);
    VERIFICAR(ackValido(p, 2, 300));
}

// --------------------------------------------------------------------------
// 2. Construccion: version, tipo, identificador y secuencia
// --------------------------------------------------------------------------

void construccionCamposAlerta() {
    const PaqueteEspNow p = construirPaquete(MSG_ALERTA, 7, 3900, 42, false);
    VERIFICAR_IGUAL(p.version, PROTOCOLO_VERSION);
    VERIFICAR_IGUAL(p.version, 1);
    VERIFICAR_IGUAL(p.tipo, MSG_ALERTA);
    VERIFICAR_IGUAL(p.tipo, 1);
    VERIFICAR_IGUAL(p.id_dispositivo, 7);
    VERIFICAR_IGUAL(p.secuencia, 42);
    VERIFICAR_IGUAL(p.bateria_mv, 3900);
    VERIFICAR_IGUAL(p.flags, 0);
}

void construccionCamposHeartbeat() {
    const PaqueteEspNow p = construirPaquete(MSG_HEARTBEAT, 255, 4200, 1, false);
    VERIFICAR_IGUAL(p.version, PROTOCOLO_VERSION);
    VERIFICAR_IGUAL(p.tipo, MSG_HEARTBEAT);
    VERIFICAR_IGUAL(p.tipo, 2);
    VERIFICAR_IGUAL(p.id_dispositivo, 255);
    VERIFICAR_IGUAL(p.secuencia, 1);
    VERIFICAR_IGUAL(p.bateria_mv, 4200);
}

void construccionSecuenciasLimite() {
    const uint16_t secuencias[] = {0, 1, 0x00FF, 0x0100, 0x7FFF, 0x8000, 0xFFFE, 0xFFFF};
    for (uint16_t s : secuencias) {
        const PaqueteEspNow p = construirPaquete(MSG_ALERTA, ID, 3900, s, false);
        VERIFICAR_IGUAL(p.secuencia, s);
        uint8_t bytes[8];
        aBytes(p, bytes);
        VERIFICAR_IGUAL(bytes[4], s & 0xFFu);         // little-endian
        VERIFICAR_IGUAL(bytes[5], (s >> 8) & 0xFFu);
    }
}

void construccionNoMarcaReintentoNiBarrido() {
    const PaqueteEspNow p = construirPaquete(MSG_ALERTA, ID, 0, 9, true);
    VERIFICAR_IGUAL(p.flags & FLAG_REINTENTO, 0);
    VERIFICAR_IGUAL(p.flags & FLAG_BARRIDO, 0);
    VERIFICAR_IGUAL(p.flags & ~FLAGS_COLGANTE & 0xFFu, 0);  // bits 4-7 reservados en 0
}

// --------------------------------------------------------------------------
// 3. Bateria: clasificacion y flag
// --------------------------------------------------------------------------

void bateriaClasificacion() {
    VERIFICAR_IGUAL(BATERIA_BAJA_MV, 3500);
    VERIFICAR(bateria::logica::esBaja(0));
    VERIFICAR(bateria::logica::esBaja(3499));
    VERIFICAR(!bateria::logica::esBaja(3500));
    VERIFICAR(!bateria::logica::esBaja(3900));
}

void bateriaFlagEnPaquete() {
    struct Caso {
        uint16_t mv;
        bool baja;
    };
    const Caso casos_bateria[] = {{0, true}, {3499, true}, {3500, false}, {3900, false}};
    for (const Caso& c : casos_bateria) {
        const PaqueteEspNow p = construirPaquete(MSG_HEARTBEAT, ID, c.mv, 5, false);
        VERIFICAR_IGUAL((p.flags & FLAG_BATERIA_BAJA) != 0, c.baja);
        VERIFICAR_IGUAL(p.bateria_mv, c.mv);
        // El flag de bateria es independiente del calculo con bateria::logica::esBaja.
        VERIFICAR_IGUAL((p.flags & FLAG_BATERIA_BAJA) != 0, bateria::logica::esBaja(c.mv));
    }
}

void bateriaCentinelaCero() {
    // 0 mV es el centinela de lectura invalida: no es una medicion valida y se marca bajo
    // por seguridad (firmware/colgante/README.md).
    VERIFICAR_IGUAL(BATERIA_LECTURA_INVALIDA_MV, 0);
    VERIFICAR(!bateria::logica::lecturaBateriaValida(0));
    VERIFICAR(bateria::logica::lecturaBateriaValida(3499));
    VERIFICAR(bateria::logica::lecturaBateriaValida(3900));
}

void bateriaConversionAdc() {
    VERIFICAR(bateria::logica::lecturaAdcValida(0));
    VERIFICAR(bateria::logica::lecturaAdcValida(BATERIA_ADC_LIMITE_MV));
    VERIFICAR(!bateria::logica::lecturaAdcValida(BATERIA_ADC_LIMITE_MV + 1));
    VERIFICAR_IGUAL(bateria::logica::convertirAdcABateria(0), 0);
    VERIFICAR_IGUAL(bateria::logica::convertirAdcABateria(1343), 4200);
    VERIFICAR(bateria::logica::convertirAdcABateria(BATERIA_ADC_LIMITE_MV) <= UINT16_MAX);
}

// --------------------------------------------------------------------------
// 4. Flags de arranque en frio, reintento y barrido
// --------------------------------------------------------------------------

void flagArranqueFrio() {
    const PaqueteEspNow frio = construirPaquete(MSG_ALERTA, ID, 3900, 1, true);
    const PaqueteEspNow normal = construirPaquete(MSG_ALERTA, ID, 3900, 1, false);
    VERIFICAR_IGUAL(frio.flags, FLAG_ARRANQUE_FRIO);
    VERIFICAR_IGUAL(normal.flags, 0);
    const PaqueteEspNow frio_bajo = construirPaquete(MSG_ALERTA, ID, 3499, 1, true);
    VERIFICAR_IGUAL(frio_bajo.flags, FLAG_ARRANQUE_FRIO | FLAG_BATERIA_BAJA);
}

void flagsValoresDeBits() {
    VERIFICAR_IGUAL(FLAG_BATERIA_BAJA, 0x01);
    VERIFICAR_IGUAL(FLAG_REINTENTO, 0x02);
    VERIFICAR_IGUAL(FLAG_BARRIDO, 0x04);
    VERIFICAR_IGUAL(FLAG_ARRANQUE_FRIO, 0x08);
    VERIFICAR_IGUAL(FLAG_ACK_DUPLICADO, 0x01);
}

void prepararFlagsCombinaciones() {
    VERIFICAR_IGUAL(prepararFlags(0, false, false), 0);
    VERIFICAR_IGUAL(prepararFlags(0, true, false), FLAG_REINTENTO);
    VERIFICAR_IGUAL(prepararFlags(0, false, true), FLAG_BARRIDO);
    VERIFICAR_IGUAL(prepararFlags(0, true, true), FLAG_REINTENTO | FLAG_BARRIDO);
}

void prepararFlagsConservaFlagsPrevios() {
    const uint8_t base = FLAG_BATERIA_BAJA | FLAG_ARRANQUE_FRIO;
    VERIFICAR_IGUAL(prepararFlags(base, false, false), base);
    VERIFICAR_IGUAL(prepararFlags(base, true, true), base | FLAG_REINTENTO | FLAG_BARRIDO);
    // Idempotente: aplicarlo dos veces no cambia el resultado.
    const uint8_t una = prepararFlags(base, true, false);
    VERIFICAR_IGUAL(prepararFlags(una, true, false), una);
}

// --------------------------------------------------------------------------
// 5. Retransmisiones: misma secuencia
// --------------------------------------------------------------------------

// Reproduce la composicion de cada intento tal como la hace enviarIntentos() en espnow_tx.cpp:
// copia del original y flags = prepararFlags(original.flags, intento > 0 || barrido, barrido).
// No ejecuta enviarIntentos() (depende de esp_now_send y FreeRTOS); verifica sus piezas puras.
PaqueteEspNow intento(const PaqueteEspNow& original, uint8_t numero, bool barrido) {
    PaqueteEspNow p = original;
    p.flags = prepararFlags(p.flags, numero > 0 || barrido, barrido);
    return p;
}

void retransmisionCanalGuardadoReutilizaSecuencia() {
    const PaqueteEspNow original = construirPaquete(MSG_ALERTA, ID, 3499, 0x1234, true);
    for (uint8_t n = 0; n < ACK_REINTENTOS; ++n) {
        const PaqueteEspNow p = intento(original, n, false);
        VERIFICAR_IGUAL(p.secuencia, original.secuencia);
        VERIFICAR_IGUAL(p.version, original.version);
        VERIFICAR_IGUAL(p.tipo, original.tipo);
        VERIFICAR_IGUAL(p.id_dispositivo, original.id_dispositivo);
        VERIFICAR_IGUAL(p.bateria_mv, original.bateria_mv);
        VERIFICAR_IGUAL((p.flags & FLAG_REINTENTO) != 0, n > 0);
        VERIFICAR_IGUAL(p.flags & FLAG_BARRIDO, 0);
        VERIFICAR_IGUAL(p.flags & (FLAG_BATERIA_BAJA | FLAG_ARRANQUE_FRIO),
                        FLAG_BATERIA_BAJA | FLAG_ARRANQUE_FRIO);
        // El ACK de la secuencia original confirma cualquier retransmision.
        VERIFICAR(ackValido(ack(PROTOCOLO_VERSION, MSG_ACK, ID, 0, original.secuencia), ID,
                            p.secuencia));
    }
}

void retransmisionBarridoReutilizaSecuencia() {
    const PaqueteEspNow original = construirPaquete(MSG_HEARTBEAT, ID, 3900, 0xFFFF, false);
    unsigned enviados = 0;
    for (uint8_t canal = CANAL_MIN; canal <= CANAL_MAX; ++canal) {
        for (uint8_t n = 0; n < ACK_REINTENTOS_BARRIDO; ++n) {
            const PaqueteEspNow p = intento(original, n, true);
            VERIFICAR_IGUAL(p.secuencia, 0xFFFF);
            VERIFICAR_IGUAL(p.flags, FLAG_REINTENTO | FLAG_BARRIDO);
            ++enviados;
        }
    }
    VERIFICAR_IGUAL(enviados, (CANAL_MAX - CANAL_MIN + 1) * ACK_REINTENTOS_BARRIDO);
}

// --------------------------------------------------------------------------
// 6. Aceptacion y rechazo de ACK
// --------------------------------------------------------------------------

void ackAceptado() {
    VERIFICAR(ackValido(ack(PROTOCOLO_VERSION, MSG_ACK, ID, 0, 300), ID, 300));
    VERIFICAR(ackValido(ack(PROTOCOLO_VERSION, MSG_ACK, ID, FLAG_ACK_DUPLICADO, 300), ID, 300));
    VERIFICAR(ackValido(ack(PROTOCOLO_VERSION, MSG_ACK, 255, 0, 0xFFFF), 255, 0xFFFF));
    VERIFICAR(ackValido(ack(PROTOCOLO_VERSION, MSG_ACK, ID, 0, 0), ID, 0));
}

void ackRechazadoPorVersion() {
    VERIFICAR(!ackValido(ack(0, MSG_ACK, ID, 0, 300), ID, 300));
    VERIFICAR(!ackValido(ack(PROTOCOLO_VERSION + 1, MSG_ACK, ID, 0, 300), ID, 300));
    VERIFICAR(!ackValido(ack(0xFF, MSG_ACK, ID, 0, 300), ID, 300));
}

void ackRechazadoPorTipo() {
    VERIFICAR(!ackValido(ack(PROTOCOLO_VERSION, MSG_ALERTA, ID, 0, 300), ID, 300));
    VERIFICAR(!ackValido(ack(PROTOCOLO_VERSION, MSG_HEARTBEAT, ID, 0, 300), ID, 300));
    VERIFICAR(!ackValido(ack(PROTOCOLO_VERSION, 0, ID, 0, 300), ID, 300));
    VERIFICAR(!ackValido(ack(PROTOCOLO_VERSION, 4, ID, 0, 300), ID, 300));
    // Un eco del propio paquete transmitido no se acepta como ACK.
    const PaqueteEspNow eco = construirPaquete(MSG_ALERTA, ID, 3900, 300, false);
    VERIFICAR(!ackValido(eco, ID, 300));
}

void ackRechazadoPorIdentificador() {
    VERIFICAR(!ackValido(ack(PROTOCOLO_VERSION, MSG_ACK, ID + 1, 0, 300), ID, 300));
    VERIFICAR(!ackValido(ack(PROTOCOLO_VERSION, MSG_ACK, 0, 0, 300), ID, 300));
}

void ackRechazadoPorSecuencia() {
    VERIFICAR(!ackValido(ack(PROTOCOLO_VERSION, MSG_ACK, ID, 0, 299), ID, 300));
    VERIFICAR(!ackValido(ack(PROTOCOLO_VERSION, MSG_ACK, ID, 0, 301), ID, 300));
    VERIFICAR(!ackValido(ack(PROTOCOLO_VERSION, MSG_ACK, ID, 0, 0), ID, 0xFFFF));
    // Bytes intercambiados (error de endianness) no se aceptan.
    VERIFICAR(!ackValido(ack(PROTOCOLO_VERSION, MSG_ACK, ID, 0, 0x2C01), ID, 0x012C));
}

void ackComportamientoObservadoCamposNoValidados() {
    // Comportamiento actual documentado por la prueba: ackValido() no revisa bateria_mv ni
    // los bits reservados de flags. El protocolo pide 0 / reservados en 0, pero no exige
    // descartarlos. Si se decide endurecer la validacion, esta prueba debe actualizarse.
    VERIFICAR(ackValido(ack(PROTOCOLO_VERSION, MSG_ACK, ID, 0, 300, 3900), ID, 300));
    VERIFICAR(ackValido(ack(PROTOCOLO_VERSION, MSG_ACK, ID, 0xF0, 300), ID, 300));
}

// --------------------------------------------------------------------------
// 7. Canales
// --------------------------------------------------------------------------

void canalesLimites() {
    VERIFICAR_IGUAL(CANAL_MIN, 1);
    VERIFICAR_IGUAL(CANAL_MAX, 13);
    VERIFICAR(!canalValido(0));
    VERIFICAR(canalValido(1));
    VERIFICAR(canalValido(13));
    VERIFICAR(!canalValido(14));
    VERIFICAR(!canalValido(255));
    VERIFICAR(canalValido(CANAL_POR_DEFECTO));
}

void canalesRecorridoCompleto() {
    unsigned validos = 0;
    for (unsigned c = 0; c <= 255; ++c) {
        const bool esperado = c >= 1 && c <= 13;
        VERIFICAR_IGUAL(canalValido(static_cast<uint8_t>(c)), esperado);
        validos += canalValido(static_cast<uint8_t>(c)) ? 1u : 0u;
    }
    VERIFICAR_IGUAL(validos, 13);
}

void tiempoPeorCasoDocumentado() {
    // docs/protocolo.md: 3 x 100 ms + 13 canales x 2 x 50 ms = 1.6 s (sin arranque de WiFi).
    const unsigned peor_caso_ms =
        ACK_REINTENTOS * ACK_TIMEOUT_MS +
        (CANAL_MAX - CANAL_MIN + 1) * ACK_REINTENTOS_BARRIDO * ACK_TIMEOUT_BARRIDO_MS;
    VERIFICAR_IGUAL(peor_caso_ms, 1600);
}

// --------------------------------------------------------------------------
// 8. Decision de despertar (despertar::decidir)
// --------------------------------------------------------------------------

using despertar::Accion;
using despertar::Causa;
using despertar::Decision;
using despertar::Entradas;
using despertar::Motivo;
using despertar::Reinicio;

constexpr Entradas entradas(bool recuperando, Reinicio reinicio, Causa causa, bool boton) {
    return {recuperando, reinicio, causa, boton};
}

// La decision es constexpr: los casos principales tambien se comprueban al compilar.
static_assert(despertar::decidir(entradas(false, Reinicio::DEEP_SLEEP, Causa::GPIO, true)).accion ==
                  Accion::ALERTA,
              "Wake GPIO del boton debe producir ALERTA");
static_assert(despertar::decidir(entradas(false, Reinicio::DEEP_SLEEP, Causa::TEMPORIZADOR, false))
                      .accion == Accion::HEARTBEAT,
              "Wake por timer debe producir HEARTBEAT");

void verificarDecision(const Entradas& e, Accion accion, Motivo motivo) {
    const Decision d = despertar::decidir(e);
    VERIFICAR_IGUAL(static_cast<int>(d.accion), static_cast<int>(accion));
    VERIFICAR_IGUAL(static_cast<int>(d.motivo), static_cast<int>(motivo));
}

void decisionAlertaPorBoton() {
    verificarDecision(entradas(false, Reinicio::DEEP_SLEEP, Causa::GPIO, true), Accion::ALERTA,
                      Motivo::BOTON);
}

void decisionPulsacionBreveYaLiberada() {
    // Pulsacion breve: el boton ya se solto cuando main evalua, pero el RTC retuvo el bit de
    // GPIO3 en el estado de wake. Entradas no tiene campo de nivel actual del boton, por lo que
    // la liberacion no puede descartar la alerta.
    const Entradas breve = entradas(false, Reinicio::DEEP_SLEEP, Causa::GPIO, true);
    verificarDecision(breve, Accion::ALERTA, Motivo::BOTON);
}

void decisionHeartbeatPorTemporizador() {
    verificarDecision(entradas(false, Reinicio::DEEP_SLEEP, Causa::TEMPORIZADOR, false),
                      Accion::HEARTBEAT, Motivo::TEMPORIZADOR);
}

void decisionArranqueFrioNoTransmite() {
    // Encendido, reset por boton EN, watchdog, brownout, panic: todo lo que no sea deep sleep.
    verificarDecision(entradas(false, Reinicio::OTRO, Causa::OTRA, false), Accion::NINGUNA,
                      Motivo::ARRANQUE_FUERA_DE_DEEP_SLEEP);
    verificarDecision(entradas(false, Reinicio::OTRO, Causa::GPIO, true), Accion::NINGUNA,
                      Motivo::ARRANQUE_FUERA_DE_DEEP_SLEEP);
    verificarDecision(entradas(false, Reinicio::OTRO, Causa::TEMPORIZADOR, false),
                      Accion::NINGUNA, Motivo::ARRANQUE_FUERA_DE_DEEP_SLEEP);
}

void decisionRecuperacionBotonAtascadoNoTransmite() {
    verificarDecision(entradas(true, Reinicio::DEEP_SLEEP, Causa::TEMPORIZADOR, false),
                      Accion::NINGUNA, Motivo::RECUPERACION_BOTON_ATASCADO);
    // La recuperacion tiene prioridad sobre cualquier otra entrada.
    verificarDecision(entradas(true, Reinicio::DEEP_SLEEP, Causa::GPIO, true), Accion::NINGUNA,
                      Motivo::RECUPERACION_BOTON_ATASCADO);
}

void decisionGpioAjeno() {
    verificarDecision(entradas(false, Reinicio::DEEP_SLEEP, Causa::GPIO, false), Accion::NINGUNA,
                      Motivo::GPIO_AJENO);
}

void decisionCausaNoOperativa() {
    verificarDecision(entradas(false, Reinicio::DEEP_SLEEP, Causa::OTRA, false), Accion::NINGUNA,
                      Motivo::CAUSA_NO_OPERATIVA);
    // El bit del boton sin wake GPIO no basta para alertar.
    verificarDecision(entradas(false, Reinicio::DEEP_SLEEP, Causa::OTRA, true), Accion::NINGUNA,
                      Motivo::CAUSA_NO_OPERATIVA);
}

void decisionValoresDesconocidos() {
    // Valores fuera de los enumeradores (memoria corrupta o un mapeo futuro incompleto) nunca
    // deben producir una transmision.
    const Causa causa_desconocida = static_cast<Causa>(99);
    const Reinicio reinicio_desconocido = static_cast<Reinicio>(77);
    verificarDecision(entradas(false, Reinicio::DEEP_SLEEP, causa_desconocida, true),
                      Accion::NINGUNA, Motivo::CAUSA_NO_OPERATIVA);
    verificarDecision(entradas(false, reinicio_desconocido, Causa::GPIO, true), Accion::NINGUNA,
                      Motivo::ARRANQUE_FUERA_DE_DEEP_SLEEP);
    verificarDecision(entradas(false, reinicio_desconocido, Causa::TEMPORIZADOR, false),
                      Accion::NINGUNA, Motivo::ARRANQUE_FUERA_DE_DEEP_SLEEP);
}

void decisionTablaCompleta() {
    // Especificacion escrita fila por fila (no se recalcula con la logica de produccion):
    // recuperando, reinicio, causa, bit del boton -> accion, motivo.
    struct Fila {
        bool recuperando;
        Reinicio reinicio;
        Causa causa;
        bool boton;
        Accion accion;
        Motivo motivo;
    };
    constexpr Reinicio DS = Reinicio::DEEP_SLEEP;
    constexpr Reinicio OT = Reinicio::OTRO;
    constexpr Causa TMR = Causa::TEMPORIZADOR;
    constexpr Causa GPIO = Causa::GPIO;
    constexpr Causa OTRA = Causa::OTRA;
    constexpr Accion NADA = Accion::NINGUNA;
    constexpr Motivo REC = Motivo::RECUPERACION_BOTON_ATASCADO;
    constexpr Motivo FRIO = Motivo::ARRANQUE_FUERA_DE_DEEP_SLEEP;
    const Fila tabla[] = {
        {true, DS, TMR, false, NADA, REC},
        {true, DS, TMR, true, NADA, REC},
        {true, DS, GPIO, false, NADA, REC},
        {true, DS, GPIO, true, NADA, REC},
        {true, DS, OTRA, false, NADA, REC},
        {true, DS, OTRA, true, NADA, REC},
        {true, OT, TMR, false, NADA, REC},
        {true, OT, TMR, true, NADA, REC},
        {true, OT, GPIO, false, NADA, REC},
        {true, OT, GPIO, true, NADA, REC},
        {true, OT, OTRA, false, NADA, REC},
        {true, OT, OTRA, true, NADA, REC},
        {false, DS, TMR, false, Accion::HEARTBEAT, Motivo::TEMPORIZADOR},
        {false, DS, TMR, true, Accion::HEARTBEAT, Motivo::TEMPORIZADOR},
        {false, DS, GPIO, false, NADA, Motivo::GPIO_AJENO},
        {false, DS, GPIO, true, Accion::ALERTA, Motivo::BOTON},
        {false, DS, OTRA, false, NADA, Motivo::CAUSA_NO_OPERATIVA},
        {false, DS, OTRA, true, NADA, Motivo::CAUSA_NO_OPERATIVA},
        {false, OT, TMR, false, NADA, FRIO},
        {false, OT, TMR, true, NADA, FRIO},
        {false, OT, GPIO, false, NADA, FRIO},
        {false, OT, GPIO, true, NADA, FRIO},
        {false, OT, OTRA, false, NADA, FRIO},
        {false, OT, OTRA, true, NADA, FRIO},
    };
    VERIFICAR_IGUAL(sizeof(tabla) / sizeof(tabla[0]), 2 * 2 * 3 * 2);
    unsigned alertas = 0;
    unsigned heartbeats = 0;
    for (const Fila& f : tabla) {
        verificarDecision(entradas(f.recuperando, f.reinicio, f.causa, f.boton), f.accion,
                          f.motivo);
        alertas += despertar::decidir(entradas(f.recuperando, f.reinicio, f.causa, f.boton))
                               .accion == Accion::ALERTA ? 1u : 0u;
        heartbeats += despertar::decidir(entradas(f.recuperando, f.reinicio, f.causa, f.boton))
                                  .accion == Accion::HEARTBEAT ? 1u : 0u;
    }
    // Solo una combinacion transmite ALERTA y solo dos transmiten HEARTBEAT.
    VERIFICAR_IGUAL(alertas, 1);
    VERIFICAR_IGUAL(heartbeats, 2);
}

}  // namespace

int main() {
    std::printf("Pruebas host del colgante (protocolo v%d)\n", PROTOCOLO_VERSION);

    ejecutar("paquete: tamano 8 bytes y desplazamientos", tamanoYDesplazamientos);
    ejecutar("paquete: bytes del ejemplo ALERTA de docs/protocolo.md", bytesEjemploAlertaDocumentado);
    ejecutar("paquete: ACK del ejemplo de docs/protocolo.md es valido", bytesEjemploAckDocumentadoEsValido);
    ejecutar("construccion: campos de ALERTA", construccionCamposAlerta);
    ejecutar("construccion: campos de HEARTBEAT", construccionCamposHeartbeat);
    ejecutar("construccion: secuencias limite y little-endian", construccionSecuenciasLimite);
    ejecutar("construccion: no marca reintento, barrido ni bits reservados", construccionNoMarcaReintentoNiBarrido);
    ejecutar("bateria: clasificacion 0/3499/3500/3900 mV", bateriaClasificacion);
    ejecutar("bateria: FLAG_BATERIA_BAJA en el paquete", bateriaFlagEnPaquete);
    ejecutar("bateria: centinela 0 mV invalido", bateriaCentinelaCero);
    ejecutar("bateria: limites ADC y conversion del divisor", bateriaConversionAdc);
    ejecutar("flags: arranque en frio", flagArranqueFrio);
    ejecutar("flags: valores de bits", flagsValoresDeBits);
    ejecutar("flags: prepararFlags combinaciones", prepararFlagsCombinaciones);
    ejecutar("flags: prepararFlags conserva flags previos", prepararFlagsConservaFlagsPrevios);
    ejecutar("retransmision: canal guardado reutiliza secuencia", retransmisionCanalGuardadoReutilizaSecuencia);
    ejecutar("retransmision: barrido reutiliza secuencia", retransmisionBarridoReutilizaSecuencia);
    ejecutar("ack: aceptado", ackAceptado);
    ejecutar("ack: rechazado por version", ackRechazadoPorVersion);
    ejecutar("ack: rechazado por tipo", ackRechazadoPorTipo);
    ejecutar("ack: rechazado por identificador", ackRechazadoPorIdentificador);
    ejecutar("ack: rechazado por secuencia", ackRechazadoPorSecuencia);
    ejecutar("ack: campos no validados (comportamiento actual)", ackComportamientoObservadoCamposNoValidados);
    ejecutar("canales: limites 0/1/13/14/255", canalesLimites);
    ejecutar("canales: recorrido 0..255", canalesRecorridoCompleto);
    ejecutar("tiempos: peor caso documentado 1.6 s", tiempoPeorCasoDocumentado);
    ejecutar("despertar: ALERTA por wake GPIO3", decisionAlertaPorBoton);
    ejecutar("despertar: pulsacion breve ya liberada sigue siendo ALERTA", decisionPulsacionBreveYaLiberada);
    ejecutar("despertar: HEARTBEAT por temporizador", decisionHeartbeatPorTemporizador);
    ejecutar("despertar: arranque frio o reinicio no transmite", decisionArranqueFrioNoTransmite);
    ejecutar("despertar: recuperacion de boton atascado no transmite", decisionRecuperacionBotonAtascadoNoTransmite);
    ejecutar("despertar: wake GPIO ajeno no transmite", decisionGpioAjeno);
    ejecutar("despertar: causa no operativa no transmite", decisionCausaNoOperativa);
    ejecutar("despertar: valores de enum desconocidos no transmiten", decisionValoresDesconocidos);
    ejecutar("despertar: tabla completa de 24 combinaciones", decisionTablaCompleta);

    std::printf("\nCasos: %d, fallidos: %d. Verificaciones: %d, fallidas: %d\n", casos,
                casos_fallidos, verificaciones, fallos_verificacion);
    std::printf("%s\n", casos_fallidos == 0 ? "RESULTADO: OK" : "RESULTADO: FALLO");
    return casos_fallidos == 0 ? 0 : 1;
}
