// Firmware de la base (XIAO ESP32-C6 del kit MR60BHA2).
//
// Lee el radar, recibe al colgante por ESP-NOW (con ACK y filtro de duplicados), maneja el LED y
// el buzzer de la base, se conecta al router por WiFi (BASE_WIFI_HABILITADO) y escribe en Firebase
// (firebase_cliente, en su propia tarea: loop() solo encola). Todavía sin Telegram.
//
// A Firebase van: el estado cada ESTADO_INTERVALO_MS (lo maneja la tarea de red), una lectura
// cada LECTURA_INTERVALO_MS, /colgantes/{id} en cada mensaje nuevo del colgante y un evento BOTON
// en cada ALERTA nueva.
//
// Salida por serial. Todos los registros empiezan con '#', que analisis/capturar_serial.py
// trata como comentario:
//   - Por defecto: un registro por mensaje del colgante y por cambio de estado del radar.
//   - Con BASE_SALIDA_CSV 1 (config.h), además una línea CSV por lectura del radar:
//       Encabezado: ms,valida,presencia,distancia_cm,resp_rpm,latido_bpm,fase_total,fase_resp,fase_latido
//       Datos: valida y presencia como 0/1; valores no recibidos como "nan".
//
// Comandos por el monitor serial (una letra y Enter): ver ayuda().
//
// Con -DDIAGNOSTICO (entorno "diagnostico") se compila src/diagnostico.cpp en su lugar.

#ifndef DIAGNOSTICO

#include <Arduino.h>
#include <protocolo.h>

#include "alerta_local.h"
#include "boton_silencio.h"
#include "config.h"
#include "espnow_rx.h"
#include "firebase_cliente.h"
#include "radar.h"
#include "red_wifi.h"

static const char *ENCABEZADO_CSV =
    "ms,valida,presencia,distancia_cm,resp_rpm,latido_bpm,fase_total,fase_resp,fase_latido";

// ---------------------------------------------------------------------------
// Arranque
// ---------------------------------------------------------------------------

// Elige la antena antes de iniciar WiFi. initVariant() ya dejó WIFI_ENABLE (GPIO3) en bajo, que
// habilita el conmutador de RF, y WIFI_ANT_CONFIG (GPIO14) en bajo (antena de chip).
static void seleccionarAntena() {
    pinMode(WIFI_ENABLE, OUTPUT);
    digitalWrite(WIFI_ENABLE, LOW);
    pinMode(WIFI_ANT_CONFIG, OUTPUT);
    digitalWrite(WIFI_ANT_CONFIG, BASE_ANTENA_EXTERNA ? HIGH : LOW);
}

static void esperarMonitor() {
    // Con USB CDC el puerto tarda en aparecer; se espera un poco sin bloquear si no hay PC.
    unsigned long inicio = millis();
    while (!Serial && millis() - inicio < 2000) {
        delay(10);
    }
}

static void imprimirMac() {
    uint8_t mac[6];
    espnow_rx::macPropia(mac);
    Serial.printf("# MAC STA: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3],
                  mac[4], mac[5]);
    Serial.println("# Para include/secrets.h del colgante:");
    Serial.printf("#define BASE_MAC  { 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X }\n", mac[0],
                  mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void ayuda() {
    Serial.println("# Comandos (letra y Enter):");
    Serial.println("#   b  buzzer fijo 10 s (medir el voltaje en D10)");
    Serial.println("#   s  silenciar la alerta local");
    Serial.println("#   e  estado: MAC, canal, antena, radar, WiFi, Firebase y contadores");
    Serial.println("#   h  esta ayuda");
    Serial.println("# Boton BOOT: silencia la alerta local (no presionarlo al reiniciar)");
}

// ---------------------------------------------------------------------------
// Colgante
// ---------------------------------------------------------------------------

static uint32_t total_nuevos = 0;
static uint32_t total_duplicados = 0;

static void imprimirBateria(uint16_t bateria_mv) {
    if (bateria_mv == 0) {
        Serial.print("bat=sin dato ");
    } else {
        Serial.printf("bat=%u mV ", bateria_mv);
    }
}

static void imprimirMensaje(const MensajeColgante &m) {
    const PaqueteEspNow &p = m.paquete;
    char flags[64];
    espnow_rx::textoFlags(p.flags, flags, sizeof(flags));
    Serial.printf("# [%9lu ms] %02X:%02X:%02X:%02X:%02X:%02X %-9s id=%u seq=%u flags=%s ",
                  static_cast<unsigned long>(m.ms), m.mac[0], m.mac[1], m.mac[2], m.mac[3], m.mac[4],
                  m.mac[5], espnow_rx::nombreTipo(p.tipo), p.id_dispositivo, p.secuencia, flags);
    imprimirBateria(p.bateria_mv);
    Serial.printf("rssi=%d dBm canal=%u %s ack=%s\n", m.rssi, m.canal,
                  m.duplicado ? "DUPLICADO" : "NUEVO", m.ack_ok ? "OK" : "ERROR");
}

// La alerta local va primero y no depende de la red; la nube solo recibe una copia en la cola.
static void enviarANube(const MensajeColgante &m) {
    const PaqueteEspNow &p = m.paquete;
    firebase_cliente::encolarColgante(p.id_dispositivo, p.bateria_mv, m.rssi);
    if (p.tipo == MSG_ALERTA) {
        char origen[16];
        snprintf(origen, sizeof(origen), "colgante%u", p.id_dispositivo);
        firebase_cliente::encolarEvento(EVENTO_BOTON, origen, "boton del colgante");
    }
}

// Solo los mensajes nuevos disparan la alerta local y van a la nube; un duplicado ya se atendió.
static void atenderMensaje(const MensajeColgante &m) {
    imprimirMensaje(m);
    if (m.duplicado) {
        total_duplicados++;
        return;
    }
    total_nuevos++;
    if (m.paquete.tipo == MSG_ALERTA) {
        alerta_local::activar(PATRON_BOTON);
    } else if ((m.paquete.flags & FLAG_BATERIA_BAJA) != 0) {
        alerta_local::activar(PATRON_AVISO);
    }
    enviarANube(m);
}

static void atenderColgante() {
    MensajeColgante mensaje;
    while (espnow_rx::siguienteMensaje(mensaje)) {
        atenderMensaje(mensaje);
    }
}

// ---------------------------------------------------------------------------
// Radar
// ---------------------------------------------------------------------------

static void imprimirLectura(const LecturaRadar &l) {
    Serial.printf("%lu,%d,%d,%.1f,%.2f,%.2f,%.6f,%.6f,%.6f\n",
                  (unsigned long)l.millis_medicion, l.valida ? 1 : 0, l.presencia ? 1 : 0,
                  l.distancia_cm, l.resp_rpm, l.latido_bpm,
                  l.fase_total, l.fase_resp, l.fase_latido);
}

static void revisarSinDatos() {
    static bool sin_datos_antes = false;
    const bool sin_datos = radar::sinDatos();
    if (sin_datos == sin_datos_antes) {
        return;
    }
    Serial.printf("# [%9lu ms] radar %s (reinicios de UART: %lu)\n", millis(),
                  sin_datos ? "sin datos" : "con datos", (unsigned long)radar::reiniciosUart());
    if (sin_datos) {
        alerta_local::activar(PATRON_AVISO);
    }
    sin_datos_antes = sin_datos;
}

// Los tres estados de docs/radar.md, 13.6 (el tercero, "radar sin datos", sale en
// revisarSinDatos()). "Sin persona detectada" no es "cama vacía".
enum EstadoCama : uint8_t { CAMA_DESCONOCIDO, CAMA_PERSONA, CAMA_SIN_OBJETIVO, CAMA_ACTIVIDAD_BAJA };

static EstadoCama estadoCama(const EstadoPersona &e) {
    if (e.persona) {
        return CAMA_PERSONA;
    }
    return e.objetivo ? CAMA_ACTIVIDAD_BAJA : CAMA_SIN_OBJETIVO;
}

static void imprimirPersona(const EstadoPersona &e) {
    if (e.persona) {
        Serial.printf("persona detectada (actividad %.3f)", e.actividad);
    } else if (e.objetivo) {
        Serial.printf("sin persona detectada (actividad %.3f < %.2f)", e.actividad,
                      RADAR_UMBRAL_PERSONA);
    } else {
        Serial.print("sin persona detectada (radar sin objetivo)");
    }
}

// Registra los cambios de estado, evaluando la ventana de 30 s una vez por segundo.
static void revisarPersona() {
    static uint32_t ultima_ms = 0;
    static EstadoCama antes = CAMA_DESCONOCIDO;
    if (millis() - ultima_ms < 1000) {
        return;
    }
    ultima_ms = millis();
    const EstadoPersona e = radar::estadoPersona();
    if (estadoCama(e) == antes) {
        return;
    }
    antes = estadoCama(e);
    Serial.printf("# [%9lu ms] radar: ", millis());
    imprimirPersona(e);
    Serial.println();
}

// Valores del módulo de la última trama: NAN si no llegaron. formato_nube omite los que no
// corresponden (sin objetivo, 0 o fuera de rango).
static LecturaNube armarLecturaNube() {
    const EstadoPersona e = radar::estadoPersona();
    const LecturaRadar u = radar::ultimaLectura();
    LecturaNube l;
    l.millis_medicion = millis();
    l.persona = e.persona;
    l.objetivo = e.objetivo;
    l.actividad = e.objetivo ? e.actividad : NAN;
    l.distancia_cm = e.objetivo ? u.distancia_cm : NAN;
    l.resp_rpm = u.resp_rpm;
    l.latido_bpm = u.latido_bpm;
    return l;
}

// Una lectura cada LECTURA_INTERVALO_MS. Con el radar sin datos no se escribe nada: el hueco en
// /lecturas es la señal (docs/modelo-datos.md, 1.1).
static void enviarLecturaPeriodica() {
    static uint32_t ultima_ms = 0;
    if (millis() - ultima_ms < LECTURA_INTERVALO_MS) {
        return;
    }
    ultima_ms = millis();
    if (radar::sinDatos() || radar::tramasFase() == 0) {
        return;
    }
    firebase_cliente::encolarLectura(armarLecturaNube());
}

static void atenderRadar() {
    radar::actualizar();
    LecturaRadar lectura;
    while (radar::siguienteLectura(lectura)) {
        if (BASE_SALIDA_CSV) {
            imprimirLectura(lectura);
        }
    }
    revisarSinDatos();
    revisarPersona();
    enviarLecturaPeriodica();
}

// ---------------------------------------------------------------------------
// Botón BOOT: silencia la alerta local
// ---------------------------------------------------------------------------

static void atenderBoton() {
    if (!boton_silencio::pulsado()) {
        return;
    }
    alerta_local::silenciar();
    Serial.printf("# [%9lu ms] Alerta local silenciada (boton BOOT)\n", millis());
}

// ---------------------------------------------------------------------------
// Comandos por serial
// ---------------------------------------------------------------------------

static void imprimirEstado() {
    imprimirMac();
    Serial.printf("# Canal %u, antena %s, buzzer %s\n", espnow_rx::canalActual(),
                  BASE_ANTENA_EXTERNA ? "externa (U.FL)" : "de chip",
                  BASE_BUZZER_HABILITADO ? "habilitado" : "deshabilitado");
    Serial.printf("# Colgante: nuevos %lu, duplicados %lu, invalidos %lu, perdidos en cola %lu\n",
                  (unsigned long)total_nuevos, (unsigned long)total_duplicados,
                  (unsigned long)espnow_rx::invalidos(), (unsigned long)espnow_rx::perdidos());
    Serial.printf("# Radar: %s, reinicios de UART %lu, patron de alerta %d\n",
                  radar::sinDatos() ? "sin datos" : "con datos",
                  (unsigned long)radar::reiniciosUart(), (int)alerta_local::patronActivo());
    const EstadoPersona e = radar::estadoPersona();
    Serial.print("# Radar: ");
    imprimirPersona(e);
    Serial.printf(", %u tramas con dato en 30 s; pico de la UART %u B (seguro < %u B)\n",
                  e.muestras, (unsigned)radar::picoBufferUart(), (unsigned)radar::PICO_UART_SEGURO);
    red_wifi::imprimirEstado();
    firebase_cliente::imprimirEstado();
}

static void ejecutarComando(char comando) {
    switch (comando) {
        case 'b':
        case 'B':
            alerta_local::probarBuzzer(PRUEBA_BUZZER_MS);
            Serial.printf("# Buzzer fijo %lu s: medir el voltaje entre D10 y GND\n",
                          (unsigned long)(PRUEBA_BUZZER_MS / 1000));
            if (!BASE_BUZZER_HABILITADO) {
                Serial.println("# Aviso: BASE_BUZZER_HABILITADO es 0, el buzzer no va a sonar");
            }
            break;
        case 's':
        case 'S':
            alerta_local::silenciar();
            Serial.println("# Alerta local silenciada");
            break;
        case 'e':
        case 'E':
            imprimirEstado();
            break;
        default:
            ayuda();
            break;
    }
}

// Lee lo que haya en el buffer sin esperar. Se ejecuta la primera letra de cada línea.
static void leerComandos() {
    static bool inicio_linea = true;
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            inicio_linea = true;
        } else if (inicio_linea && c != ' ') {
            ejecutarComando(c);
            inicio_linea = false;
        }
    }
}

// ---------------------------------------------------------------------------

void setup() {
    seleccionarAntena();
    Serial.begin(115200);
    esperarMonitor();

    Serial.printf("# %s v%s (protocolo v%d)\n", FIRMWARE_NOMBRE, FIRMWARE_VERSION, PROTOCOLO_VERSION);
    alerta_local::iniciar();
    boton_silencio::iniciar();
    // Primero la radio, después ESP-NOW (necesita la radio arrancada). La conexión al router
    // empieza en el primer red_wifi::actualizar() de loop().
    if (!red_wifi::iniciar() || !espnow_rx::iniciar()) {
        // La base sigue con el radar y la alerta local; el error queda a la vista en el LED.
        Serial.println("# ERROR: no se pudo iniciar WiFi/ESP-NOW; el colgante no podra comunicarse");
        alerta_local::activar(PATRON_AVISO);
    }
    firebase_cliente::iniciar();  // la tarea de red espera sola a que haya WiFi
    imprimirEstado();
    ayuda();
    if (BASE_SALIDA_CSV) {
        Serial.println(ENCABEZADO_CSV);
    }

    radar::iniciar();
}

void loop() {
    red_wifi::actualizar();
    atenderColgante();
    atenderRadar();
    leerComandos();
    atenderBoton();
    alerta_local::actualizar();
}

#endif  // DIAGNOSTICO
