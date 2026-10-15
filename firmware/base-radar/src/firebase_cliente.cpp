#include "firebase_cliente.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include <cstdarg>
#include <cstring>

#include "cola_peticiones.h"
#include "config.h"
#include "cuerpo_http.h"
#include "red_wifi.h"
#include "vigilante_red.h"

// Las credenciales solo hacen falta con Firebase habilitado (red_wifi.cpp ya da el #error si
// falta secrets.h). En el firmware de diagnóstico o en la fase local no se compila la red.
#if FIREBASE_HABILITADO && !defined(DIAGNOSTICO) && __has_include("secrets.h")
#include "secrets.h"
#define NUBE_COMPILADA 1
#else
#define NUBE_COMPILADA 0
#endif

#ifndef FIREBASE_URL
#define FIREBASE_URL      ""
#define FIREBASE_API_KEY  ""
#define DISPOSITIVO_EMAIL ""
#define DISPOSITIVO_CLAVE ""
#define BASE_ID           ""
#endif

// Bundle de certificados raíz que trae ESP-IDF dentro de libmbedtls.a de arduino-esp32 3.3.7
// (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL: la lista de Mozilla, ~69 kB en flash). Incluye
// las raíces de Google Trust Services que usan googleapis.com y firebaseio.com.
extern const uint8_t bundle_ca_inicio[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t bundle_ca_fin[] asm("_binary_x509_crt_bundle_end");

namespace firebase_cliente {

namespace {

const char *URL_INICIO_SESION =
    "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword";
const char *URL_RENOVAR_TOKEN = "https://securetoken.googleapis.com/v1/token";

// Código propio: el 401 pidió iniciar sesión de nuevo y no se pudo (sin red, clave mala).
constexpr int CODIGO_SIN_SESION = -100;

// Cabeceras de la respuesta que se guardan: el lector del cuerpo necesita saber si es chunked.
const char *CABECERAS[] = {"Transfer-Encoding"};

// Motivo del último reinicio del vigilante. RTC_NOINIT sobrevive a esp_restart(), así el
// arranque siguiente lo muestra por serial.
constexpr uint32_t MARCA_REINICIO = 0x56524544;  // "VRED"
RTC_NOINIT_ATTR uint32_t reinicio_marca;
RTC_NOINIT_ATTR char reinicio_motivo[112];

enum Resultado : uint8_t {
    RESULTADO_OK,
    RESULTADO_REINTENTAR,  // red o servidor: queda en la cola y se reintenta con espera
    RESULTADO_RECHAZADA,   // las reglas o el formato la rechazan: reintentar no sirve
};

// ---------------------------------------------------------------------------
// Registro por serial ("# [ms] firebase: ..."), desde las dos tareas
// ---------------------------------------------------------------------------

void registrar(const char *formato, ...) {
    char texto[224];
    va_list args;
    va_start(args, formato);
    vsnprintf(texto, sizeof(texto), formato, args);
    va_end(args);
    Serial.printf("# [%9lu ms] firebase: %s\n", millis(), texto);
}

// ---------------------------------------------------------------------------
// Estado compartido entre loop() y la tarea de red. Protegido por `candado`, que nunca se tiene
// tomado durante una petición: solo para copiar datos.
// ---------------------------------------------------------------------------

// Una petición HTTPS terminada, para el comando "e".
struct PeticionTerminada {
    char     metodo[8];
    char     ruta[64];
    int      codigo;
    char     texto[112];   // "HTTP 204", "error -11 read Timeout", ...
    uint32_t duracion_ms;
    uint32_t fin_ms;
};

struct Resumen {
    bool     sesion;
    char     uid[48];
    uint32_t inicios_sesion;
    uint32_t renovaciones;
    uint32_t escrituras_ok;
    uint32_t errores;
    uint32_t rechazadas;
    bool     hubo_ok;
    uint32_t ultima_ok_ms;
    bool     hubo_peticion;
    PeticionTerminada ultima;
    bool     hubo_error;
    PeticionTerminada ultimo_error;  // la última que terminó en error de red o HTTP >= 400
    bool     hora_ok;
    uint64_t hora_ref_ms;   // ultimo_contacto leído de vuelta
    uint32_t millis_ref;    // millis() al leerlo
};

SemaphoreHandle_t candado = nullptr;
ColaPeticiones cola;
Resumen resumen = {};
TaskHandle_t tarea = nullptr;
const char *motivo_inactiva = "FIREBASE_HABILITADO 0 en config.h";
VigilanteRed vigilante;
NetworkClientSecure *cliente_en_uso = nullptr;  // el de la petición en curso (para cortarla)

class Bloqueo {
public:
    Bloqueo() { xSemaphoreTake(candado, portMAX_DELAY); }
    ~Bloqueo() { xSemaphoreGive(candado); }
};

bool activa() {
    return candado != nullptr;
}

// ---------------------------------------------------------------------------
// Solo la tarea de red
// ---------------------------------------------------------------------------

struct Sesion {
    char     id_token[2048];   // JWT de ~1 kB
    char     renovacion[1024]; // refresh token
    uint32_t emitido_ms;
    uint32_t dura_ms;
    bool     activa;
};

Sesion sesion = {};
String url_base;
NetworkClientSecure *cliente_bd = nullptr;  // conexión a la base de datos, con keep-alive
HTTPClient *http_bd = nullptr;
uint32_t ultimo_estado_ms = 0;
bool estado_enviado = false;
uint32_t ultimo_uso_bd_ms = 0;  // fin de la última petición por cliente_bd
uint32_t espera_error_ms = 0;   // 0 = sin error pendiente
uint32_t marca_error_ms = 0;
char buffer_respuesta[FIREBASE_RESPUESTA_MAX];

// Límites de config.h (FIREBASE_TIMEOUT_*); qué espera cubre cada uno en docs/fallos.md,
// escenario 5.
void configurarCliente(NetworkClientSecure &cliente) {
    cliente.setCACertBundle(bundle_ca_inicio, static_cast<size_t>(bundle_ca_fin - bundle_ca_inicio));
    cliente.setHandshakeTimeout(FIREBASE_TIMEOUT_TLS_MS / 1000);  // recibe segundos
}

void configurarHttp(HTTPClient &http) {
    // Va al connect() del cliente TLS: select del connect TCP y socket_timeout de ssl_client, que
    // también corta una escritura TLS sin avance.
    http.setConnectTimeout(FIREBASE_TIMEOUT_CONEXION_MS);
    // Tiempo sin recibir nada mientras llegan las cabeceras.
    http.setTimeout(FIREBASE_TIMEOUT_RESPUESTA_MS);
    http.collectHeaders(CABECERAS, sizeof(CABECERAS) / sizeof(CABECERAS[0]));
}

// Texto del error que devuelve Firebase: {"error":"Permission denied"} en la base de datos o
// {"error":{"message":"INVALID_LOGIN_CREDENTIALS",...}} en Auth.
String textoError(int codigo, const String &cuerpo) {
    if (codigo < 0 && codigo != CODIGO_SIN_SESION) {
        return HTTPClient::errorToString(codigo);
    }
    char texto[80];
    if (formato_nube::extraerTexto(cuerpo.c_str(), "message", texto, sizeof(texto)) ||
        formato_nube::extraerTexto(cuerpo.c_str(), "error", texto, sizeof(texto))) {
        return String(texto);
    }
    return codigo == CODIGO_SIN_SESION ? String("sin sesion") : String("");
}

// ---------------------------------------------------------------------------
// Cada petición HTTPS: vigilancia (vigilante_red.h), traza y último error
// ---------------------------------------------------------------------------

// Devuelve el último error TLS del cliente antes de la petición: describir() solo lo muestra si
// cambió, porque ssl_client lo guarda entre conexiones.
int empezarPeticion(const char *metodo, const char *ruta, NetworkClientSecure &cliente) {
    char texto[8];
    const int tls_antes = cliente.lastError(texto, sizeof(texto));
    Bloqueo b;
    vigilante.empezar(metodo, ruta, millis());
    cliente_en_uso = &cliente;
    return tls_antes;
}

// "HTTP 204", "HTTP 401 Permission denied" o "error -11 read Timeout"; si no se pudo conectar,
// con el error TLS de esta conexión.
void describir(int codigo, const String &cuerpo, NetworkClientSecure &cliente, int tls_antes,
               char *texto, size_t tamano) {
    if (codigo > 0) {
        const String detalle = codigo >= 400 ? textoError(codigo, cuerpo) : String();
        snprintf(texto, tamano, "HTTP %d%s%s", codigo, detalle.length() > 0 ? " " : "",
                 detalle.c_str());
        return;
    }
    const String error = textoError(codigo, cuerpo);
    if (codigo != HTTPC_ERROR_CONNECTION_REFUSED) {
        snprintf(texto, tamano, "error %d %s", codigo, error.c_str());
        return;
    }
    char tls[64];
    const int error_tls = cliente.lastError(tls, sizeof(tls));
    if (error_tls == -1 && error_tls != tls_antes) {
        // ssl_client devuelve -1 sin código de mbedTLS: connect TCP o handshake vencidos.
        snprintf(texto, tamano, "error %d sin conexion; TCP o handshake TLS sin respuesta", codigo);
    } else if (error_tls < 0 && error_tls != tls_antes) {
        snprintf(texto, tamano, "error %d sin conexion; TLS -0x%04x %s", codigo, -error_tls, tls);
    } else {
        snprintf(texto, tamano, "error %d sin conexion (DNS, TCP o TLS)", codigo);
    }
}

// Devuelve true si el vigilante la había cortado: el cliente se debe recrear.
bool terminarPeticion(int codigo, const String &cuerpo, NetworkClientSecure &cliente,
                      int tls_antes) {
    PeticionTerminada t = {};
    bool cortada;
    {
        Bloqueo b;
        const PeticionEnCurso &p = vigilante.peticion();
        strlcpy(t.metodo, p.metodo, sizeof(t.metodo));
        strlcpy(t.ruta, p.ruta, sizeof(t.ruta));
        t.fin_ms = millis();
        t.duracion_ms = t.fin_ms - p.inicio_ms;
        cortada = vigilante.terminar(t.fin_ms);
        cliente_en_uso = nullptr;
    }
    t.codigo = codigo;
    describir(codigo, cuerpo, cliente, tls_antes, t.texto, sizeof(t.texto));
    if (cortada) {
        strlcat(t.texto, " (cortada por el vigilante)", sizeof(t.texto));
    }
    const bool fallo = codigo <= 0 || codigo >= 400;
    {
        Bloqueo b;
        resumen.hubo_peticion = true;
        resumen.ultima = t;
        if (fallo) {
            resumen.hubo_error = true;
            resumen.ultimo_error = t;
        }
    }
    if (FIREBASE_TRAZA || cortada) {
        registrar("%s %s -> %s (%lu ms)", t.metodo, t.ruta, t.texto,
                  static_cast<unsigned long>(t.duracion_ms));
    }
    return cortada;
}

// Lee el cuerpo con un límite total de FIREBASE_TIMEOUT_CUERPO_MS, en lugar de
// HTTPClient::getString() (sin límite: cuerpo_http.h). Devuelve codigo, o un error de HTTPClient
// si el cuerpo no llegó completo; entonces cierra la conexión, que ya no se puede reusar.
int leerCuerpo(HTTPClient &http, NetworkClientSecure &cliente, int codigo, String &respuesta) {
    respuesta = String();
    if (codigo <= 0) {
        return codigo;
    }
    const int32_t largo = http.getSize();
    const ModoCuerpo modo = cuerpo_http::modo(
        codigo, largo, http.header("Transfer-Encoding").equalsIgnoreCase("chunked"));
    CuerpoHttp cuerpo(buffer_respuesta, sizeof(buffer_respuesta));
    cuerpo.iniciar(modo, largo);
    uint8_t bloque[256];
    const uint32_t inicio = millis();
    while (!cuerpo.completo() && !cuerpo.invalido() &&
           millis() - inicio < FIREBASE_TIMEOUT_CUERPO_MS) {
        const int disponibles = cliente.available();
        if (disponibles > 0) {
            const size_t pedir = disponibles < static_cast<int>(sizeof(bloque))
                                     ? static_cast<size_t>(disponibles)
                                     : sizeof(bloque);
            const int leidos = cliente.read(bloque, pedir);
            if (leidos > 0) {
                cuerpo.agregar(bloque, static_cast<size_t>(leidos));
                continue;
            }
        }
        if (!cliente.connected()) {
            cuerpo.cerrada();
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    int resultado = codigo;
    if (!cuerpo.completo()) {
        resultado = cuerpo.invalido()      ? HTTPC_ERROR_ENCODING
                    : cliente.connected() ? HTTPC_ERROR_READ_TIMEOUT
                                          : HTTPC_ERROR_CONNECTION_LOST;
    } else if (cuerpo.recortado()) {
        resultado = HTTPC_ERROR_TOO_LESS_RAM;
    } else {
        respuesta = buffer_respuesta;
    }
    if (resultado != codigo || modo == CUERPO_HASTA_CIERRE) {
        cliente.stop();
    }
    return resultado;
}

// ---------------------------------------------------------------------------
// Firebase Auth
// ---------------------------------------------------------------------------

// POST a Firebase Auth con una conexión propia que se cierra al terminar (una vez por hora).
// etiqueta: nombre corto para la traza (la URL lleva la API key).
int pedirAuth(const char *url, const char *etiqueta, const char *tipo, const String &cuerpo,
              String &respuesta) {
    NetworkClientSecure cliente;
    configurarCliente(cliente);
    HTTPClient http;
    configurarHttp(http);
    http.setReuse(false);
    const int tls_antes = empezarPeticion("POST", etiqueta, cliente);
    int codigo = HTTPC_ERROR_CONNECTION_REFUSED;
    respuesta = String();
    if (http.begin(cliente, url)) {
        http.addHeader("Content-Type", tipo);
        codigo = leerCuerpo(http, cliente, http.POST(cuerpo), respuesta);
        http.end();
    }
    terminarPeticion(codigo, respuesta, cliente, tls_antes);  // el cliente es nuevo en cada una
    return codigo;
}

void publicarSesion(const char *uid, bool contar_inicio, bool contar_renovacion) {
    Bloqueo b;
    resumen.sesion = sesion.activa;
    if (uid != nullptr) {
        strlcpy(resumen.uid, uid, sizeof(resumen.uid));
    }
    resumen.inicios_sesion += contar_inicio ? 1 : 0;
    resumen.renovaciones += contar_renovacion ? 1 : 0;
}

bool guardarTokens(const String &respuesta, const char *clave_id, const char *clave_renovacion,
                   const char *clave_duracion) {
    char duracion[16];
    uint64_t segundos = 0;
    const char *json = respuesta.c_str();
    sesion.activa =
        formato_nube::extraerTexto(json, clave_id, sesion.id_token, sizeof(sesion.id_token)) &&
        formato_nube::extraerTexto(json, clave_renovacion, sesion.renovacion,
                                   sizeof(sesion.renovacion)) &&
        formato_nube::extraerTexto(json, clave_duracion, duracion, sizeof(duracion)) &&
        formato_nube::leerEntero(duracion, segundos) && segundos > 0;
    sesion.emitido_ms = millis();
    sesion.dura_ms = static_cast<uint32_t>(segundos * 1000);
    return sesion.activa;
}

bool iniciarSesion() {
    char cuerpo[256];
    formato_nube::jsonInicioSesion(DISPOSITIVO_EMAIL, DISPOSITIVO_CLAVE, cuerpo, sizeof(cuerpo));
    String url = String(URL_INICIO_SESION) + "?key=" + FIREBASE_API_KEY;
    String respuesta;
    const int codigo = pedirAuth(url.c_str(), "auth/signInWithPassword", "application/json",
                                 String(cuerpo), respuesta);
    if (codigo != 200 || !guardarTokens(respuesta, "idToken", "refreshToken", "expiresIn")) {
        sesion.activa = false;
        publicarSesion(nullptr, false, false);
        registrar("no se pudo iniciar sesion con %s: HTTP %d %s", DISPOSITIVO_EMAIL, codigo,
                  textoError(codigo, respuesta).c_str());
        return false;
    }
    char uid[48] = "?";
    formato_nube::extraerTexto(respuesta.c_str(), "localId", uid, sizeof(uid));
    publicarSesion(uid, true, false);
    registrar("sesion iniciada: %s (uid %s), token por %lu s", DISPOSITIVO_EMAIL, uid,
              static_cast<unsigned long>(sesion.dura_ms / 1000));
    return true;
}

bool renovarToken() {
    String cuerpo = String("grant_type=refresh_token&refresh_token=") + sesion.renovacion;
    String url = String(URL_RENOVAR_TOKEN) + "?key=" + FIREBASE_API_KEY;
    String respuesta;
    const int codigo =
        pedirAuth(url.c_str(), "auth/token", "application/x-www-form-urlencoded", cuerpo, respuesta);
    if (codigo != 200 || !guardarTokens(respuesta, "id_token", "refresh_token", "expires_in")) {
        registrar("no se pudo renovar el token (HTTP %d %s); se inicia sesion de nuevo", codigo,
                  textoError(codigo, respuesta).c_str());
        return iniciarSesion();
    }
    publicarSesion(nullptr, false, true);
    registrar("token renovado, vence en %lu s", static_cast<unsigned long>(sesion.dura_ms / 1000));
    return true;
}

bool tokenPorVencer() {
    const uint32_t margen = sesion.dura_ms > FIREBASE_MARGEN_RENOVACION_MS
                                ? sesion.dura_ms - FIREBASE_MARGEN_RENOVACION_MS
                                : sesion.dura_ms / 2;
    return millis() - sesion.emitido_ms >= margen;
}

bool asegurarSesion() {
    if (!sesion.activa) {
        return iniciarSesion();
    }
    return tokenPorVencer() ? renovarToken() : true;
}

// ---------------------------------------------------------------------------
// Realtime Database
// ---------------------------------------------------------------------------

void crearClienteBd() {
    cliente_bd = new NetworkClientSecure();
    configurarCliente(*cliente_bd);
    http_bd = new HTTPClient();
    configurarHttp(*http_bd);
    http_bd->setReuse(true);  // keep-alive: un handshake TLS por conexión, no por petición
}

// Tras un corte del vigilante el cliente pudo quedar a medias (cortado en medio del handshake o
// de un registro TLS): se recrea entero en lugar de reusarlo.
void recrearClienteBd() {
    delete http_bd;  // primero: su destructor todavía usa el cliente
    delete cliente_bd;
    crearClienteBd();
    registrar("cliente TLS de la base de datos recreado");
}

void cerrarConexion() {
    if (cliente_bd != nullptr && cliente_bd->connected()) {
        cliente_bd->stop();
    }
}

int pedirBdUnaVez(const char *metodo, const char *ruta, const String &url, const char *cuerpo,
                  String &respuesta) {
    const int tls_antes = empezarPeticion(metodo, ruta, *cliente_bd);
    int codigo = HTTPC_ERROR_CONNECTION_REFUSED;
    respuesta = String();
    if (http_bd->begin(*cliente_bd, url)) {
        const size_t largo = cuerpo != nullptr ? strlen(cuerpo) : 0;
        codigo = http_bd->sendRequest(
            metodo, reinterpret_cast<uint8_t *>(const_cast<char *>(cuerpo)), largo);
        codigo = leerCuerpo(*http_bd, *cliente_bd, codigo, respuesta);
        http_bd->end();
    }
    if (codigo <= 0) {
        cerrarConexion();  // la próxima petición abre una conexión nueva
    }
    ultimo_uso_bd_ms = millis();
    if (terminarPeticion(codigo, respuesta, *cliente_bd, tls_antes)) {
        recrearClienteBd();
    }
    return codigo;
}

// Una petición REST por la conexión con keep-alive. ruta sin barra inicial ni ".json".
// Una conexión sin usar por FIREBASE_REUSO_MAX_MS se cierra antes. Si falla sobre una conexión
// reutilizada (el servidor pudo cerrarla), se reintenta una vez con una conexión nueva.
int pedirBd(const char *metodo, const char *ruta, const char *cuerpo, bool silencioso,
            String &respuesta) {
    String url = url_base + "/" + ruta + ".json?auth=" + sesion.id_token;
    if (silencioso) {
        url += "&print=silent";  // respuesta 204 sin cuerpo: no cuenta como descarga
    }
    if (millis() - ultimo_uso_bd_ms >= FIREBASE_REUSO_MAX_MS) {
        cerrarConexion();
    }
    const bool reutilizada = cliente_bd->connected();
    const int codigo = pedirBdUnaVez(metodo, ruta, url, cuerpo, respuesta);
    if (codigo > 0 || !reutilizada) {
        return codigo;
    }
    return pedirBdUnaVez(metodo, ruta, url, cuerpo, respuesta);
}

// Ante un 401 (token vencido o revocado; también es la respuesta de las reglas) inicia sesión de
// nuevo y reintenta una vez, como web/simulador/simulador.py.
int pedirBdConSesion(const char *metodo, const char *ruta, const char *cuerpo, bool silencioso,
                     String &respuesta) {
    int codigo = pedirBd(metodo, ruta, cuerpo, silencioso, respuesta);
    if (codigo != 401) {
        return codigo;
    }
    registrar("401 en %s (%s); se inicia sesion de nuevo", ruta,
              textoError(codigo, respuesta).c_str());
    sesion.activa = false;
    if (!iniciarSesion()) {
        return CODIGO_SIN_SESION;
    }
    return pedirBd(metodo, ruta, cuerpo, silencioso, respuesta);
}

Resultado clasificar(int codigo) {
    if (codigo >= 200 && codigo < 300) {
        return RESULTADO_OK;
    }
    if (codigo == 400 || codigo == 401 || codigo == 403 || codigo == 413) {
        return RESULTADO_RECHAZADA;
    }
    return RESULTADO_REINTENTAR;
}

// descartable: false para el estado y la hora, que se reintentan aunque las reglas los rechacen.
void anotarResultado(const char *metodo, const char *ruta, int codigo, const String &respuesta,
                     bool descartable) {
    const Resultado r = clasificar(codigo);
    const bool descartada = descartable && r == RESULTADO_RECHAZADA;
    {
        Bloqueo b;
        if (r == RESULTADO_OK) {
            resumen.escrituras_ok++;
            resumen.hubo_ok = true;
            resumen.ultima_ok_ms = millis();
            return;
        }
        resumen.errores++;
        resumen.rechazadas += descartada ? 1 : 0;
    }
    registrar("ERROR %s %s: HTTP %d %s%s", metodo, ruta, codigo,
              textoError(codigo, respuesta).c_str(),
              descartada ? " (rechazada, se descarta)" : " (se reintenta)");
}

Resultado escribir(const char *metodo, const char *ruta, const char *json,
                   bool descartable = true) {
    String respuesta;
    const int codigo = pedirBdConSesion(metodo, ruta, json, true, respuesta);
    anotarResultado(metodo, ruta, codigo, respuesta, descartable);
    return clasificar(codigo);
}

// ---------------------------------------------------------------------------
// Estado y hora del servidor (docs/modelo-datos.md, sección 2)
// ---------------------------------------------------------------------------

String rutaEstado() {
    return String("dispositivos/") + BASE_ID + "/estado";
}

bool leerHoraServidor() {
    const String ruta = rutaEstado() + "/ultimo_contacto";
    String respuesta;
    const int codigo = pedirBdConSesion("GET", ruta.c_str(), nullptr, false, respuesta);
    const uint32_t ahora = millis();
    uint64_t hora = 0;
    if (codigo != 200 || !formato_nube::leerEntero(respuesta.c_str(), hora)) {
        anotarResultado("GET", ruta.c_str(), codigo == 200 ? 0 : codigo, respuesta, false);
        return false;
    }
    Bloqueo b;
    resumen.hora_ok = true;
    resumen.hora_ref_ms = hora;
    resumen.millis_ref = ahora;
    return true;
}

int rssiWifi() {
    return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
}

Resultado enviarEstado() {
    char json[160];
    formato_nube::jsonEstado(rssiWifi(), FIRMWARE_VERSION, json, sizeof(json));
    const Resultado r = escribir("PATCH", rutaEstado().c_str(), json, false);
    if (r != RESULTADO_OK) {
        return r;
    }
    if (!leerHoraServidor()) {
        return RESULTADO_REINTENTAR;
    }
    estado_enviado = true;
    ultimo_estado_ms = millis();
    return RESULTADO_OK;
}

bool tocaEstado(bool hora_ok) {
    return !hora_ok || !estado_enviado || millis() - ultimo_estado_ms >= ESTADO_INTERVALO_MS;
}

// ---------------------------------------------------------------------------
// Peticiones de la cola
// ---------------------------------------------------------------------------

Resultado enviarEvento(const PeticionEvento &e) {
    char detalle[128];
    const uint32_t retraso_s = (millis() - e.millis_creado) / 1000;
    if (retraso_s >= 10) {
        snprintf(detalle, sizeof(detalle), "%s (enviado %lu s despues)", e.detalle,
                 static_cast<unsigned long>(retraso_s));
    } else {
        strlcpy(detalle, e.detalle, sizeof(detalle));
    }
    char json[320];
    formato_nube::jsonEvento(e.tipo, e.origen, detalle, json, sizeof(json));
    const Resultado r = escribir("POST", "eventos", json);
    if (r == RESULTADO_OK) {
        registrar("evento %s de %s enviado", formato_nube::nombreEvento(e.tipo), e.origen);
    }
    return r;
}

Resultado enviarColgante(const PeticionColgante &c) {
    char ruta[24];
    snprintf(ruta, sizeof(ruta), "colgantes/%u", c.id);
    char json[160];
    formato_nube::jsonColgante(c.bateria_mv, c.rssi, BASE_ID, json, sizeof(json));
    return escribir("PATCH", ruta, json);
}

Resultado enviarLectura(const LecturaNube &l, uint64_t hora_ref_ms, uint32_t millis_ref) {
    const uint64_t ts = formato_nube::millisAHoraServidor(hora_ref_ms, millis_ref, l.millis_medicion);
    char clave[16];
    if (!formato_nube::claveLectura(ts, clave, sizeof(clave))) {
        registrar("lectura descartada: ts %llu no tiene 13 digitos",
                  static_cast<unsigned long long>(ts));
        return RESULTADO_RECHAZADA;
    }
    char ruta[64];
    snprintf(ruta, sizeof(ruta), "lecturas/%s/%s", BASE_ID, clave);
    char json[192];
    const bool diferida = millis() - l.millis_medicion > LECTURA_DIFERIDA_MS;
    formato_nube::jsonLectura(l, diferida, json, sizeof(json));
    return escribir("PUT", ruta, json);
}

Resultado enviar(const Peticion &p, uint64_t hora_ref_ms, uint32_t millis_ref) {
    switch (p.tipo) {
        case PETICION_EVENTO:
            return enviarEvento(p.evento);
        case PETICION_COLGANTE:
            return enviarColgante(p.colgante);
        case PETICION_LECTURA:
            return enviarLectura(p.lectura, hora_ref_ms, millis_ref);
        default:
            return RESULTADO_RECHAZADA;
    }
}

// ---------------------------------------------------------------------------
// Bucle de la tarea
// ---------------------------------------------------------------------------

void anotarFallo() {
    espera_error_ms = espera_error_ms == 0 ? FIREBASE_ESPERA_ERROR_MS : espera_error_ms * 2;
    if (espera_error_ms > FIREBASE_ESPERA_ERROR_MAXIMA_MS) {
        espera_error_ms = FIREBASE_ESPERA_ERROR_MAXIMA_MS;
    }
    marca_error_ms = millis();
}

void atenderResultado(Resultado r) {
    if (r == RESULTADO_REINTENTAR) {
        anotarFallo();
    } else {
        espera_error_ms = 0;
    }
}

bool esperandoTrasError() {
    return espera_error_ms != 0 && millis() - marca_error_ms < espera_error_ms;
}

// Una petición por vuelta: primero eventos, después el estado si toca (da la hora), después
// colgantes y lecturas. Devuelve false si no había nada que hacer.
bool atenderSiguiente() {
    Peticion p;
    bool hay;
    bool hora_ok;
    uint64_t hora_ref_ms;
    uint32_t millis_ref;
    {
        Bloqueo b;
        hora_ok = resumen.hora_ok;
        hora_ref_ms = resumen.hora_ref_ms;
        millis_ref = resumen.millis_ref;
        hay = cola.siguiente(hora_ok, p);
    }
    if (!(hay && p.tipo == PETICION_EVENTO) && tocaEstado(hora_ok)) {
        // El estado no se descarta: rechazado (p. ej. la cuenta no está en /cuentas_dispositivo)
        // también espera, para no iniciar sesión en bucle.
        const Resultado r = enviarEstado();
        atenderResultado(r == RESULTADO_OK ? r : RESULTADO_REINTENTAR);
        return true;
    }
    if (!hay) {
        return false;
    }
    const Resultado r = enviar(p, hora_ref_ms, millis_ref);
    if (r != RESULTADO_REINTENTAR) {
        Bloqueo b;
        cola.confirmar(p);
    }
    atenderResultado(r);
    return true;
}

void tareaRed(void *) {
    crearClienteBd();
    for (;;) {
        {
            Bloqueo b;
            vigilante.latir(millis());
        }
        if (!red_wifi::conectado()) {
            cerrarConexion();
            vTaskDelay(pdMS_TO_TICKS(500));
        } else if (esperandoTrasError()) {
            vTaskDelay(pdMS_TO_TICKS(100));
        } else if (!asegurarSesion()) {
            espera_error_ms = FIREBASE_ESPERA_ERROR_MAXIMA_MS;
            marca_error_ms = millis();
        } else if (!atenderSiguiente()) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

bool valoresDeEjemplo() {
    return String(FIREBASE_URL).indexOf("tu-proyecto") >= 0 ||
           String(FIREBASE_API_KEY).indexOf("api_key_web") >= 0 ||
           String(DISPOSITIVO_CLAVE).indexOf("clave_de_la_cuenta") >= 0;
}

String quitarBarraFinal(const char *url) {
    String s(url);
    while (s.endsWith("/")) {
        s.remove(s.length() - 1);
    }
    return s;
}

unsigned long segundosDesde(uint32_t ms) {
    return static_cast<unsigned long>((millis() - ms) / 1000);
}

void imprimirSesion(const Resumen &r) {
    Serial.printf("# Firebase: sesion %s", r.sesion ? "activa" : "sin sesion");
    if (r.uid[0] != '\0') {
        Serial.printf(" (uid %s)", r.uid);
    }
    Serial.printf(", inicios %lu, renovaciones %lu\n", static_cast<unsigned long>(r.inicios_sesion),
                  static_cast<unsigned long>(r.renovaciones));
}

void imprimirEscrituras(const Resumen &r) {
    if (r.hubo_ok) {
        Serial.printf("# Firebase: ultima escritura OK hace %lu s", segundosDesde(r.ultima_ok_ms));
    } else {
        Serial.print("# Firebase: ninguna escritura OK todavia");
    }
    Serial.printf("; OK %lu, errores %lu (rechazadas %lu)", static_cast<unsigned long>(r.escrituras_ok),
                  static_cast<unsigned long>(r.errores), static_cast<unsigned long>(r.rechazadas));
    Serial.println();
}

void imprimirTerminada(const char *titulo, const PeticionTerminada &t) {
    Serial.printf("# Firebase: %s: %s %s -> %s (%lu ms), hace %lu s\n", titulo, t.metodo, t.ruta,
                  t.texto, static_cast<unsigned long>(t.duracion_ms), segundosDesde(t.fin_ms));
}

// Petición en curso y desde cuándo, la última terminada, el último error y el vigilante.
void imprimirPeticiones(const Resumen &r, const PeticionEnCurso &p, uint32_t cortes,
                        bool pospuesto) {
    if (p.activa) {
        const uint32_t lleva_ms = millis() - p.inicio_ms;
        Serial.printf("# Firebase: peticion en curso: %s %s, lleva %lu.%lu s (corte a los %lu s)\n",
                      p.metodo, p.ruta, static_cast<unsigned long>(lleva_ms / 1000),
                      static_cast<unsigned long>(lleva_ms % 1000 / 100),
                      static_cast<unsigned long>(FIREBASE_PETICION_MAX_MS / 1000));
    } else {
        Serial.println("# Firebase: ninguna peticion en curso");
    }
    if (r.hubo_peticion) {
        imprimirTerminada("ultima peticion", r.ultima);
    }
    if (r.hubo_error) {
        imprimirTerminada("ultimo error", r.ultimo_error);
    } else {
        Serial.println("# Firebase: ninguna peticion con error");
    }
    Serial.printf("# Firebase: vigilante: %lu cortes%s\n", static_cast<unsigned long>(cortes),
                  pospuesto ? ", reinicio pospuesto por la alerta local" : "");
}

void imprimirCola(const TamanosCola &t) {
    Serial.printf("# Firebase: cola eventos %u/%u, colgantes %u/%u, lecturas %u/%u "
                  "(lecturas descartadas %lu, eventos sin lugar %lu)\n",
                  static_cast<unsigned>(t.eventos), COLA_EVENTOS_CAPACIDAD,
                  static_cast<unsigned>(t.colgantes), COLA_COLGANTES_CAPACIDAD,
                  static_cast<unsigned>(t.lecturas), COLA_LECTURAS_CAPACIDAD,
                  static_cast<unsigned long>(t.lecturas_descartadas),
                  static_cast<unsigned long>(t.eventos_rechazados));
}

void imprimirHora(const Resumen &r) {
    if (!r.hora_ok) {
        Serial.println("# Firebase: hora del servidor NO sincronizada (las lecturas esperan)");
        return;
    }
    const uint64_t ahora = formato_nube::millisAHoraServidor(r.hora_ref_ms, r.millis_ref, millis());
    Serial.printf("# Firebase: hora del servidor sincronizada, ahora %llu ms (referencia de hace "
                  "%lu s)\n",
                  static_cast<unsigned long long>(ahora), segundosDesde(r.millis_ref));
}

void imprimirMemoria() {
    Serial.printf("# Memoria: heap libre %lu B (minimo %lu B), pila libre de la tarea de red %lu B\n",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMinFreeHeap()),
                  static_cast<unsigned long>(uxTaskGetStackHighWaterMark(tarea)));
}

// Desde loop(): shutdown() del socket de la petición en curso. Es seguro desde otra tarea (lwIP
// lo pasa por su hilo tcpip) y despierta a la tarea de red con un error en el select, el recv o
// el send en que esté. El close() lo hace la tarea de red al recibir el error: si lo hiciera
// loop(), el número de socket se podría reusar mientras la tarea de red todavía lo tiene.
// Con el candado tomado: la tarea de red no puede destruir cliente_en_uso mientras tanto.
void cortarPeticion() {
    if (cliente_en_uso == nullptr) {
        return;
    }
    const int fd = cliente_en_uso->fd();
    if (fd >= 0) {
        lwip_shutdown(fd, SHUT_RDWR);
    }
}

void describirBloqueo(const PeticionEnCurso &p, char *texto, size_t tamano) {
    if (p.activa) {
        snprintf(texto, tamano, "%s %s lleva %lu s sin terminar", p.metodo, p.ruta,
                 segundosDesde(p.inicio_ms));
    } else {
        snprintf(texto, tamano, "la tarea de red no avanza hace mas de %lu s",
                 static_cast<unsigned long>(FIREBASE_PETICION_MAX_MS / 1000));
    }
}

// Último recurso (vigilante_red.h). Se pierde la cola en RAM; el motivo queda en RTC para el
// arranque siguiente. Con el candado tomado: la cola no cambia mientras se cuenta lo que se pierde.
[[noreturn]] void reiniciar(const char *motivo) {
    const TamanosCola t = cola.tamanos();
    registrar("VIGILANTE: %s; se reinicia la base (se pierden %u eventos, %u colgantes y %u "
              "lecturas pendientes)",
              motivo, static_cast<unsigned>(t.eventos), static_cast<unsigned>(t.colgantes),
              static_cast<unsigned>(t.lecturas));
    strlcpy(reinicio_motivo, motivo, sizeof(reinicio_motivo));
    reinicio_marca = MARCA_REINICIO;
    Serial.flush();
    esp_restart();
}

void informarReinicioAnterior() {
    if (reinicio_marca == MARCA_REINICIO && esp_reset_reason() == ESP_RST_SW) {
        reinicio_motivo[sizeof(reinicio_motivo) - 1] = '\0';
        registrar("el arranque anterior termino en un reinicio del vigilante: %s", reinicio_motivo);
    }
    reinicio_marca = 0;
}

}  // namespace

void iniciar() {
    if (!NUBE_COMPILADA) {
        return;
    }
    informarReinicioAnterior();
    if (valoresDeEjemplo()) {
        motivo_inactiva = "secrets.h tiene los valores de ejemplo de Firebase";
        registrar("deshabilitado: %s", motivo_inactiva);
        return;
    }
    url_base = quitarBarraFinal(FIREBASE_URL);
    candado = xSemaphoreCreateMutex();
    vigilante.latir(millis());
    if (candado == nullptr ||
        xTaskCreate(tareaRed, "red", TAREA_RED_PILA, nullptr, TAREA_RED_PRIORIDAD, &tarea) != pdPASS) {
        candado = nullptr;
        motivo_inactiva = "no se pudo crear la tarea de red";
        registrar("ERROR: %s", motivo_inactiva);
    }
}

void encolarLectura(const LecturaNube &lectura) {
    if (!activa()) {
        return;
    }
    Bloqueo b;
    cola.agregarLectura(lectura);
}

void encolarColgante(uint8_t id, uint16_t bateria_mv, int8_t rssi) {
    if (!activa()) {
        return;
    }
    Bloqueo b;
    cola.agregarColgante({id, bateria_mv, rssi});
}

void encolarEvento(TipoEvento tipo, const char *origen, const char *detalle) {
    if (!activa()) {
        return;
    }
    PeticionEvento e{};
    e.tipo = tipo;
    strlcpy(e.origen, origen, sizeof(e.origen));
    strlcpy(e.detalle, detalle != nullptr ? detalle : "", sizeof(e.detalle));
    e.millis_creado = millis();
    bool ok;
    {
        Bloqueo b;
        ok = cola.agregarEvento(e);
    }
    if (!ok) {
        registrar("ERROR: cola de eventos llena (%u), no se guardo %s de %s",
                  COLA_EVENTOS_CAPACIDAD, formato_nube::nombreEvento(tipo), origen);
    }
}

void vigilar(bool alerta_urgente) {
    static uint32_t ultima_ms = 0;
    static bool pospuesto_antes = false;
    if (!activa() || millis() - ultima_ms < FIREBASE_VIGILANTE_PERIODO_MS) {
        return;
    }
    ultima_ms = millis();
    AccionVigilante accion;
    PeticionEnCurso p;
    bool pospuesto;
    {
        Bloqueo b;
        accion = vigilante.revisar(ultima_ms, alerta_urgente);
        p = vigilante.peticion();
        pospuesto = vigilante.reinicioPospuesto();
        if (accion == VIGILANTE_CORTAR) {
            cortarPeticion();
        }
    }
    if (accion == VIGILANTE_NADA && pospuesto == pospuesto_antes) {
        return;
    }
    char motivo[sizeof(reinicio_motivo)];
    describirBloqueo(p, motivo, sizeof(motivo));
    if (accion == VIGILANTE_CORTAR) {
        registrar("VIGILANTE: %s; se cierra su socket", motivo);
    } else if (pospuesto && !pospuesto_antes) {
        registrar("VIGILANTE: %s; reinicio pospuesto hasta que termine la alerta local", motivo);
    }
    pospuesto_antes = pospuesto;
    if (accion == VIGILANTE_REINICIAR) {
        Bloqueo b;
        reiniciar(motivo);
    }
}

void imprimirEstado() {
    if (!activa()) {
        Serial.printf("# Firebase: deshabilitado (%s)\n", motivo_inactiva);
        return;
    }
    Resumen r;
    TamanosCola t;
    PeticionEnCurso p;
    uint32_t cortes;
    bool pospuesto;
    {
        Bloqueo b;
        r = resumen;
        t = cola.tamanos();
        p = vigilante.peticion();
        cortes = vigilante.cortes();
        pospuesto = vigilante.reinicioPospuesto();
    }
    imprimirSesion(r);
    imprimirEscrituras(r);
    imprimirPeticiones(r, p, cortes, pospuesto);
    imprimirCola(t);
    imprimirHora(r);
    imprimirMemoria();
}

}  // namespace firebase_cliente
