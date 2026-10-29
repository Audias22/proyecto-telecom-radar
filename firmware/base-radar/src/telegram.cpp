#include "telegram.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstring>

#include "cliente_tls.h"
#include "config.h"
#include "formato_nube.h"
#include "mensajes_telegram.h"
#include "peticion_https.h"
#include "registro.h"

#if TELEGRAM_HABILITADO && !defined(DIAGNOSTICO) && __has_include("secrets.h")
#include "secrets.h"
#define TELEGRAM_COMPILADO 1
#else
#define TELEGRAM_COMPILADO 0
#endif

#ifndef TELEGRAM_TOKEN
#define TELEGRAM_TOKEN   ""
#define TELEGRAM_CHAT_ID ""
#endif

namespace telegram {

namespace {

const char *URL_API = "https://api.telegram.org/bot";
const char *RUTA_TRAZA = "telegram/sendMessage";  // la URL lleva el token: nunca a la traza

struct Resumen {
    uint32_t enviados;
    uint32_t errores;
    uint32_t rechazados;
    bool     hubo_ok;
    uint32_t ultimo_ok_ms;
    bool     hubo_error;
    PeticionTerminada ultimo_error;
};

// Protege resumen y limite, que lee loop() en el comando "e".
SemaphoreHandle_t candado = nullptr;
Resumen resumen = {};
LimiteTelegram limite;
const char *motivo_inactivo = "TELEGRAM_HABILITADO 0 en config.h";

// Solo la tarea de red.
ClienteTls *cliente = nullptr;
HTTPClient *http = nullptr;
uint32_t ultimo_uso_ms = 0;

class Bloqueo {
public:
    Bloqueo() { xSemaphoreTake(candado, portMAX_DELAY); }
    ~Bloqueo() { xSemaphoreGive(candado); }
};

// Se revisa con String, en tiempo de ejecución: con las funciones de <cstring> sobre los
// literales el compilador evalúa la comparación y sus advertencias imprimen el token.
const char *revisarSecretos() {
    if (!TELEGRAM_COMPILADO) {
        return "TELEGRAM_HABILITADO 0 en config.h (o firmware de diagnostico)";
    }
    const String token(TELEGRAM_TOKEN);
    const String chat(TELEGRAM_CHAT_ID);
    if (token.length() == 0 || chat.length() == 0) {
        return "faltan TELEGRAM_TOKEN y TELEGRAM_CHAT_ID en secrets.h";
    }
    if (token.indexOf("token_del_bot") >= 0 || chat == "-1001234567890") {
        return "secrets.h tiene los valores de ejemplo de TELEGRAM_TOKEN o TELEGRAM_CHAT_ID";
    }
    if (token.indexOf(':') < 0) {
        return "TELEGRAM_TOKEN no tiene el formato de @BotFather (numero:texto)";
    }
    return nullptr;
}

void crearCliente() {
    cliente = new ClienteTls();
    http = new HTTPClient();
    peticion_https::configurarHttp(*http);
    http->setReuse(true);
}

// Tras un corte del vigilante el cliente pudo quedar a medias: se recrea entero.
void recrearCliente() {
    delete http;  // primero: su destructor todavía usa el cliente
    delete cliente;
    crearCliente();
    registrar("telegram", "cliente TLS recreado");
}

void anotarError(const PeticionTerminada &t) {
    Bloqueo b;
    resumen.hubo_error = true;
    resumen.ultimo_error = t;
}

int pedirUnaVez(const String &url, const char *json, String &respuesta) {
    const int tls_antes = peticion_https::empezar("POST", RUTA_TRAZA, *cliente);
    int codigo = HTTPC_ERROR_CONNECTION_REFUSED;
    if (http->begin(*cliente, url)) {
        http->addHeader("Content-Type", "application/json");
        codigo = http->POST(reinterpret_cast<uint8_t *>(const_cast<char *>(json)), strlen(json));
        codigo = peticion_https::leerCuerpo(*http, *cliente, codigo, respuesta);
        http->end();
    }
    if (codigo <= 0) {
        cerrarConexion();  // la próxima petición abre una conexión nueva
    }
    ultimo_uso_ms = millis();
    PeticionTerminada t;
    const bool cortada = peticion_https::terminar("telegram", codigo, respuesta, *cliente,
                                                  tls_antes, t);
    if (codigo != 200) {
        anotarError(t);
    }
    if (cortada) {
        recrearCliente();
    }
    return codigo;
}

// Si falla sobre una conexión reutilizada (el servidor pudo cerrarla), se reintenta una vez con
// una conexión nueva.
int pedir(const char *json, String &respuesta) {
    if (cliente == nullptr) {
        crearCliente();
    }
    cerrarSiInactiva();
    const String url = String(URL_API) + TELEGRAM_TOKEN + "/sendMessage";
    const bool reutilizada = cliente->connected();
    const int codigo = pedirUnaVez(url, json, respuesta);
    if (codigo > 0 || !reutilizada) {
        return codigo;
    }
    return pedirUnaVez(url, json, respuesta);
}

ResultadoTelegram clasificar(int codigo, const String &respuesta, bool urgente) {
    const uint32_t ahora = millis();
    Bloqueo b;
    if (codigo == 200) {
        limite.enviado(ahora, urgente);
        resumen.enviados++;
        resumen.hubo_ok = true;
        resumen.ultimo_ok_ms = ahora;
        return TELEGRAM_OK;
    }
    if (codigo <= 0 || codigo == 429 || codigo >= 500) {
        uint64_t segundos = 0;
        formato_nube::extraerEntero(respuesta.c_str(), "retry_after", segundos);
        limite.fallo(ahora, static_cast<uint32_t>(segundos * 1000));
        resumen.errores++;
        return TELEGRAM_REINTENTAR;
    }
    limite.sinError();
    resumen.rechazados++;
    return TELEGRAM_RECHAZADO;
}

unsigned long segundosDesde(uint32_t ms) {
    return static_cast<unsigned long>((millis() - ms) / 1000);
}

}  // namespace

void iniciar() {
    const char *problema = revisarSecretos();
    if (problema != nullptr) {
        motivo_inactivo = problema;
        registrar("telegram", "deshabilitado: %s", motivo_inactivo);
        return;
    }
    candado = xSemaphoreCreateMutex();
    if (candado == nullptr) {
        motivo_inactivo = "sin memoria para el candado";
        registrar("telegram", "ERROR: %s", motivo_inactivo);
        return;
    }
    registrar("telegram", "activo");
}

bool activo() {
    return candado != nullptr;
}

bool listo(bool urgente) {
    if (!activo()) {
        return false;
    }
    Bloqueo b;
    return limite.puedeEnviar(millis(), urgente);
}

ResultadoTelegram enviar(const char *texto, bool urgente) {
    char json[384];
    if (formato_nube::jsonTelegram(TELEGRAM_CHAT_ID, texto, json, sizeof(json)) == 0) {
        registrar("telegram", "ERROR: el mensaje no cabe en el cuerpo de la peticion");
        return TELEGRAM_RECHAZADO;
    }
    String respuesta;
    const int codigo = pedir(json, respuesta);
    const ResultadoTelegram r = clasificar(codigo, respuesta, urgente);
    if (r == TELEGRAM_RECHAZADO) {
        registrar("telegram", "ERROR: HTTP %d %s; el aviso se descarta (revisar TELEGRAM_TOKEN, "
                              "TELEGRAM_CHAT_ID y que el bot este en el chat)",
                  codigo, peticion_https::textoError(codigo, respuesta).c_str());
    }
    return r;
}

void cerrarSiInactiva() {
    if (cliente != nullptr && cliente->connected() &&
        millis() - ultimo_uso_ms >= TELEGRAM_REUSO_MAX_MS) {
        cliente->stop();
    }
}

void cerrarConexion() {
    if (cliente != nullptr && cliente->connected()) {
        cliente->stop();
    }
}

void imprimirEstado() {
    if (!activo()) {
        Serial.printf("# Telegram: deshabilitado (%s)\n", motivo_inactivo);
        return;
    }
    Resumen r;
    uint32_t espera;
    {
        Bloqueo b;
        r = resumen;
        espera = limite.espera(millis(), false);
    }
    Serial.printf("# Telegram: activo; enviados %lu, errores %lu, rechazados %lu",
                  static_cast<unsigned long>(r.enviados), static_cast<unsigned long>(r.errores),
                  static_cast<unsigned long>(r.rechazados));
    if (r.hubo_ok) {
        Serial.printf("; ultimo OK hace %lu s", segundosDesde(r.ultimo_ok_ms));
    }
    Serial.printf("; proximo aviso no urgente en %lu s\n",
                  static_cast<unsigned long>((espera + 999) / 1000));
    if (r.hubo_error) {
        Serial.printf("# Telegram: ultimo error: %s (%lu ms), hace %lu s\n", r.ultimo_error.texto,
                      static_cast<unsigned long>(r.ultimo_error.duracion_ms),
                      segundosDesde(r.ultimo_error.fin_ms));
    }
}

}  // namespace telegram
