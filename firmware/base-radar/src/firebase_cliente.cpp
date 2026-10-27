#include "firebase_cliente.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdarg>
#include <cstring>

#include "almacen.h"
#include "cola_peticiones.h"
#include "config.h"
#include "detectores.h"
#include "mensajes_telegram.h"
#include "motivo_reinicio.h"
#include "peticion_https.h"
#include "red_wifi.h"
#include "registro.h"
#include "telegram.h"
#include "watchdog_tareas.h"

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
#endif
#ifndef BASE_ID
#define BASE_ID "base"
#endif

namespace firebase_cliente {

namespace {

const char *URL_INICIO_SESION =
    "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword";
const char *URL_RENOVAR_TOKEN = "https://securetoken.googleapis.com/v1/token";
const char *CLAVE_EVENTOS = "eventos";  // almacen.h

// Código propio: el 401 pidió iniciar sesión de nuevo y no se pudo (sin red, clave mala).
constexpr int CODIGO_SIN_SESION = -100;

enum Resultado : uint8_t {
    RESULTADO_OK,
    RESULTADO_REINTENTAR,  // red o servidor: queda en la cola y se reintenta con espera
    RESULTADO_RECHAZADA,   // las reglas o el formato la rechazan: reintentar no sirve
};

void registrarFirebase(const char *formato, ...) __attribute__((format(printf, 1, 2)));
void registrarFirebase(const char *formato, ...) {
    va_list args;
    va_start(args, formato);
    registrarLista("firebase", formato, args);
    va_end(args);
}

// ---------------------------------------------------------------------------
// Estado compartido entre loop() y la tarea de red. Protegido por `candado`, que nunca se tiene
// tomado durante una petición: solo para copiar datos.
// ---------------------------------------------------------------------------

struct Resumen {
    bool     sesion;
    char     uid[48];
    uint32_t inicios_sesion;
    uint32_t renovaciones;
    uint32_t escrituras_ok;
    uint32_t errores;      // peticiones con error, también las de inicio de sesión
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
    bool     eventos_guardados;      // la última escritura en NVS salió bien
    int      eventos_restaurados;    // al arrancar, de NVS (-1: datos inválidos)
};

SemaphoreHandle_t candado = nullptr;
ColaPeticiones cola;
Resumen resumen = {};
TaskHandle_t tarea = nullptr;
bool firebase_activa = false;
const char *motivo_inactiva = "FIREBASE_HABILITADO 0 en config.h";

class Bloqueo {
public:
    Bloqueo() { xSemaphoreTake(candado, portMAX_DELAY); }
    ~Bloqueo() { xSemaphoreGive(candado); }
};

// La tarea de red corre: Firebase, Telegram o los dos.
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

// ultimo_contacto anterior, para BASE_DESCONECTADA (docs/fallos.md, escenario 6).
struct Contacto {
    bool     leido;     // al arrancar ya se leyó el que dejó el arranque anterior
    bool     hay;       // existe (la primera vez de una base nueva no)
    uint64_t hora_ms;
};

Sesion sesion = {};
Contacto contacto = {};
String url_base;
NetworkClientSecure *cliente_bd = nullptr;  // conexión a la base de datos, con keep-alive
HTTPClient *http_bd = nullptr;
uint32_t ultimo_estado_ms = 0;
bool estado_enviado = false;
uint32_t ultimo_uso_bd_ms = 0;  // fin de la última petición por cliente_bd
uint32_t espera_error_ms = 0;   // 0 = sin error pendiente
uint32_t marca_error_ms = 0;
uint8_t buffer_eventos[ColaEventos::TAMANO_SERIALIZADO];

String textoError(int codigo, const String &cuerpo) {
    const String texto = peticion_https::textoError(codigo, cuerpo);
    if (texto.length() == 0 && codigo == CODIGO_SIN_SESION) {
        return String("sin sesion");
    }
    return texto;
}

void anotarTerminada(const PeticionTerminada &t) {
    const bool fallo = t.codigo <= 0 || t.codigo >= 400;
    Bloqueo b;
    resumen.hubo_peticion = true;
    resumen.ultima = t;
    if (fallo) {
        resumen.hubo_error = true;
        resumen.ultimo_error = t;
    }
}

void contarError() {
    Bloqueo b;
    resumen.errores++;
}

// ---------------------------------------------------------------------------
// Firebase Auth
// ---------------------------------------------------------------------------

// POST a Firebase Auth con una conexión propia que se cierra al terminar (una vez por hora).
// etiqueta: nombre corto para la traza (la URL lleva la API key).
int pedirAuth(const char *url, const char *etiqueta, const char *tipo, const String &cuerpo,
              String &respuesta) {
    NetworkClientSecure cliente;
    peticion_https::configurarCliente(cliente);
    HTTPClient http;
    peticion_https::configurarHttp(http);
    http.setReuse(false);
    const int tls_antes = peticion_https::empezar("POST", etiqueta, cliente);
    int codigo = HTTPC_ERROR_CONNECTION_REFUSED;
    respuesta = String();
    if (http.begin(cliente, url)) {
        http.addHeader("Content-Type", tipo);
        codigo = peticion_https::leerCuerpo(http, cliente, http.POST(cuerpo), respuesta);
        http.end();
    }
    PeticionTerminada t;
    peticion_https::terminar("firebase", codigo, respuesta, cliente, tls_antes, t);  // cliente nuevo
    anotarTerminada(t);
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
        contarError();
        registrarFirebase("no se pudo iniciar sesion con %s: HTTP %d %s", DISPOSITIVO_EMAIL,
                          codigo, textoError(codigo, respuesta).c_str());
        return false;
    }
    char uid[48] = "?";
    formato_nube::extraerTexto(respuesta.c_str(), "localId", uid, sizeof(uid));
    publicarSesion(uid, true, false);
    registrarFirebase("sesion iniciada: %s (uid %s), token por %lu s", DISPOSITIVO_EMAIL, uid,
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
        contarError();
        registrarFirebase("no se pudo renovar el token (HTTP %d %s); se inicia sesion de nuevo",
                          codigo, textoError(codigo, respuesta).c_str());
        return iniciarSesion();
    }
    publicarSesion(nullptr, false, true);
    registrarFirebase("token renovado, vence en %lu s",
                      static_cast<unsigned long>(sesion.dura_ms / 1000));
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
    peticion_https::configurarCliente(*cliente_bd);
    http_bd = new HTTPClient();
    peticion_https::configurarHttp(*http_bd);
    http_bd->setReuse(true);  // keep-alive: un handshake TLS por conexión, no por petición
}

// Tras un corte del vigilante el cliente pudo quedar a medias (cortado en medio del handshake o
// de un registro TLS): se recrea entero en lugar de reusarlo.
void recrearClienteBd() {
    delete http_bd;  // primero: su destructor todavía usa el cliente
    delete cliente_bd;
    crearClienteBd();
    registrarFirebase("cliente TLS de la base de datos recreado");
}

void cerrarConexion() {
    if (cliente_bd != nullptr && cliente_bd->connected()) {
        cliente_bd->stop();
    }
}

int pedirBdUnaVez(const char *metodo, const char *ruta, const String &url, const char *cuerpo,
                  String &respuesta) {
    const int tls_antes = peticion_https::empezar(metodo, ruta, *cliente_bd);
    int codigo = HTTPC_ERROR_CONNECTION_REFUSED;
    respuesta = String();
    if (http_bd->begin(*cliente_bd, url)) {
        const size_t largo = cuerpo != nullptr ? strlen(cuerpo) : 0;
        codigo = http_bd->sendRequest(
            metodo, reinterpret_cast<uint8_t *>(const_cast<char *>(cuerpo)), largo);
        codigo = peticion_https::leerCuerpo(*http_bd, *cliente_bd, codigo, respuesta);
        http_bd->end();
    }
    if (codigo <= 0) {
        cerrarConexion();  // la próxima petición abre una conexión nueva
    }
    ultimo_uso_bd_ms = millis();
    PeticionTerminada t;
    const bool cortada =
        peticion_https::terminar("firebase", codigo, respuesta, *cliente_bd, tls_antes, t);
    anotarTerminada(t);
    if (cortada) {
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
    registrarFirebase("401 en %s (%s); se inicia sesion de nuevo", ruta,
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
    registrarFirebase("ERROR %s %s: HTTP %d %s%s", metodo, ruta, codigo,
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
// Eventos: destinos, cola y guardado en NVS
// ---------------------------------------------------------------------------

uint8_t destinosActivos() {
    return static_cast<uint8_t>((firebase_activa ? DESTINO_FIREBASE : 0) |
                                (telegram::activo() ? DESTINO_TELEGRAM : 0));
}

// REINICIO solo va a Telegram: las reglas no lo aceptan en /eventos.
uint8_t destinosPara(TipoEvento tipo) {
    const uint8_t destinos = destinosActivos();
    return tipo == EVENTO_REINICIO ? (destinos & DESTINO_TELEGRAM) : destinos;
}

void agregarEvento(TipoEvento tipo, const char *origen, const char *detalle) {
    PeticionEvento e{};
    e.tipo = tipo;
    e.destinos = destinosPara(tipo);
    strlcpy(e.origen, origen != nullptr ? origen : BASE_ID, sizeof(e.origen));
    strlcpy(e.detalle, detalle != nullptr ? detalle : "", sizeof(e.detalle));
    e.millis_creado = millis();
    if (e.destinos == 0) {
        return;  // ni Firebase ni Telegram para este tipo
    }
    ResultadoAgregar r;
    {
        Bloqueo b;
        r = cola.agregarEvento(e);
    }
    if (r == EVENTO_RECHAZADO) {
        registrarFirebase("ERROR: cola de eventos llena (%u), no se guardo %s de %s",
                          COLA_EVENTOS_CAPACIDAD, formato_nube::nombreEvento(tipo), e.origen);
    } else if (r == EVENTO_AGREGADO_REEMPLAZANDO) {
        registrarFirebase("cola de eventos llena: el BOTON reemplazo al evento mas antiguo");
    }
}

// Escribe en NVS los eventos pendientes si cambiaron (o si la escritura anterior falló). Con la
// cola vacía borra la clave.
void guardarEventos() {
    static bool reintentar = false;
    size_t largo = 0;
    {
        Bloqueo b;
        if (!cola.eventosCambiaron() && !reintentar) {
            return;
        }
        largo = cola.serializarEventos(buffer_eventos, sizeof(buffer_eventos));
    }
    const bool vacia = largo <= 4;
    const bool ok = largo > 0 && almacen::escribir(CLAVE_EVENTOS, buffer_eventos, vacia ? 0 : largo);
    reintentar = !ok;
    Bloqueo b;
    resumen.eventos_guardados = ok;
}

void restaurarEventos() {
    const size_t largo = almacen::leer(CLAVE_EVENTOS, buffer_eventos, sizeof(buffer_eventos));
    if (largo == 0) {
        return;
    }
    const int n = cola.restaurarEventos(buffer_eventos, largo, destinosActivos());
    resumen.eventos_restaurados = n;
    if (n < 0) {
        registrarFirebase("eventos guardados en NVS con otro formato: se ignoran");
    } else {
        registrarFirebase("%d eventos pendientes de antes del reinicio, restaurados de NVS", n);
    }
}

// ---------------------------------------------------------------------------
// Estado, hora del servidor (docs/modelo-datos.md, 2) y BASE_DESCONECTADA (docs/fallos.md, 6)
// ---------------------------------------------------------------------------

String rutaEstado() {
    return String("dispositivos/") + BASE_ID + "/estado";
}

// GET de ultimo_contacto. Devuelve el código; con 200, existe dice si había un valor ("null"
// cuando la base nunca escribió su estado).
int leerUltimoContacto(uint64_t &hora, bool &existe) {
    const String ruta = rutaEstado() + "/ultimo_contacto";
    String respuesta;
    const int codigo = pedirBdConSesion("GET", ruta.c_str(), nullptr, false, respuesta);
    existe = codigo == 200 && formato_nube::leerEntero(respuesta.c_str(), hora);
    const bool nulo = codigo == 200 && respuesta.indexOf("null") >= 0;
    if (codigo == 200 && !existe && !nulo) {
        anotarResultado("GET", ruta.c_str(), 0, respuesta, false);  // respuesta sin un número
        return 0;
    }
    if (codigo != 200) {
        anotarResultado("GET", ruta.c_str(), codigo, respuesta, false);
    }
    return codigo;
}

// Al arrancar, antes del primer PATCH del estado (que lo pisa): el ultimo_contacto que dejó el
// arranque anterior.
bool leerContactoAnterior() {
    uint64_t hora = 0;
    bool existe = false;
    if (leerUltimoContacto(hora, existe) != 200) {
        return false;
    }
    contacto = {true, existe, hora};
    return true;
}

bool leerHoraServidor(uint64_t &hora) {
    bool existe = false;
    const int codigo = leerUltimoContacto(hora, existe);
    const uint32_t ahora = millis();
    if (codigo != 200 || !existe) {
        return false;
    }
    Bloqueo b;
    resumen.hora_ok = true;
    resumen.hora_ref_ms = hora;
    resumen.millis_ref = ahora;
    return true;
}

// Compara el ultimo_contacto nuevo con el anterior (del arranque anterior o del último estado
// enviado). Una diferencia de BASE_DESCONECTADA_MIN_MS o más es un evento, una vez por vuelta.
void revisarDesconexion(uint64_t nueva_hora) {
    const uint64_t duracion = duracionDesconexion(contacto.hay, contacto.hora_ms, nueva_hora);
    contacto.hay = true;
    contacto.hora_ms = nueva_hora;
    if (duracion == 0) {
        return;
    }
    char texto[24];
    textoDuracion(duracion, texto, sizeof(texto));
    char detalle[48];
    snprintf(detalle, sizeof(detalle), "sin conexión por %s", texto);
    registrarFirebase("la base estuvo %s sin actualizar su estado: evento BASE_DESCONECTADA", texto);
    agregarEvento(EVENTO_BASE_DESCONECTADA, BASE_ID, detalle);
}

int rssiWifi() {
    return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
}

Resultado enviarEstado() {
    if (!contacto.leido && !leerContactoAnterior()) {
        return RESULTADO_REINTENTAR;
    }
    char json[160];
    formato_nube::jsonEstado(rssiWifi(), FIRMWARE_VERSION, json, sizeof(json));
    const Resultado r = escribir("PATCH", rutaEstado().c_str(), json, false);
    if (r != RESULTADO_OK) {
        return r;
    }
    uint64_t hora = 0;
    if (!leerHoraServidor(hora)) {
        return RESULTADO_REINTENTAR;
    }
    revisarDesconexion(hora);
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

void detalleEnviado(const PeticionEvento &e, char *detalle, size_t tamano) {
    const uint32_t retraso_s = (millis() - e.millis_creado) / 1000;
    if (e.previo_reinicio) {
        snprintf(detalle, tamano, "%s (de antes de un reinicio de la base)", e.detalle);
    } else if (retraso_s >= 10) {
        snprintf(detalle, tamano, "%s (enviado %lu s despues)", e.detalle,
                 static_cast<unsigned long>(retraso_s));
    } else {
        strlcpy(detalle, e.detalle, tamano);
    }
}

Resultado enviarEvento(const PeticionEvento &e) {
    char detalle[128];
    detalleEnviado(e, detalle, sizeof(detalle));
    char json[320];
    formato_nube::jsonEvento(e.tipo, e.origen, detalle, json, sizeof(json));
    const Resultado r = escribir("POST", "eventos", json);
    if (r == RESULTADO_OK) {
        registrarFirebase("evento %s de %s enviado", formato_nube::nombreEvento(e.tipo), e.origen);
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
        registrarFirebase("lectura descartada: ts %llu no tiene 13 digitos",
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

bool atenderFirebase() {
    if (!firebase_activa || esperandoTrasError()) {
        return false;
    }
    if (!asegurarSesion()) {
        espera_error_ms = FIREBASE_ESPERA_ERROR_MAXIMA_MS;
        marca_error_ms = millis();
        return true;
    }
    return atenderSiguiente();
}

// Un aviso por vuelta, el BOTON primero. Un aviso que no sale queda en la cola; uno rechazado sin
// remedio (token o chat inválidos) se saca, para no trabar a los demás.
bool atenderTelegram() {
    if (!telegram::activo()) {
        return false;
    }
    PeticionEvento e;
    uint32_t numero;
    bool hay;
    {
        Bloqueo b;
        hay = cola.siguienteTelegram(e, numero);
    }
    const bool urgente = hay && mensajes_telegram::esUrgente(e.tipo);
    if (!hay || !telegram::listo(urgente)) {
        return false;
    }
    char texto[256];
    mensajes_telegram::texto(e, millis(), texto, sizeof(texto));
    const telegram::ResultadoTelegram r = telegram::enviar(texto, urgente);
    if (r == telegram::TELEGRAM_REINTENTAR) {
        return true;
    }
    {
        Bloqueo b;
        cola.confirmarTelegram(numero);
    }
    if (r == telegram::TELEGRAM_OK) {
        registrar("telegram", "aviso %s de %s enviado", formato_nube::nombreEvento(e.tipo),
                  e.origen);
    }
    return true;
}

void cerrarConexiones() {
    cerrarConexion();
    telegram::cerrarConexion();
}

// Cada vuelta: avisa al vigilante y al watchdog, guarda lo pendiente en NVS (también sin red) y,
// con WiFi, atiende Telegram y después Firebase. Telegram no espera a Firebase: si Firebase está
// en su espera tras error o sin sesión, el aviso sale igual.
void tareaRed(void *) {
    watchdog_tareas::suscribirEstaTarea();
    if (firebase_activa) {
        crearClienteBd();
    }
    for (;;) {
        peticion_https::latir();
        watchdog_tareas::alimentar();
        guardarEventos();
        almacen::escribirPendientes();
        if (!red_wifi::conectado()) {
            cerrarConexiones();
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        telegram::cerrarSiInactiva();
        const bool hubo_telegram = atenderTelegram();
        const bool hubo_firebase = atenderFirebase();
        if (!hubo_telegram && !hubo_firebase) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

bool valoresDeEjemplo() {
    return String(FIREBASE_URL).indexOf("tu-proyecto") >= 0 ||
           String(FIREBASE_API_KEY).indexOf("api_key_web") >= 0 ||
           String(DISPOSITIVO_CLAVE).indexOf("clave_de_la_cuenta") >= 0;
}

bool revisarFirebase() {
    if (!NUBE_COMPILADA) {
        return false;
    }
    if (valoresDeEjemplo()) {
        motivo_inactiva = "secrets.h tiene los valores de ejemplo de Firebase";
        registrarFirebase("deshabilitado: %s", motivo_inactiva);
        return false;
    }
    return true;
}

String quitarBarraFinal(const char *url) {
    String s(url);
    while (s.endsWith("/")) {
        s.remove(s.length() - 1);
    }
    return s;
}

// ---------------------------------------------------------------------------
// Comando "e"
// ---------------------------------------------------------------------------

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
    Serial.printf("; OK %lu, errores %lu (rechazadas %lu)\n",
                  static_cast<unsigned long>(r.escrituras_ok),
                  static_cast<unsigned long>(r.errores), static_cast<unsigned long>(r.rechazadas));
}

void imprimirTerminada(const char *titulo, const PeticionTerminada &t) {
    Serial.printf("# Firebase: %s: %s %s -> %s (%lu ms), hace %lu s\n", titulo, t.metodo, t.ruta,
                  t.texto, static_cast<unsigned long>(t.duracion_ms), segundosDesde(t.fin_ms));
}

void imprimirPeticiones(const Resumen &r) {
    if (r.hubo_peticion) {
        imprimirTerminada("ultima peticion", r.ultima);
    }
    if (r.hubo_error) {
        imprimirTerminada("ultimo error", r.ultimo_error);
    } else {
        Serial.println("# Firebase: ninguna peticion con error");
    }
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

void imprimirFirebase(const Resumen &r) {
    if (!firebase_activa) {
        Serial.printf("# Firebase: deshabilitado (%s)\n", motivo_inactiva);
        return;
    }
    imprimirSesion(r);
    imprimirEscrituras(r);
    imprimirPeticiones(r);
    imprimirHora(r);
}

void imprimirCola(const Resumen &r, const TamanosCola &t) {
    Serial.printf("# Cola: eventos pendientes %u/%u (Firebase %u, Telegram %u), %s; rechazados "
                  "%lu, reemplazados por un BOTON %lu",
                  static_cast<unsigned>(t.eventos), COLA_EVENTOS_CAPACIDAD,
                  static_cast<unsigned>(t.eventos_firebase),
                  static_cast<unsigned>(t.eventos_telegram),
                  r.eventos_guardados ? "guardados en NVS" : "NVS sin escribir todavia",
                  static_cast<unsigned long>(t.eventos_rechazados),
                  static_cast<unsigned long>(t.eventos_reemplazados));
    if (r.eventos_restaurados != 0) {
        Serial.printf("; restaurados al arrancar %d", r.eventos_restaurados);
    }
    Serial.println();
    Serial.printf("# Cola offline: lecturas %u/%u (%lu s de lecturas), descartadas %lu; colgantes "
                  "%u/%u\n",
                  static_cast<unsigned>(t.lecturas), COLA_OFFLINE_LECTURAS,
                  static_cast<unsigned long>(t.lecturas * (LECTURA_INTERVALO_MS / 1000)),
                  static_cast<unsigned long>(t.lecturas_descartadas),
                  static_cast<unsigned>(t.colgantes), COLA_COLGANTES_CAPACIDAD);
}

void imprimirMemoria() {
    Serial.printf("# Memoria: heap libre %lu B (minimo %lu B), pila libre de la tarea de red %lu B\n",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMinFreeHeap()),
                  static_cast<unsigned long>(uxTaskGetStackHighWaterMark(tarea)));
}

// Último recurso del vigilante (vigilante_red.h): guarda los eventos pendientes en NVS, deja el
// motivo en RTC para el arranque siguiente y reinicia. Se pierden las lecturas y los colgantes en
// RAM. Con el candado tomado: la cola no cambia mientras se guarda.
[[noreturn]] void reiniciar(const char *motivo) {
    const TamanosCola t = cola.tamanos();
    const size_t largo = cola.serializarEventos(buffer_eventos, sizeof(buffer_eventos));
    const bool guardados = largo > 0 && almacen::escribir(CLAVE_EVENTOS, buffer_eventos,
                                                          largo <= 4 ? 0 : largo);
    registrar("vigilante", "%s; se reinicia la base (%u eventos pendientes %s; se pierden %u "
                           "colgantes y %u lecturas)",
              motivo, static_cast<unsigned>(t.eventos),
              guardados ? "guardados en NVS" : "SIN GUARDAR", static_cast<unsigned>(t.colgantes),
              static_cast<unsigned>(t.lecturas));
    motivo_reinicio::anotarVigilante(motivo);
    Serial.flush();
    esp_restart();
}

}  // namespace

void iniciar() {
    firebase_activa = revisarFirebase();
    telegram::iniciar();
    if (!firebase_activa && !telegram::activo()) {
        return;
    }
    url_base = quitarBarraFinal(FIREBASE_URL);
    candado = xSemaphoreCreateMutex();
    if (candado == nullptr || !peticion_https::iniciar()) {
        candado = nullptr;
        firebase_activa = false;
        motivo_inactiva = "sin memoria para la tarea de red";
        registrarFirebase("ERROR: %s", motivo_inactiva);
        return;
    }
    restaurarEventos();
    if (xTaskCreate(tareaRed, "red", TAREA_RED_PILA, nullptr, TAREA_RED_PRIORIDAD, &tarea) !=
        pdPASS) {
        candado = nullptr;
        firebase_activa = false;
        motivo_inactiva = "no se pudo crear la tarea de red";
        registrarFirebase("ERROR: %s", motivo_inactiva);
    }
}

void encolarLectura(const LecturaNube &lectura) {
    if (!activa() || !firebase_activa) {
        return;
    }
    Bloqueo b;
    cola.agregarLectura(lectura);
}

void encolarColgante(uint8_t id, uint16_t bateria_mv, int8_t rssi) {
    if (!activa() || !firebase_activa) {
        return;
    }
    Bloqueo b;
    cola.agregarColgante({id, bateria_mv, rssi});
}

void encolarEvento(TipoEvento tipo, const char *origen, const char *detalle) {
    if (activa()) {
        agregarEvento(tipo, origen, detalle);
    }
}

bool tareaActiva() {
    return activa();
}

void vigilar(bool alerta_urgente) {
    if (!activa()) {
        return;
    }
    char motivo[112];
    if (peticion_https::vigilar(alerta_urgente, motivo, sizeof(motivo)) == VIGILANTE_REINICIAR) {
        Bloqueo b;
        reiniciar(motivo);
    }
}

void imprimirEstado() {
    Resumen r = {};
    TamanosCola t = {};
    if (activa()) {
        Bloqueo b;
        r = resumen;
        t = cola.tamanos();
    }
    imprimirFirebase(r);
    telegram::imprimirEstado();
    if (!activa()) {
        return;
    }
    peticion_https::imprimirEstado();
    imprimirCola(r, t);
    imprimirMemoria();
}

}  // namespace firebase_cliente
