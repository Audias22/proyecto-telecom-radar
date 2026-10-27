// Firmware de la base (XIAO ESP32-C6 del kit MR60BHA2).
//
// Lee el radar, recibe al colgante por ESP-NOW (con ACK y filtro de duplicados), maneja el LED y
// el buzzer de la base, se conecta al router por WiFi (BASE_WIFI_HABILITADO) y escribe en Firebase
// y Telegram (firebase_cliente y telegram, en su propia tarea: loop() solo encola).
//
// A Firebase van: el estado cada ESTADO_INTERVALO_MS (lo maneja la tarea de red), una lectura
// cada LECTURA_INTERVALO_MS, /colgantes/{id} en cada mensaje nuevo del colgante y los eventos
// (docs/fallos.md, "Tipos de evento"), que también se avisan por Telegram:
//   - BOTON en cada ALERTA nueva.
//   - BATERIA_BAJA, COLGANTE_AUSENTE y RADAR_SIN_DATOS, una vez por episodio (detectores.h).
//   - BASE_DESCONECTADA lo crea la tarea de red al volver (firebase_cliente.h).
// Un reinicio inesperado (watchdog, brownout, vigilante...) se avisa solo por Telegram.
// loop() está bajo el watchdog de tareas (watchdog_tareas.h).
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
#include "almacen.h"
#include "boton_silencio.h"
#include "config.h"
#include "detectores.h"
#include "espnow_rx.h"
#include "firebase_cliente.h"
#include "motivo_reinicio.h"
#include "radar.h"
#include "red_wifi.h"
#include "watchdog_tareas.h"

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
    Serial.println("#   e  estado: MAC, canal, antena, radar, colgantes, WiFi, Firebase,");
    Serial.println("#      Telegram, peticion en curso, eventos pendientes y cola offline");
    Serial.println("#   r  PRUEBA: simular el radar sin datos (otra vez r para volver)");
    Serial.println("#   p  PRUEBA: bateria del colgante simulada, alterna 3400 y 3900 mV");
    Serial.println("#   w  PRUEBA: colgar loop() para que el watchdog reinicie la base");
    Serial.println("#   h  esta ayuda");
    Serial.println("# Boton BOOT: silencia la alerta local (no presionarlo al reiniciar)");
}

// ---------------------------------------------------------------------------
// Detectores de eventos (detectores.h). Su estado se guarda en NVS para no repetir un aviso tras
// un reinicio.
// ---------------------------------------------------------------------------

static const char *CLAVE_DETECTORES = "detectores";  // almacen.h

static RegistroColgantes colgantes;
static Episodio radar_sin_datos(RADAR_EVENTO_ESPERA_MS, RADAR_RECUPERADO_MS);
static bool radar_guardado = false;  // radar_sin_datos.activo() en la última escritura

static void restaurarDetectores() {
    uint8_t datos[2 + RegistroColgantes::TAMANO_SERIALIZADO];
    const size_t largo = almacen::leer(CLAVE_DETECTORES, datos, sizeof(datos));
    bool radar = false;
    if (largo == 0 || !restaurarEstado(datos, largo, millis(), colgantes, radar)) {
        return;
    }
    radar_sin_datos.restaurar(radar);
    radar_guardado = radar;
    Serial.printf("# Detectores restaurados de NVS: %u colgantes conocidos, radar %s\n",
                  static_cast<unsigned>(colgantes.cantidad()),
                  radar ? "en episodio sin datos" : "normal");
}

// loop() no espera a la flash: deja los datos y los escribe la tarea de red. Sin tarea de red
// (sin Firebase ni Telegram) los escribe aquí; son unos 20 bytes y cambian muy rara vez.
static void guardarDetectores() {
    if (!colgantes.cambio() && radar_sin_datos.activo() == radar_guardado) {
        return;
    }
    uint8_t datos[2 + RegistroColgantes::TAMANO_SERIALIZADO];
    const size_t largo = serializarEstado(colgantes, radar_sin_datos.activo(), datos, sizeof(datos));
    almacen::pedirEscritura(CLAVE_DETECTORES, datos, largo);
    if (!firebase_cliente::tareaActiva()) {
        almacen::escribirPendientes();
    }
    colgantes.limpiarCambio();
    radar_guardado = radar_sin_datos.activo();
}

static void nombreColgante(uint8_t id, char *origen, size_t tamano) {
    snprintf(origen, tamano, "colgante%u", id);
}

// Cualquier mensaje, nuevo o duplicado: el colgante está vivo. BATERIA_BAJA una vez por descarga.
static void revisarColgante(const MensajeColgante &m) {
    const PaqueteEspNow &p = m.paquete;
    const bool baja = (p.flags & FLAG_BATERIA_BAJA) != 0;
    const ResultadoMensaje r = colgantes.mensaje(p.id_dispositivo, baja, p.bateria_mv, m.ms);
    if (r.volvio) {
        Serial.printf("# [%9lu ms] colgante %u: volvio a comunicarse\n", millis(),
                      p.id_dispositivo);
    }
    if (!r.bateria_baja) {
        return;
    }
    char origen[16];
    nombreColgante(p.id_dispositivo, origen, sizeof(origen));
    char detalle[16];
    snprintf(detalle, sizeof(detalle), "%u mV", p.bateria_mv);
    Serial.printf("# [%9lu ms] colgante %u: bateria baja (%s), evento BATERIA_BAJA\n", millis(),
                  p.id_dispositivo, detalle);
    firebase_cliente::encolarEvento(EVENTO_BATERIA_BAJA, origen, detalle);
}

static void revisarAusentes() {
    uint8_t ids[RegistroColgantes::MAXIMO];
    const size_t n = colgantes.revisarAusentes(millis(), ids, RegistroColgantes::MAXIMO);
    for (size_t k = 0; k < n; k++) {
        char duracion[24];
        textoDuracion(COLGANTE_AUSENTE_MS, duracion, sizeof(duracion));
        char detalle[48];
        snprintf(detalle, sizeof(detalle), "sin mensajes por %s", duracion);
        char origen[16];
        nombreColgante(ids[k], origen, sizeof(origen));
        Serial.printf("# [%9lu ms] colgante %u: %s, evento COLGANTE_AUSENTE\n", millis(), ids[k],
                      detalle);
        firebase_cliente::encolarEvento(EVENTO_COLGANTE_AUSENTE, origen, detalle);
    }
}

static void revisarRadarSinDatos() {
    const CambioEpisodio cambio = radar_sin_datos.actualizar(radar::sinDatos(), millis());
    if (cambio == EPISODIO_EMPIEZA) {
        char duracion[24];
        textoDuracion(RADAR_SIN_DATOS_MS + RADAR_EVENTO_ESPERA_MS, duracion, sizeof(duracion));
        char detalle[48];
        snprintf(detalle, sizeof(detalle), "sin datos por %s", duracion);
        Serial.printf("# [%9lu ms] radar %s, evento RADAR_SIN_DATOS\n", millis(), detalle);
        firebase_cliente::encolarEvento(EVENTO_RADAR_SIN_DATOS, nullptr, detalle);
    } else if (cambio == EPISODIO_TERMINA) {
        Serial.printf("# [%9lu ms] radar: %lu s seguidos con datos, termina el episodio sin datos\n",
                      millis(), static_cast<unsigned long>(RADAR_RECUPERADO_MS / 1000));
    }
}

static void atenderDetectores() {
    static uint32_t ultima_ms = 0;
    if (millis() - ultima_ms < DETECTORES_PERIODO_MS) {
        return;
    }
    ultima_ms = millis();
    revisarAusentes();
    revisarRadarSinDatos();
    guardarDetectores();
}

// ---------------------------------------------------------------------------
// Colgante
// ---------------------------------------------------------------------------

static uint32_t total_nuevos = 0;
static uint32_t total_duplicados = 0;
static uint8_t ultimo_colgante = 1;  // id del último colgante que habló (comando "p")

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
        nombreColgante(p.id_dispositivo, origen, sizeof(origen));
        firebase_cliente::encolarEvento(EVENTO_BOTON, origen, "boton del colgante");
    }
}

// Solo los mensajes nuevos disparan la alerta local y van a la nube; un duplicado ya se atendió.
static void atenderMensaje(const MensajeColgante &m) {
    imprimirMensaje(m);
    ultimo_colgante = m.paquete.id_dispositivo;
    revisarColgante(m);
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
// Vigilante de la red
// ---------------------------------------------------------------------------

// Una alerta urgente (BOTON, APNEA) pospone el reinicio del vigilante de la red: la base nunca
// se reinicia con el buzzer sonando. Un aviso (amarillo, sin buzzer) no lo pospone.
static void vigilarRed() {
    const PatronAlerta patron = alerta_local::patronActivo();
    firebase_cliente::vigilar(patron == PATRON_BOTON || patron == PATRON_APNEA);
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
    Serial.printf("# Detectores: %u colgantes conocidos (ausente tras %lu min sin mensajes), "
                  "radar %s\n",
                  static_cast<unsigned>(colgantes.cantidad()),
                  static_cast<unsigned long>(COLGANTE_AUSENTE_MS / 60000),
                  radar_sin_datos.activo() ? "en episodio sin datos" : "normal");
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

// Prueba de BATERIA_BAJA (comando "p"): un mensaje simulado del último colgante que habló pasa por
// el detector de batería, sin radio. Alterna 3400 mV con FLAG_BATERIA_BAJA y 3900 mV sin ella
// (rearma). No escribe /colgantes ni activa la alerta local: solo el evento y su aviso.
static void simularBateria() {
    static bool baja = false;
    baja = !baja;
    MensajeColgante m{};
    m.paquete.id_dispositivo = ultimo_colgante;
    m.paquete.flags = baja ? FLAG_BATERIA_BAJA : 0;
    m.paquete.bateria_mv = baja ? 3400 : 3900;
    m.ms = millis();
    Serial.printf("# PRUEBA: mensaje simulado del colgante %u con %u mV%s\n", ultimo_colgante,
                  m.paquete.bateria_mv, baja ? " y BAT_BAJA" : "");
    revisarColgante(m);
}

// Prueba del watchdog de tareas (comando "w"): loop() deja de volver a propósito. A los
// WDT_TAREAS_TIMEOUT_MS ESP-IDF reinicia la base y el arranque siguiente avisa por Telegram
// "watchdog de tareas". Es el único bloqueo de loop() y solo ocurre con este comando.
static void probarWatchdog() {
    Serial.printf("# PRUEBA: loop() se cuelga; el watchdog reinicia la base en %lu s\n",
                  static_cast<unsigned long>(WDT_TAREAS_TIMEOUT_MS / 1000));
    Serial.flush();
    for (;;) {
        delay(100);
    }
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
        case 'r':
        case 'R':
            radar::simularSinDatos(!radar::simulandoSinDatos());
            Serial.printf("# PRUEBA: radar %s\n", radar::simulandoSinDatos()
                                                      ? "sin datos simulado (r para volver)"
                                                      : "leyendo tramas otra vez");
            break;
        case 'p':
        case 'P':
            simularBateria();
            break;
        case 'w':
        case 'W':
            probarWatchdog();
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
// Arranque: motivo del reinicio (motivo_reinicio.h)
// ---------------------------------------------------------------------------

static void imprimirMotivo(const MotivoReinicio &m) {
    Serial.printf("# Arranque: %s%s\n", m.texto, m.inesperado ? " (reinicio inesperado)" : "");
}

// Después de firebase_cliente::iniciar(): va a Telegram (las reglas no aceptan este evento).
static void avisarReinicio(const MotivoReinicio &m) {
    if (m.inesperado) {
        firebase_cliente::encolarEvento(EVENTO_REINICIO, nullptr, m.texto);
    }
}

// ---------------------------------------------------------------------------

void setup() {
    seleccionarAntena();
    Serial.begin(115200);
    esperarMonitor();

    Serial.printf("# %s v%s (protocolo v%d)\n", FIRMWARE_NOMBRE, FIRMWARE_VERSION, PROTOCOLO_VERSION);
    const MotivoReinicio motivo = motivo_reinicio::leer();
    imprimirMotivo(motivo);
    watchdog_tareas::iniciar();
    alerta_local::iniciar();
    boton_silencio::iniciar();
    almacen::iniciar();
    restaurarDetectores();
    // Primero la radio, después ESP-NOW (necesita la radio arrancada). La conexión al router
    // empieza en el primer red_wifi::actualizar() de loop().
    if (!red_wifi::iniciar() || !espnow_rx::iniciar()) {
        // La base sigue con el radar y la alerta local; el error queda a la vista en el LED.
        Serial.println("# ERROR: no se pudo iniciar WiFi/ESP-NOW; el colgante no podra comunicarse");
        alerta_local::activar(PATRON_AVISO);
    }
    firebase_cliente::iniciar();  // la tarea de red espera sola a que haya WiFi
    avisarReinicio(motivo);
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
    atenderDetectores();
    leerComandos();
    atenderBoton();
    alerta_local::actualizar();
    vigilarRed();
}

#endif  // DIAGNOSTICO
