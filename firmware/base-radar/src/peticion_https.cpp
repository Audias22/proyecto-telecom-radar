#include "peticion_https.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/sockets.h>

#include <cstring>

#include "config.h"
#include "cuerpo_http.h"
#include "formato_nube.h"
#include "registro.h"
#include "watchdog_tareas.h"

// Bundle de certificados raíz que trae ESP-IDF dentro de libmbedtls.a de arduino-esp32 3.3.7
// (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL: la lista de Mozilla, ~69 kB en flash). Incluye
// las raíces de Google Trust Services (googleapis.com, firebaseio.com) y la de Go Daddy G2
// (api.telegram.org).
extern const uint8_t bundle_ca_inicio[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t bundle_ca_fin[] asm("_binary_x509_crt_bundle_end");

namespace peticion_https {

namespace {

// Cabeceras de la respuesta que se guardan: el lector del cuerpo necesita saber si es chunked.
const char *CABECERAS[] = {"Transfer-Encoding"};

// Protege el vigilante y cliente_en_uso, que comparten la tarea de red y loop(). Nunca se tiene
// tomado durante una petición.
SemaphoreHandle_t candado = nullptr;
VigilanteRed vigilante;
NetworkClientSecure *cliente_en_uso = nullptr;  // el de la petición en curso (para cortarla)
char buffer_respuesta[FIREBASE_RESPUESTA_MAX];

class Bloqueo {
public:
    Bloqueo() { xSemaphoreTake(candado, portMAX_DELAY); }
    ~Bloqueo() { xSemaphoreGive(candado); }
};

unsigned long segundosDesde(uint32_t ms) {
    return static_cast<unsigned long>((millis() - ms) / 1000);
}

// "HTTP 204", "HTTP 401 Permission denied" o "error -11 read Timeout"; si no se pudo conectar,
// con el error TLS de esta conexión. tls_antes: describir() solo lo muestra si cambió, porque
// ssl_client lo guarda entre conexiones.
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

}  // namespace

bool iniciar() {
    candado = xSemaphoreCreateMutex();
    if (candado != nullptr) {
        latir();
    }
    return candado != nullptr;
}

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

int empezar(const char *metodo, const char *ruta, NetworkClientSecure &cliente) {
    char texto[8];
    const int tls_antes = cliente.lastError(texto, sizeof(texto));
    watchdog_tareas::pausarEstaTarea();
    Bloqueo b;
    vigilante.empezar(metodo, ruta, millis());
    cliente_en_uso = &cliente;
    return tls_antes;
}

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

bool terminar(const char *modulo, int codigo, const String &cuerpo, NetworkClientSecure &cliente,
              int tls_antes, PeticionTerminada &t) {
    t = {};
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
    watchdog_tareas::reanudarEstaTarea();
    t.codigo = codigo;
    describir(codigo, cuerpo, cliente, tls_antes, t.texto, sizeof(t.texto));
    if (cortada) {
        strlcat(t.texto, " (cortada por el vigilante)", sizeof(t.texto));
    }
    if (FIREBASE_TRAZA || cortada) {
        registrar(modulo, "%s %s -> %s (%lu ms)", t.metodo, t.ruta, t.texto,
                  static_cast<unsigned long>(t.duracion_ms));
    }
    return cortada;
}

String textoError(int codigo, const String &cuerpo) {
    if (codigo < 0) {
        return HTTPClient::errorToString(codigo);
    }
    char texto[80];
    const char *claves[] = {"message", "error", "description"};
    for (const char *clave : claves) {
        if (formato_nube::extraerTexto(cuerpo.c_str(), clave, texto, sizeof(texto))) {
            return String(texto);
        }
    }
    return String();
}

void latir() {
    Bloqueo b;
    vigilante.latir(millis());
}

AccionVigilante vigilar(bool alerta_urgente, char *motivo, size_t tamano) {
    static uint32_t ultima_ms = 0;
    static bool pospuesto_antes = false;
    if (candado == nullptr || millis() - ultima_ms < FIREBASE_VIGILANTE_PERIODO_MS) {
        return VIGILANTE_NADA;
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
        return accion;
    }
    describirBloqueo(p, motivo, tamano);
    if (accion == VIGILANTE_CORTAR) {
        registrar("vigilante", "%s; se cierra su socket", motivo);
    } else if (pospuesto && !pospuesto_antes) {
        registrar("vigilante", "%s; reinicio pospuesto hasta que termine la alerta local", motivo);
    }
    pospuesto_antes = pospuesto;
    return accion;
}

void imprimirEstado() {
    if (candado == nullptr) {
        return;
    }
    PeticionEnCurso p;
    uint32_t cortes;
    bool pospuesto;
    {
        Bloqueo b;
        p = vigilante.peticion();
        cortes = vigilante.cortes();
        pospuesto = vigilante.reinicioPospuesto();
    }
    if (p.activa) {
        const uint32_t lleva_ms = millis() - p.inicio_ms;
        Serial.printf("# Red: peticion en curso: %s %s, lleva %lu.%lu s (corte a los %lu s)\n",
                      p.metodo, p.ruta, static_cast<unsigned long>(lleva_ms / 1000),
                      static_cast<unsigned long>(lleva_ms % 1000 / 100),
                      static_cast<unsigned long>(FIREBASE_PETICION_MAX_MS / 1000));
    } else {
        Serial.println("# Red: ninguna peticion en curso");
    }
    Serial.printf("# Red: vigilante: %lu cortes%s\n", static_cast<unsigned long>(cortes),
                  pospuesto ? ", reinicio pospuesto por la alerta local" : "");
}

}  // namespace peticion_https
