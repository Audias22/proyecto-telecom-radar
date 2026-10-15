#include "radar.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Seeed_Arduino_mmWave.h>
#include <math.h>
#include <string.h>

#include "config.h"

namespace radar {

// Tipos de trama del MR60BHA2 (TypeHeartBreath en SEEED_MR60BHA2.h).
static const uint16_t TRAMA_FASES     = static_cast<uint16_t>(TypeHeartBreath::TypeHeartBreathPhase);
static const uint16_t TRAMA_RESP      = static_cast<uint16_t>(TypeHeartBreath::TypeBreathRate);
static const uint16_t TRAMA_LATIDO    = static_cast<uint16_t>(TypeHeartBreath::TypeHeartRate);
static const uint16_t TRAMA_DISTANCIA = static_cast<uint16_t>(TypeHeartBreath::TypeHeartBreathDistance);
static const uint16_t TRAMA_PRESENCIA = static_cast<uint16_t>(TypeHeartBreath::ReportHumanDetection);

// Máximo de tramas procesadas por llamada a actualizar(), para acotar el tiempo en loop().
static const int TRAMAS_POR_LLAMADA = 16;

static const size_t CAPACIDAD_COLA = 32;

// Buffer de recepción de la UART: lo fija la librería en begin() con setRxBufferSize(32 kB)
// (SeeedmmWave.cpp). Con objetivo el radar manda ~74 tramas/s (docs/radar.md, 12.2), unos
// 1.7 kB/s, así que 32 kB son ~19 s de datos (2.8 s aun con la línea a 115200 baudios llena).
// El límite real es otro: cada update() pasa todo lo que hay en la UART a la cola de tramas de la
// librería, que guarda MMWaveMaxQueueSize = 120 tramas y descarta las más viejas (~1.6 s a
// 74 tramas/s, unos 2.8 kB). Por eso loop() no puede quedarse más de ~1.5 s sin llamar a
// actualizar(): la tarea de red corre con la misma prioridad que loop() (config.h, TAREA_RED_*)
// y nunca la bloquea. picoBufferUart() lo confirma en hardware: debe quedar por debajo de
// PICO_UART_SEGURO (radar.h).

// La librería decodifica cada trama en handleType() y la guarda para sus getters. Se sobrescribe
// solo para saber qué tipo llegó y cuándo, y para leer dos datos que los getters no distinguen:
// - isHumanDetected() devuelve false tanto si no hay persona como si no llegó trama nueva.
// - getDistance() devuelve false cuando el radar indica que no hay objetivo.
class MonitorMR60BHA2 : public SEEED_MR60BHA2 {
public:
    uint16_t ultimo_tipo = 0;
    uint32_t ultima_trama_ms = 0;
    uint32_t tramas_totales = 0;

    bool hay_presencia_trama = false;  // el radar mandó al menos una trama de presencia
    bool presencia_trama = false;
    bool hay_objetivo = false;         // bandera de la trama de distancia

    bool handleType(uint16_t tipo, const uint8_t *datos, size_t largo) override {
        bool ok = SEEED_MR60BHA2::handleType(tipo, datos, largo);
        if (!ok) {
            return false;
        }
        ultimo_tipo = tipo;
        ultima_trama_ms = millis();
        tramas_totales++;

        if (tipo == TRAMA_PRESENCIA && largo >= 1) {
            hay_presencia_trama = true;
            presencia_trama = datos[0] != 0;
        } else if (tipo == TRAMA_DISTANCIA && largo >= sizeof(uint32_t)) {
            uint32_t bandera;
            memcpy(&bandera, datos, sizeof(bandera));
            hay_objetivo = bandera != 0;
        }
        return true;
    }
};

static HardwareSerial serial_radar(RADAR_UART_NUM);
static MonitorMR60BHA2 mmwave;

// Último valor recibido de cada tipo de trama. NAN = no recibido o descartado por falta de datos.
static float resp_rpm = NAN;
static float latido_bpm = NAN;
static float distancia_cm = NAN;

static LecturaRadar ultima = {};
static LecturaRadar cola[CAPACIDAD_COLA];
static size_t cola_inicio = 0;
static size_t cola_cantidad = 0;

static VentanaActividad ventana;
static size_t pico_uart = 0;

static uint32_t tramas_fase = 0;
static uint32_t reinicios = 0;
static uint32_t ultimo_intento_ms = 0;
static bool sin_datos = false;

static bool enRango(float valor, float minimo, float maximo) {
    return !isnan(valor) && valor >= minimo && valor <= maximo;
}

static bool presenciaActual() {
    // Se prefiere la trama de presencia; si el firmware del radar no la manda, se usa la bandera
    // de objetivo de la trama de distancia.
    return mmwave.hay_presencia_trama ? mmwave.presencia_trama : mmwave.hay_objetivo;
}

static void encolar(const LecturaRadar &lectura) {
    if (cola_cantidad == CAPACIDAD_COLA) {
        cola_inicio = (cola_inicio + 1) % CAPACIDAD_COLA;  // se descarta la más antigua
        cola_cantidad--;
    }
    cola[(cola_inicio + cola_cantidad) % CAPACIDAD_COLA] = lectura;
    cola_cantidad++;
}

static void armarLectura(float fase_total, float fase_resp, float fase_latido) {
    LecturaRadar l;
    l.millis_medicion = mmwave.ultima_trama_ms;
    l.presencia = presenciaActual();
    l.distancia_cm = distancia_cm;
    l.resp_rpm = resp_rpm;
    l.latido_bpm = latido_bpm;
    l.fase_total = fase_total;
    l.fase_resp = fase_resp;
    l.fase_latido = fase_latido;
    l.valida = enRango(resp_rpm, RADAR_RESP_MIN_RPM, RADAR_RESP_MAX_RPM) &&
               enRango(latido_bpm, RADAR_LATIDO_MIN_BPM, RADAR_LATIDO_MAX_BPM) &&
               enRango(distancia_cm, RADAR_DISTANCIA_MIN_CM, RADAR_DISTANCIA_MAX_CM) &&
               !isnan(fase_total) && !isnan(fase_resp) && !isnan(fase_latido);
    ultima = l;
    ventana.agregar(l.millis_medicion, fase_total);
    tramas_fase++;
    encolar(l);
}

// Copia el valor de la trama recién procesada desde el getter de la librería.
static void leerTrama(uint16_t tipo) {
    if (tipo == TRAMA_FASES) {
        float total, resp, latido;
        if (mmwave.getHeartBreathPhases(total, resp, latido)) {
            armarLectura(total, resp, latido);
        }
    } else if (tipo == TRAMA_RESP) {
        mmwave.getBreathRate(resp_rpm);
    } else if (tipo == TRAMA_LATIDO) {
        mmwave.getHeartRate(latido_bpm);
    } else if (tipo == TRAMA_DISTANCIA) {
        float d;
        distancia_cm = mmwave.getDistance(d) ? d : NAN;
    } else if (tipo == TRAMA_PRESENCIA) {
        mmwave.isHumanDetected();  // limpia la bandera interna; el valor ya se leyó en handleType
    }
}

static void abrirUart() {
    // begin() de la librería no bloquea cuando no se usa pin de reset.
    mmwave.begin(&serial_radar);
}

static void revisarSinDatos() {
    uint32_t ahora = millis();
    if (ahora - mmwave.ultima_trama_ms < RADAR_SIN_DATOS_MS) {
        sin_datos = false;
        return;
    }
    if (!sin_datos) {
        // Los valores viejos ya no describen a la persona.
        resp_rpm = NAN;
        latido_bpm = NAN;
        distancia_cm = NAN;
        sin_datos = true;
    }
    if (ahora - ultimo_intento_ms >= RADAR_SIN_DATOS_MS) {
        serial_radar.end();
        abrirUart();
        ultimo_intento_ms = ahora;
        reinicios++;
    }
}

void iniciar() {
    abrirUart();
    // Se cuenta el arranque como la última trama para dar al radar RADAR_SIN_DATOS_MS de margen.
    mmwave.ultima_trama_ms = millis();
    ultimo_intento_ms = millis();
}

static void medirBufferUart() {
    const size_t esperando = static_cast<size_t>(serial_radar.available());
    if (esperando > pico_uart) {
        pico_uart = esperando;
    }
}

void actualizar() {
    medirBufferUart();
    for (int i = 0; i < TRAMAS_POR_LLAMADA; i++) {
        uint32_t antes = mmwave.tramas_totales;
        // update(0) lee lo que haya en el buffer de la UART sin esperar y procesa una sola trama
        // de la cola de la librería. Con un timeout mayor, la librería hace espera activa.
        mmwave.update(0);
        if (mmwave.tramas_totales == antes) {
            break;  // no había trama completa (o era inválida); el resto en la próxima vuelta
        }
        leerTrama(mmwave.ultimo_tipo);
    }
    revisarSinDatos();
}

bool siguienteLectura(LecturaRadar &lectura) {
    if (cola_cantidad == 0) {
        return false;
    }
    lectura = cola[cola_inicio];
    cola_inicio = (cola_inicio + 1) % CAPACIDAD_COLA;
    cola_cantidad--;
    return true;
}

LecturaRadar ultimaLectura() {
    return ultima;
}

EstadoPersona estadoPersona() {
    return ventana.evaluar(millis());
}

bool sinDatos() {
    return sin_datos;
}

uint32_t reiniciosUart() {
    return reinicios;
}

uint32_t tramasFase() {
    return tramas_fase;
}

uint32_t tramasTotales() {
    return mmwave.tramas_totales;
}

size_t picoBufferUart() {
    return pico_uart;
}

}  // namespace radar
