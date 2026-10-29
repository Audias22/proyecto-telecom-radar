// test_logica.cpp
// Pruebas unitarias en host (PC) de la lógica pura de la base: ventana de actividad del radar
// (persona / objetivo), JSON que se escribe en Firebase, cola de peticiones a la tarea de red,
// lector del cuerpo de las respuestas HTTP, vigilante de la tarea de red, cola offline, detectores
// de eventos y avisos de Telegram.
//
// Incluye los headers y .cpp reales (sin Arduino); no duplica su lógica. Los JSON esperados son
// los mismos que escribe web/simulador/simulador.py, que pasó la verificación contra las reglas
// publicadas. Ver pruebas_host/README.md para compilar y ejecutar.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "cola_peticiones.h"
#include "cuerpo_http.h"
#include "detectores.h"
#include "formato_nube.h"
#include "mensajes_telegram.h"
#include "ventana_actividad.h"
#include "vigilante_red.h"

namespace {

int verificaciones = 0;
int casos = 0;
int casos_fallidos = 0;
bool caso_actual_ok = true;

#define VERIFICAR(condicion)                                                      \
    do {                                                                          \
        ++verificaciones;                                                         \
        if (!(condicion)) {                                                       \
            caso_actual_ok = false;                                               \
            std::printf("    FALLO %s:%d: %s\n", __FILE__, __LINE__, #condicion); \
        }                                                                         \
    } while (0)

#define VERIFICAR_TEXTO(obtenido, esperado)                                         \
    do {                                                                            \
        ++verificaciones;                                                           \
        if (std::strcmp((obtenido), (esperado)) != 0) {                             \
            caso_actual_ok = false;                                                 \
            std::printf("    FALLO %s:%d:\n      obtenido %s\n      esperado %s\n", \
                        __FILE__, __LINE__, (obtenido), (esperado));                \
        }                                                                           \
    } while (0)

#define VERIFICAR_CERCA(obtenido, esperado) VERIFICAR(std::fabs((obtenido) - (esperado)) < 1e-6f)

void ejecutar(const char *nombre, void (*caso)()) {
    ++casos;
    caso_actual_ok = true;
    caso();
    if (!caso_actual_ok) {
        ++casos_fallidos;
    }
    std::printf("[%s] %s\n", caso_actual_ok ? " OK  " : "FALLO", nombre);
}

// La ventana ocupa ~7.5 kB: estática para no cargar la pila.
VentanaActividad ventana;

LecturaNube lecturaBase() {
    LecturaNube l;
    l.millis_medicion = 0;
    l.persona = true;
    l.objetivo = true;
    l.actividad = 0.048f;
    l.distancia_cm = 85.0f;
    l.resp_rpm = 13.0f;
    l.latido_bpm = 72.0f;
    return l;
}

// ---------------------------------------------------------------------------
// Ventana de actividad
// ---------------------------------------------------------------------------

void ventanaVacia() {
    ventana.vaciar();
    const EstadoPersona e = ventana.evaluar(1000);
    VERIFICAR(!e.objetivo);
    VERIFICAR(!e.persona);
    VERIFICAR(e.actividad == 0.0f);
    VERIFICAR(e.muestras == 0);
}

void ventanaIgnoraCeros() {
    ventana.vaciar();
    for (uint32_t i = 0; i < 20; i++) {
        ventana.agregar(i * 62, 0.0f);
    }
    ventana.agregar(1300, NAN);
    const EstadoPersona e = ventana.evaluar(1400);
    VERIFICAR(!e.objetivo);  // sin objetivo: el radar manda todo en 0
    VERIFICAR(!e.persona);
    ventana.agregar(1400, -0.2f);
    ventana.agregar(1450, 0.0f);
    const EstadoPersona e2 = ventana.evaluar(1500);
    VERIFICAR(e2.objetivo);
    VERIFICAR(e2.muestras == 1);
    VERIFICAR_CERCA(e2.actividad, 0.2f);  // valor absoluto
}

void ventanaMedianaImpar() {
    ventana.vaciar();
    ventana.agregar(0, 0.01f);
    ventana.agregar(60, -0.05f);
    ventana.agregar(120, 0.02f);
    const EstadoPersona e = ventana.evaluar(200);
    VERIFICAR_CERCA(e.actividad, 0.02f);
    VERIFICAR(e.objetivo);
    VERIFICAR(!e.persona);  // 0.02 < 0.03: objetivo con actividad baja
}

void ventanaMedianaPar() {
    // Como numpy.median: promedio de los dos del medio.
    ventana.vaciar();
    ventana.agregar(0, 0.04f);
    ventana.agregar(60, 0.10f);
    ventana.agregar(120, -0.02f);
    ventana.agregar(180, 0.001f);
    const EstadoPersona e = ventana.evaluar(200);
    VERIFICAR_CERCA(e.actividad, 0.03f);
    VERIFICAR(e.muestras == 4);
}

void ventanaUmbral() {
    ventana.vaciar();
    ventana.agregar(0, RADAR_UMBRAL_PERSONA);
    VERIFICAR(ventana.evaluar(10).persona);  // >= 0.03 es persona
    ventana.vaciar();
    ventana.agregar(0, 0.0299f);
    VERIFICAR(!ventana.evaluar(10).persona);
}

void ventanaDeslizante() {
    ventana.vaciar();
    ventana.agregar(1000, 0.5f);
    VERIFICAR(ventana.evaluar(1000 + RADAR_VENTANA_ACTIVIDAD_MS - 1).objetivo);
    VERIFICAR(!ventana.evaluar(1000 + RADAR_VENTANA_ACTIVIDAD_MS).objetivo);  // (t-30 s, t]
    // La persona se va: con solo ceros, a los 30 s ya no hay objetivo.
    ventana.vaciar();
    for (uint32_t t = 0; t < 30000; t += 62) {
        ventana.agregar(t, 0.05f);
    }
    for (uint32_t t = 30000; t < 60000; t += 115) {
        ventana.agregar(t, 0.0f);
    }
    VERIFICAR(ventana.evaluar(45000).persona);
    VERIFICAR(!ventana.evaluar(60000).objetivo);
}

void ventanaCapacidad() {
    // 25 tramas/s durante 30 s = 750 > 640: quedan las 640 más nuevas.
    ventana.vaciar();
    for (uint32_t i = 0; i < 750; i++) {
        ventana.agregar(i * 40, i < 110 ? 5.0f : 0.05f);
    }
    const EstadoPersona e = ventana.evaluar(749 * 40);
    VERIFICAR(e.muestras == RADAR_CAPACIDAD_VENTANA);
    VERIFICAR_CERCA(e.actividad, 0.05f);  // las 110 viejas (5.0) se pisaron
}

void ventanaTasaReal() {
    // 16.2 tramas/s con objetivo, 1.5 % en 0 (docs/radar.md, 12.1).
    ventana.vaciar();
    uint32_t t = 0;
    for (int i = 0; i < 2000; i++) {
        t += (i % 2 == 0) ? 46 : 77;
        ventana.agregar(t, (i % 66 == 0) ? 0.0f : ((i % 2 == 0) ? 0.04f : -0.06f));
    }
    const EstadoPersona e = ventana.evaluar(t);
    VERIFICAR(e.muestras > 470 && e.muestras < 500);
    VERIFICAR(e.persona);
    VERIFICAR(e.actividad >= 0.04f && e.actividad <= 0.06f);
}

void ventanaVueltaMillis() {
    ventana.vaciar();
    ventana.agregar(0xFFFFFF00u, 0.07f);
    ventana.agregar(0x00000100u, 0.09f);
    const EstadoPersona e = ventana.evaluar(0x00000200u);
    VERIFICAR(e.muestras == 2);
    VERIFICAR_CERCA(e.actividad, 0.08f);
    VERIFICAR(ventana.evaluar(0xFFFFFF00u + RADAR_VENTANA_ACTIVIDAD_MS).muestras == 1);
}

// ---------------------------------------------------------------------------
// Formato de Firebase
// ---------------------------------------------------------------------------

void lecturaCompleta() {
    char json[192];
    VERIFICAR(formato_nube::jsonLectura(lecturaBase(), false, json, sizeof(json)) > 0);
    VERIFICAR_TEXTO(json, "{\"persona\":true,\"objetivo\":true,\"actividad\":0.048,"
                          "\"distancia_cm\":85,\"resp_rpm\":13.0,\"latido_bpm\":72}");
}

void lecturaSinObjetivo() {
    LecturaNube l = lecturaBase();
    l.objetivo = false;  // aunque llegue persona = true y valores, solo los obligatorios
    l.resp_rpm = 0.0f;
    l.latido_bpm = 0.0f;
    char json[192];
    formato_nube::jsonLectura(l, false, json, sizeof(json));
    VERIFICAR_TEXTO(json, "{\"persona\":false,\"objetivo\":false}");
}

void lecturaModuloEnCero() {
    // El caso más común dormido (docs/radar.md, 13.5): el módulo da 0 y no se envía.
    LecturaNube l = lecturaBase();
    l.persona = true;
    l.actividad = 0.0514f;
    l.distancia_cm = 84.4f;
    l.resp_rpm = 0.0f;
    l.latido_bpm = NAN;
    char json[192];
    formato_nube::jsonLectura(l, false, json, sizeof(json));
    VERIFICAR_TEXTO(json, "{\"persona\":true,\"objetivo\":true,\"actividad\":0.051,"
                          "\"distancia_cm\":84}");
}

void lecturaRangos() {
    LecturaNube l = lecturaBase();
    l.persona = false;
    l.actividad = 12.5f;     // se recorta a 10
    l.distancia_cm = NAN;    // sin distancia: se omite
    l.resp_rpm = 0.04f;      // redondeado da 0.0: se omite (las reglas piden > 0)
    l.latido_bpm = 300.0f;   // > 250: se omite
    char json[192];
    formato_nube::jsonLectura(l, true, json, sizeof(json));
    VERIFICAR_TEXTO(json, "{\"persona\":false,\"objetivo\":true,\"actividad\":10.000,"
                          "\"diferida\":true}");
    l.resp_rpm = 99.0f;
    l.latido_bpm = 0.4f;
    l.distancia_cm = 1200.0f;
    formato_nube::jsonLectura(l, false, json, sizeof(json));
    VERIFICAR(std::strstr(json, "resp_rpm") == nullptr);
    VERIFICAR(std::strstr(json, "latido_bpm") == nullptr);
    VERIFICAR(std::strstr(json, "distancia_cm") == nullptr);
    l.resp_rpm = 13.46f;
    l.latido_bpm = 71.6f;
    formato_nube::jsonLectura(l, false, json, sizeof(json));
    VERIFICAR(std::strstr(json, "\"resp_rpm\":13.5,\"latido_bpm\":72}") != nullptr);
}

void lecturaBufferChico() {
    char json[20];
    VERIFICAR(formato_nube::jsonLectura(lecturaBase(), false, json, sizeof(json)) == 0);
    VERIFICAR(json[0] == '\0');
}

void claveDeLectura() {
    char clave[16];
    VERIFICAR(formato_nube::claveLectura(1791234567890ULL, clave, sizeof(clave)));
    VERIFICAR_TEXTO(clave, "1791234567890");
    VERIFICAR(std::strlen(clave) == 13);
    VERIFICAR(!formato_nube::claveLectura(999999999999ULL, clave, sizeof(clave)));  // 12 dígitos
    VERIFICAR(!formato_nube::claveLectura(10000000000000ULL, clave, sizeof(clave)));
    VERIFICAR(!formato_nube::claveLectura(0, clave, sizeof(clave)));  // sin referencia
}

void horaServidor() {
    const uint64_t ref = 1791234567890ULL;
    VERIFICAR(formato_nube::millisAHoraServidor(ref, 60000, 65000) == ref + 5000);
    VERIFICAR(formato_nube::millisAHoraServidor(ref, 60000, 55000) == ref - 5000);  // antes
    VERIFICAR(formato_nube::millisAHoraServidor(ref, 0xFFFFF000u, 0x00001000u) == ref + 0x2000);
    VERIFICAR(formato_nube::millisAHoraServidor(ref, 0x00001000u, 0xFFFFF000u) == ref - 0x2000);
}

void estado() {
    char json[160];
    formato_nube::jsonEstado(-60, "0.5.0", json, sizeof(json));
    VERIFICAR_TEXTO(json, "{\"online\":true,\"ultimo_contacto\":{\".sv\":\"timestamp\"},"
                          "\"rssi_wifi\":-60,\"version_firmware\":\"0.5.0\"}");
    formato_nube::jsonEstado(-128, "x", json, sizeof(json));
    VERIFICAR(std::strstr(json, "\"rssi_wifi\":-127,") != nullptr);
    formato_nube::jsonEstado(3, "x", json, sizeof(json));
    VERIFICAR(std::strstr(json, "\"rssi_wifi\":0,") != nullptr);
}

void colgante() {
    char json[160];
    formato_nube::jsonColgante(3950, -70, "base01", json, sizeof(json));
    VERIFICAR_TEXTO(json, "{\"bateria_mv\":3950,\"ultimo_heartbeat\":{\".sv\":\"timestamp\"},"
                          "\"rssi\":-70,\"base\":\"base01\"}");
    formato_nube::jsonColgante(6000, -128, "base01", json, sizeof(json));
    VERIFICAR(std::strstr(json, "\"bateria_mv\":0,") != nullptr);  // fuera de rango: sin dato
    VERIFICAR(std::strstr(json, "\"rssi\":-127,") != nullptr);
}

void evento() {
    char json[320];
    formato_nube::jsonEvento(EVENTO_BOTON, "colgante1", "boton del colgante", json, sizeof(json));
    VERIFICAR_TEXTO(json, "{\"tipo\":\"BOTON\",\"origen\":\"colgante1\",\"timestamp\":"
                          "{\".sv\":\"timestamp\"},\"atendido\":false,"
                          "\"detalle\":\"boton del colgante\"}");
    formato_nube::jsonEvento(EVENTO_RADAR_SIN_DATOS, "base01", "", json, sizeof(json));
    VERIFICAR_TEXTO(json, "{\"tipo\":\"RADAR_SIN_DATOS\",\"origen\":\"base01\",\"timestamp\":"
                          "{\".sv\":\"timestamp\"},\"atendido\":false}");
    formato_nube::jsonEvento(EVENTO_BOTON, "colgante1", "dijo \"hola\"\n", json, sizeof(json));
    VERIFICAR(std::strstr(json, "\"detalle\":\"dijo \\\"hola\\\"\\u000a\"}") != nullptr);
    VERIFICAR_TEXTO(formato_nube::nombreEvento(EVENTO_BATERIA_BAJA), "BATERIA_BAJA");
    VERIFICAR_TEXTO(formato_nube::nombreEvento(EVENTO_BASE_DESCONECTADA), "BASE_DESCONECTADA");
    VERIFICAR_TEXTO(formato_nube::nombreEvento(EVENTO_COLGANTE_AUSENTE), "COLGANTE_AUSENTE");
}

void inicioSesion() {
    char json[256];
    formato_nube::jsonInicioSesion("base01@x.example", "cla\"ve\\", json, sizeof(json));
    VERIFICAR_TEXTO(json, "{\"email\":\"base01@x.example\",\"password\":\"cla\\\"ve\\\\\","
                          "\"returnSecureToken\":true}");
}

void respuestasAuth() {
    const char *inicio =
        "{\n  \"kind\": \"identitytoolkit#VerifyPasswordResponse\",\n"
        "  \"localId\": \"AbC123\",\n  \"email\": \"base01@x.example\",\n"
        "  \"idToken\": \"eyJhbGciOi.eyJpc3Mi.c2lnbmF0dXJl\",\n  \"registered\": true,\n"
        "  \"refreshToken\": \"AMf-vBx_9\",\n  \"expiresIn\": \"3600\"\n}\n";
    char texto[64];
    VERIFICAR(formato_nube::extraerTexto(inicio, "idToken", texto, sizeof(texto)));
    VERIFICAR_TEXTO(texto, "eyJhbGciOi.eyJpc3Mi.c2lnbmF0dXJl");
    VERIFICAR(formato_nube::extraerTexto(inicio, "refreshToken", texto, sizeof(texto)));
    VERIFICAR_TEXTO(texto, "AMf-vBx_9");
    VERIFICAR(formato_nube::extraerTexto(inicio, "localId", texto, sizeof(texto)));
    VERIFICAR_TEXTO(texto, "AbC123");
    uint64_t segundos = 0;
    VERIFICAR(formato_nube::extraerTexto(inicio, "expiresIn", texto, sizeof(texto)));
    VERIFICAR(formato_nube::leerEntero(texto, segundos) && segundos == 3600);
    VERIFICAR(!formato_nube::extraerTexto(inicio, "id_token", texto, sizeof(texto)));
    VERIFICAR(!formato_nube::extraerTexto(inicio, "registered", texto, sizeof(texto)));  // bool
    char chico[8];
    VERIFICAR(!formato_nube::extraerTexto(inicio, "idToken", chico, sizeof(chico)));

    const char *error = "{\"error\":{\"code\":400,\"message\":\"INVALID_LOGIN_CREDENTIALS\"}}";
    VERIFICAR(formato_nube::extraerTexto(error, "message", texto, sizeof(texto)));
    VERIFICAR_TEXTO(texto, "INVALID_LOGIN_CREDENTIALS");
    VERIFICAR(formato_nube::extraerTexto("{\n  \"error\" : \"Permission denied\"\n}\n", "error",
                                         texto, sizeof(texto)));
    VERIFICAR_TEXTO(texto, "Permission denied");
}

void enteros() {
    uint64_t v = 0;
    VERIFICAR(formato_nube::leerEntero("1791234567890", v) && v == 1791234567890ULL);
    VERIFICAR(formato_nube::leerEntero(" 1791234567890\n", v) && v == 1791234567890ULL);
    VERIFICAR(formato_nube::leerEntero("\"3600\"", v) && v == 3600);
    VERIFICAR(!formato_nube::leerEntero("null", v));
    VERIFICAR(!formato_nube::leerEntero("", v));
}

// ---------------------------------------------------------------------------
// Cuerpo de la respuesta HTTP
// ---------------------------------------------------------------------------

char cuerpo_destino[64];

size_t alimentar(CuerpoHttp &c, const char *texto) {
    return c.agregar(reinterpret_cast<const uint8_t *>(texto), std::strlen(texto));
}

// De a un byte, como puede llegar del socket TLS.
void alimentarDeAUno(CuerpoHttp &c, const char *texto) {
    for (size_t i = 0; texto[i] != '\0'; i++) {
        c.agregar(reinterpret_cast<const uint8_t *>(texto + i), 1);
    }
}

void cuerpoModo() {
    // El 204 de ?print=silent llega sin Content-Length (medido): no hay cuerpo que esperar.
    VERIFICAR(cuerpo_http::modo(204, -1, false) == CUERPO_VACIO);
    VERIFICAR(cuerpo_http::modo(304, -1, false) == CUERPO_VACIO);
    VERIFICAR(cuerpo_http::modo(101, -1, false) == CUERPO_VACIO);
    VERIFICAR(cuerpo_http::modo(200, 13, false) == CUERPO_LARGO);
    VERIFICAR(cuerpo_http::modo(401, 36, false) == CUERPO_LARGO);
    VERIFICAR(cuerpo_http::modo(200, -1, true) == CUERPO_CHUNKED);  // Firebase Auth
    VERIFICAR(cuerpo_http::modo(200, -1, false) == CUERPO_HASTA_CIERRE);
}

void cuerpoVacio() {
    CuerpoHttp c(cuerpo_destino, sizeof(cuerpo_destino));
    c.iniciar(CUERPO_VACIO, -1);
    VERIFICAR(c.completo());
    VERIFICAR(alimentar(c, "basura") == 0);
    VERIFICAR(c.largo() == 0);
    c.iniciar(CUERPO_LARGO, 0);
    VERIFICAR(c.completo());
}

void cuerpoLargo() {
    CuerpoHttp c(cuerpo_destino, sizeof(cuerpo_destino));
    c.iniciar(CUERPO_LARGO, 13);
    VERIFICAR(alimentar(c, "17912") == 5);
    VERIFICAR(!c.completo());  // si no llega el resto, el lector corta por tiempo
    VERIFICAR(alimentar(c, "34567890HTTP/1.1") == 8);  // no consume más allá del cuerpo
    VERIFICAR(c.completo() && !c.recortado());
    VERIFICAR_TEXTO(cuerpo_destino, "1791234567890");
}

void cuerpoChunked() {
    const char *respuesta = "5\r\nhola \r\n6;ext=1\r\nmundo!\r\n0\r\n\r\n";
    CuerpoHttp c(cuerpo_destino, sizeof(cuerpo_destino));
    c.iniciar(CUERPO_CHUNKED, -1);
    VERIFICAR(alimentar(c, respuesta) == std::strlen(respuesta));
    VERIFICAR(c.completo() && !c.invalido());
    VERIFICAR_TEXTO(cuerpo_destino, "hola mundo!");

    c.iniciar(CUERPO_CHUNKED, -1);
    alimentarDeAUno(c, "1A\r\nabcdefghijklmnopqrstuvwxyz\r\n0\r\n");
    VERIFICAR(!c.completo());  // falta la línea vacía final
    alimentarDeAUno(c, "\r\n");
    VERIFICAR(c.completo());
    VERIFICAR_TEXTO(cuerpo_destino, "abcdefghijklmnopqrstuvwxyz");
}

void cuerpoChunkedTrailerYLf() {
    CuerpoHttp c(cuerpo_destino, sizeof(cuerpo_destino));
    c.iniciar(CUERPO_CHUNKED, -1);
    alimentar(c, "3\r\nabc\r\n0\r\nX-Algo: 1\r\n");
    VERIFICAR(!c.completo());
    alimentar(c, "\r\n");
    VERIFICAR(c.completo());
    VERIFICAR_TEXTO(cuerpo_destino, "abc");

    c.iniciar(CUERPO_CHUNKED, -1);
    alimentar(c, "2\nok\n0\n\n");  // solo LF
    VERIFICAR(c.completo());
    VERIFICAR_TEXTO(cuerpo_destino, "ok");
}

void cuerpoChunkedInvalido() {
    CuerpoHttp c(cuerpo_destino, sizeof(cuerpo_destino));
    c.iniciar(CUERPO_CHUNKED, -1);
    alimentar(c, "zz\r\n");
    VERIFICAR(c.invalido() && !c.completo());

    c.iniciar(CUERPO_CHUNKED, -1);
    alimentar(c, "3\r\nabcX\r\n");  // faltó el CRLF después de los datos
    VERIFICAR(c.invalido());

    c.iniciar(CUERPO_CHUNKED, -1);
    alimentar(c, "123456789\r\n");  // tamaño absurdo
    VERIFICAR(c.invalido());

    c.iniciar(CUERPO_CHUNKED, -1);
    alimentar(c, "\r\n");  // línea sin tamaño
    VERIFICAR(c.invalido());
}

void cuerpoRecortado() {
    char chico[8];
    CuerpoHttp c(chico, sizeof(chico));
    c.iniciar(CUERPO_LARGO, 20);
    VERIFICAR(alimentar(c, "01234567890123456789") == 20);  // consume todo: conexión limpia
    VERIFICAR(c.completo() && c.recortado());
    VERIFICAR(c.largo() == 7);
    VERIFICAR_TEXTO(chico, "0123456");
}

void cuerpoHastaCierre() {
    CuerpoHttp c(cuerpo_destino, sizeof(cuerpo_destino));
    c.iniciar(CUERPO_HASTA_CIERRE, -1);
    alimentar(c, "{\"a\":1}");
    VERIFICAR(!c.completo());
    c.cerrada();
    VERIFICAR(c.completo());
    VERIFICAR_TEXTO(cuerpo_destino, "{\"a\":1}");

    // Un cierre a mitad de un cuerpo con largo no lo completa.
    c.iniciar(CUERPO_LARGO, 13);
    alimentar(c, "179");
    c.cerrada();
    VERIFICAR(!c.completo());
}

// ---------------------------------------------------------------------------
// Vigilante de la tarea de red
// ---------------------------------------------------------------------------

constexpr uint32_t MAX_MS = FIREBASE_PETICION_MAX_MS;
constexpr uint32_t TRAS_CORTE_MS = FIREBASE_REINICIO_TRAS_CORTE_MS;

void vigilanteTareaSana() {
    VigilanteRed v;
    v.latir(0);
    // Una vuelta cada 500 ms (lo más lento de la tarea) y peticiones cortas: nunca actúa.
    for (uint32_t t = 0; t < 10 * MAX_MS; t += 500) {
        if (t % 5000 == 0) {
            v.empezar("PUT", "lecturas/base01/1791234567890", t);
            VERIFICAR(v.revisar(t + 300, false) == VIGILANTE_NADA);
            VERIFICAR(!v.terminar(t + 400));
        } else {
            v.latir(t);
        }
        VERIFICAR(v.revisar(t + 450, false) == VIGILANTE_NADA);
    }
    VERIFICAR(v.cortes() == 0);
}

void vigilanteCortaYReinicia() {
    VigilanteRed v;
    v.latir(1000);
    v.empezar("PATCH", "dispositivos/base01/estado", 1000);
    VERIFICAR(v.peticion().activa);
    VERIFICAR(v.revisar(1000 + MAX_MS - 1, false) == VIGILANTE_NADA);
    VERIFICAR(v.revisar(1000 + MAX_MS, false) == VIGILANTE_CORTAR);
    VERIFICAR(v.cortes() == 1);
    // Un solo corte por petición; después se espera a que la tarea reaccione.
    VERIFICAR(v.revisar(1000 + MAX_MS + 1000, false) == VIGILANTE_NADA);
    VERIFICAR(v.revisar(1000 + MAX_MS + TRAS_CORTE_MS - 1, false) == VIGILANTE_NADA);
    VERIFICAR(v.revisar(1000 + MAX_MS + TRAS_CORTE_MS, false) == VIGILANTE_REINICIAR);
    VERIFICAR(v.cortes() == 1);
}

void vigilanteCorteRecupera() {
    VigilanteRed v;
    v.latir(0);
    v.empezar("GET", "dispositivos/base01/estado/ultimo_contacto", 0);
    VERIFICAR(v.revisar(MAX_MS, false) == VIGILANTE_CORTAR);
    // El corte despierta a la tarea: termina con error y sigue.
    VERIFICAR(v.terminar(MAX_MS + 200));
    VERIFICAR(!v.peticion().activa);
    // terminar() cuenta como vuelta de la tarea: pasado el plazo del reinicio no hace nada.
    VERIFICAR(v.revisar(MAX_MS + TRAS_CORTE_MS, false) == VIGILANTE_NADA);
    // Una petición nueva se vigila desde cero y se puede volver a cortar.
    const uint32_t t = 200000;
    v.empezar("POST", "eventos", t);
    VERIFICAR(!v.terminar(t + 300));
    v.empezar("POST", "eventos", t + 1000);
    VERIFICAR(v.revisar(t + 1000 + MAX_MS, false) == VIGILANTE_CORTAR);
    VERIFICAR(v.cortes() == 2);
}

void vigilanteAlertaPospone() {
    VigilanteRed v;
    v.latir(0);
    v.empezar("PATCH", "colgantes/1", 0);
    // El corte no espera a la alerta: no reinicia nada y es la recuperación normal.
    VERIFICAR(v.revisar(MAX_MS, true) == VIGILANTE_CORTAR);
    const uint32_t reinicio = MAX_MS + TRAS_CORTE_MS;
    VERIFICAR(v.revisar(reinicio, true) == VIGILANTE_NADA);
    VERIFICAR(v.reinicioPospuesto());
    VERIFICAR(v.revisar(reinicio + 59000, true) == VIGILANTE_NADA);
    VERIFICAR(v.revisar(reinicio + 60000, false) == VIGILANTE_REINICIAR);
}

void vigilanteTareaSinVueltas() {
    VigilanteRed v;
    v.latir(5000);
    // Fuera de una petición no hay socket que cortar: se pasa directo al reinicio.
    VERIFICAR(v.revisar(5000 + MAX_MS - 1, false) == VIGILANTE_NADA);
    VERIFICAR(v.revisar(5000 + MAX_MS, true) == VIGILANTE_NADA);
    VERIFICAR(v.reinicioPospuesto());
    VERIFICAR(v.revisar(5000 + MAX_MS, false) == VIGILANTE_REINICIAR);
    VERIFICAR(v.cortes() == 0);
    v.latir(5000 + MAX_MS + 1);
    VERIFICAR(!v.reinicioPospuesto());
}

void vigilanteVueltaMillis() {
    VigilanteRed v;
    const uint32_t inicio = 0xFFFFFFFFu - 10000;
    v.latir(inicio);
    v.empezar("PUT", "lecturas/base01/1791234567890", inicio);
    VERIFICAR(v.revisar(inicio + 20000, false) == VIGILANTE_NADA);  // ya dio la vuelta
    VERIFICAR(v.revisar(inicio + MAX_MS, false) == VIGILANTE_CORTAR);
}

// loop() lee millis() y recién después toma el candado; mientras tanto la tarea de red puede anotar
// una vuelta o empezar una petición con un millis() posterior (falla del 2026-10-28: reinicio con
// la red sana). Una marca posterior a `ahora` es reciente, no una tarea trabada.
void vigilanteMarcaPosterior() {
    const uint32_t ahora = 331750;
    VigilanteRed v;
    v.latir(ahora + 1);
    VERIFICAR(v.revisar(ahora, false) == VIGILANTE_NADA);
    v.latir(ahora + 3500);
    VERIFICAR(v.revisar(ahora, false) == VIGILANTE_NADA);
    v.empezar("PUT", "lecturas/base01/1793242608063", ahora + 1);
    VERIFICAR(v.revisar(ahora, false) == VIGILANTE_NADA);
    VERIFICAR(v.cortes() == 0);
    VERIFICAR(!v.terminar(ahora + 200));
    // Lo mismo justo en la vuelta de millis().
    v.latir(0);
    VERIFICAR(v.revisar(0xFFFFFFFFu, false) == VIGILANTE_NADA);
    // La protección sigue: desde la marca, a los 45 s corta y 30 s después reinicia.
    VERIFICAR(v.revisar(MAX_MS - 1, false) == VIGILANTE_NADA);
    VERIFICAR(v.revisar(MAX_MS, false) == VIGILANTE_REINICIAR);
    v.empezar("PUT", "lecturas/base01/1793242608063", ahora + 1);
    VERIFICAR(v.revisar(ahora + 1 + MAX_MS, false) == VIGILANTE_CORTAR);
    VERIFICAR(v.revisar(ahora + 1 + MAX_MS + TRAS_CORTE_MS, false) == VIGILANTE_REINICIAR);
}

void vigilanteRutaLarga() {
    VigilanteRed v;
    char ruta[100];
    std::memset(ruta, 'x', sizeof(ruta) - 1);
    ruta[sizeof(ruta) - 1] = '\0';
    v.empezar("DEMASIADOLARGO", ruta, 0);
    VERIFICAR(std::strlen(v.peticion().ruta) == sizeof(v.peticion().ruta) - 1);
    VERIFICAR_TEXTO(v.peticion().metodo, "DEMASIA");
}

// ---------------------------------------------------------------------------
// Cola de peticiones
// ---------------------------------------------------------------------------

PeticionEvento eventoBoton(int n) {
    PeticionEvento e{};
    e.tipo = EVENTO_BOTON;
    e.destinos = DESTINO_FIREBASE | DESTINO_TELEGRAM;
    std::snprintf(e.origen, sizeof(e.origen), "colgante%d", n);
    return e;
}

LecturaNube lecturaEn(uint32_t ms) {
    LecturaNube l = lecturaBase();
    l.millis_medicion = ms;
    return l;
}

void colaOrden() {
    ColaPeticiones cola;
    Peticion p;
    VERIFICAR(!cola.siguiente(true, p));
    cola.agregarLectura(lecturaEn(1));
    VERIFICAR(!cola.siguiente(false, p));  // sin hora del servidor no salen lecturas
    cola.agregarColgante({1, 3900, -60});
    cola.agregarEvento(eventoBoton(1));
    VERIFICAR(cola.siguiente(false, p) && p.tipo == PETICION_EVENTO);
    cola.confirmar(p);
    VERIFICAR(cola.siguiente(false, p) && p.tipo == PETICION_COLGANTE);
    cola.confirmar(p);
    VERIFICAR(!cola.siguiente(false, p));
    VERIFICAR(cola.siguiente(true, p) && p.tipo == PETICION_LECTURA && p.lectura.millis_medicion == 1);
    cola.confirmar(p);
    VERIFICAR(!cola.siguiente(true, p));
}

void colaReintentoNoSaca() {
    ColaPeticiones cola;
    cola.agregarEvento(eventoBoton(1));
    Peticion p;
    cola.siguiente(true, p);
    // Falló el envío: no se confirma y la misma petición vuelve a salir.
    Peticion otra;
    VERIFICAR(cola.siguiente(true, otra) && otra.numero == p.numero);
}

void colaLecturasLlena() {
    ColaPeticiones cola;
    for (uint32_t i = 0; i <= COLA_OFFLINE_LECTURAS; i++) {
        cola.agregarLectura(lecturaEn(i));
    }
    const TamanosCola t = cola.tamanos();
    VERIFICAR(t.lecturas == COLA_OFFLINE_LECTURAS);
    VERIFICAR(t.lecturas_descartadas == 1);
    Peticion p;
    VERIFICAR(cola.siguiente(true, p) && p.lectura.millis_medicion == 1);  // la 0 se descartó
}

void colaEventosNuncaDesplazados() {
    ColaPeticiones cola;
    for (int i = 0; i < COLA_EVENTOS_CAPACIDAD; i++) {
        VERIFICAR(cola.agregarEvento(eventoBoton(i)) == EVENTO_AGREGADO);
    }
    for (uint32_t i = 0; i < 3 * COLA_OFFLINE_LECTURAS; i++) {
        cola.agregarLectura(lecturaEn(i));
    }
    VERIFICAR(cola.tamanos().eventos == COLA_EVENTOS_CAPACIDAD);  // las lecturas no los tocan
    // Llena de BOTON: no hay a quién reemplazar y se rechaza el nuevo.
    VERIFICAR(cola.agregarEvento(eventoBoton(99)) == EVENTO_RECHAZADO);
    VERIFICAR(cola.tamanos().eventos_rechazados == 1);
    Peticion p;
    VERIFICAR(cola.siguiente(true, p) && std::strcmp(p.evento.origen, "colgante0") == 0);
}

void colaConfirmarTrasPisada() {
    // La tarea de red toma la lectura más antigua; mientras la envía, loop() agrega otra con la
    // cola llena y la pisa. Confirmar la vieja no debe sacar la nueva primera.
    ColaPeticiones cola;
    for (uint32_t i = 0; i < COLA_OFFLINE_LECTURAS; i++) {
        cola.agregarLectura(lecturaEn(i));
    }
    Peticion enviada;
    cola.siguiente(true, enviada);
    cola.agregarLectura(lecturaEn(100));
    cola.confirmar(enviada);
    VERIFICAR(cola.tamanos().lecturas == COLA_OFFLINE_LECTURAS);
    Peticion p;
    VERIFICAR(cola.siguiente(true, p) && p.lectura.millis_medicion == 1);
}

void colaColganteReemplaza() {
    ColaPeticiones cola;
    cola.agregarColgante({1, 3900, -60});
    Peticion enviada;
    cola.siguiente(true, enviada);
    cola.agregarColgante({1, 3800, -65});  // llega otro mensaje mientras se envía el primero
    VERIFICAR(cola.tamanos().colgantes == 1);
    cola.confirmar(enviada);
    Peticion p;
    VERIFICAR(cola.siguiente(true, p) && p.tipo == PETICION_COLGANTE);
    VERIFICAR(p.colgante.bateria_mv == 3800 && p.colgante.rssi == -65);
    cola.confirmar(p);
    VERIFICAR(cola.tamanos().colgantes == 0);
}

void colaColgantesLlena() {
    ColaPeticiones cola;
    for (uint8_t id = 1; id <= COLA_COLGANTES_CAPACIDAD + 1; id++) {
        cola.agregarColgante({id, 3900, -60});
    }
    VERIFICAR(cola.tamanos().colgantes == COLA_COLGANTES_CAPACIDAD);
    Peticion p;
    VERIFICAR(cola.siguiente(true, p) && p.colgante.id == 2);  // el 1 era el más antiguo
}

// ---------------------------------------------------------------------------
// Cola offline: eventos y lecturas sin red
// ---------------------------------------------------------------------------

PeticionEvento eventoDe(TipoEvento tipo, const char *origen, uint8_t destinos) {
    PeticionEvento e{};
    e.tipo = tipo;
    e.destinos = destinos;
    std::snprintf(e.origen, sizeof(e.origen), "%s", origen);
    std::snprintf(e.detalle, sizeof(e.detalle), "detalle de %s", origen);
    return e;
}

constexpr uint8_t AMBOS = DESTINO_FIREBASE | DESTINO_TELEGRAM;

// Una hora sin red: llegan lecturas, eventos y colgantes. Al volver, Firebase recibe primero los
// eventos en orden, después el colgante y después las lecturas en orden de medición.
void offlineOrdenAlReconectar() {
    ColaPeticiones cola;
    cola.agregarLectura(lecturaEn(1000));
    cola.agregarEvento(eventoDe(EVENTO_RADAR_SIN_DATOS, "base01", AMBOS));
    cola.agregarLectura(lecturaEn(2000));
    cola.agregarColgante({1, 3900, -60});
    cola.agregarEvento(eventoDe(EVENTO_BOTON, "colgante1", AMBOS));
    cola.agregarLectura(lecturaEn(3000));
    Peticion p;
    VERIFICAR(cola.siguiente(true, p) && p.tipo == PETICION_EVENTO &&
              p.evento.tipo == EVENTO_RADAR_SIN_DATOS);
    cola.confirmar(p);
    VERIFICAR(cola.siguiente(true, p) && p.tipo == PETICION_EVENTO && p.evento.tipo == EVENTO_BOTON);
    cola.confirmar(p);
    VERIFICAR(cola.siguiente(true, p) && p.tipo == PETICION_COLGANTE);
    cola.confirmar(p);
    for (uint32_t ms = 1000; ms <= 3000; ms += 1000) {
        VERIFICAR(cola.siguiente(true, p) && p.tipo == PETICION_LECTURA &&
                  p.lectura.millis_medicion == ms);
        cola.confirmar(p);
    }
    VERIFICAR(!cola.siguiente(true, p));
    // Telegram va por su cuenta: los dos eventos siguen pendientes para él, BOTON primero.
    PeticionEvento e;
    uint32_t numero;
    VERIFICAR(cola.tamanos().eventos == 2 && cola.tamanos().eventos_firebase == 0);
    VERIFICAR(cola.siguienteTelegram(e, numero) && e.tipo == EVENTO_BOTON);
    cola.confirmarTelegram(numero);
    VERIFICAR(cola.siguienteTelegram(e, numero) && e.tipo == EVENTO_RADAR_SIN_DATOS);
    cola.confirmarTelegram(numero);
    VERIFICAR(cola.tamanos().eventos == 0);
}

void offlineLecturasLimite() {
    static ColaLecturas lecturas;
    const uint32_t total = COLA_OFFLINE_LECTURAS + 50;  // más de 1 h sin red
    for (uint32_t i = 0; i < total; i++) {
        lecturas.agregar(lecturaEn(i * 5000), i + 1);
    }
    VERIFICAR(lecturas.cantidad() == COLA_OFFLINE_LECTURAS);
    VERIFICAR(lecturas.descartadas() == 50);
    LecturaNube l;
    uint32_t numero;
    VERIFICAR(lecturas.primera(l, numero) && l.millis_medicion == 50 * 5000);  // las 50 más viejas
    lecturas.confirmar(numero + 1);  // número equivocado: no saca nada
    VERIFICAR(lecturas.cantidad() == COLA_OFFLINE_LECTURAS);
    lecturas.confirmar(numero);
    VERIFICAR(lecturas.primera(l, numero) && l.millis_medicion == 51 * 5000);
}

void offlineEventosDestinos() {
    ColaEventos eventos;
    VERIFICAR(eventos.agregar(eventoDe(EVENTO_BOTON, "colgante1", 0), 1) == EVENTO_RECHAZADO);
    VERIFICAR(eventos.agregar(eventoDe(EVENTO_REINICIO, "base01", DESTINO_TELEGRAM), 2) ==
              EVENTO_AGREGADO);
    PeticionEvento e;
    uint32_t numero;
    VERIFICAR(!eventos.siguiente(DESTINO_FIREBASE, e, numero));  // REINICIO no va a Firebase
    VERIFICAR(eventos.siguiente(DESTINO_TELEGRAM, e, numero) && numero == 2);
    VERIFICAR(eventos.cambio());
    eventos.limpiarCambio();
    eventos.confirmar(DESTINO_FIREBASE, 2);  // no tenía ese destino: nada cambia
    VERIFICAR(!eventos.cambio() && eventos.cantidad() == 1);
    eventos.confirmar(DESTINO_TELEGRAM, 2);
    VERIFICAR(eventos.cambio() && eventos.cantidad() == 0);
}

// Con la cola llena un BOTON reemplaza al evento más antiguo que no es BOTON; otro tipo se
// rechaza.
void offlineBotonNuncaSePierde() {
    ColaEventos eventos;
    uint32_t n = 0;
    eventos.agregar(eventoDe(EVENTO_BOTON, "colgante1", AMBOS), ++n);
    for (int i = 1; i < COLA_EVENTOS_CAPACIDAD; i++) {
        eventos.agregar(eventoDe(EVENTO_COLGANTE_AUSENTE, "colgante2", AMBOS), ++n);
    }
    VERIFICAR(eventos.agregar(eventoDe(EVENTO_RADAR_SIN_DATOS, "base01", AMBOS), ++n) ==
              EVENTO_RECHAZADO);
    VERIFICAR(eventos.agregar(eventoDe(EVENTO_BOTON, "colgante3", AMBOS), ++n) ==
              EVENTO_AGREGADO_REEMPLAZANDO);
    VERIFICAR(eventos.rechazados() == 1 && eventos.reemplazados() == 1);
    VERIFICAR(eventos.cantidad() == COLA_EVENTOS_CAPACIDAD);
    // El reemplazado fue el número 2 (el primer COLGANTE_AUSENTE); el 1 (BOTON) sigue.
    PeticionEvento e;
    uint32_t numero;
    VERIFICAR(eventos.siguiente(DESTINO_FIREBASE, e, numero) && numero == 1);
    eventos.confirmar(DESTINO_FIREBASE, 1);
    VERIFICAR(eventos.siguiente(DESTINO_FIREBASE, e, numero) && numero == 3);
    VERIFICAR(eventos.siguiente(DESTINO_TELEGRAM, e, numero) && numero == 1);
    eventos.confirmar(DESTINO_TELEGRAM, 1);
    VERIFICAR(eventos.siguiente(DESTINO_TELEGRAM, e, numero) &&
              std::strcmp(e.origen, "colgante3") == 0);
}

// Guardar en NVS y restaurar tras un reinicio: mismo orden, marcados previo_reinicio, solo los
// destinos activos ahora.
void offlineEventosSobrevivenReinicio() {
    static ColaPeticiones antes;
    antes.agregarEvento(eventoDe(EVENTO_BATERIA_BAJA, "colgante1", AMBOS));
    antes.agregarEvento(eventoDe(EVENTO_BOTON, "colgante1", AMBOS));
    antes.agregarEvento(eventoDe(EVENTO_REINICIO, "base01", DESTINO_TELEGRAM));
    Peticion p;
    antes.siguiente(true, p);
    antes.confirmar(p);  // BATERIA_BAJA ya llegó a Firebase; le falta Telegram
    VERIFICAR(antes.eventosCambiaron());
    uint8_t datos[ColaEventos::TAMANO_SERIALIZADO];
    const size_t largo = antes.serializarEventos(datos, sizeof(datos));
    VERIFICAR(largo == 4 + 3 * sizeof(PeticionEvento) && !antes.eventosCambiaron());

    static ColaPeticiones despues;
    VERIFICAR(despues.restaurarEventos(datos, largo, AMBOS) == 3);
    const TamanosCola t = despues.tamanos();
    VERIFICAR(t.eventos == 3 && t.eventos_firebase == 1 && t.eventos_telegram == 3);
    VERIFICAR(despues.siguiente(true, p) && p.evento.tipo == EVENTO_BOTON &&
              p.evento.previo_reinicio);
    PeticionEvento e;
    uint32_t numero;
    VERIFICAR(despues.siguienteTelegram(e, numero) && e.tipo == EVENTO_BOTON);
    despues.confirmarTelegram(numero);
    VERIFICAR(despues.siguienteTelegram(e, numero) && e.tipo == EVENTO_BATERIA_BAJA);

    // Ahora Telegram está desactivado: lo que solo iba a Telegram no se restaura.
    static ColaPeticiones sin_telegram;
    VERIFICAR(sin_telegram.restaurarEventos(datos, largo, DESTINO_FIREBASE) == 1);
    // Datos de otro formato o cortados: se ignoran.
    VERIFICAR(sin_telegram.restaurarEventos(datos, largo - 1, AMBOS) == -1);
    datos[2] ^= 0x01;
    VERIFICAR(sin_telegram.restaurarEventos(datos, largo, AMBOS) == -1);
    VERIFICAR(antes.serializarEventos(datos, 10) == 0);  // no cabe
}

void offlineSerializaVacia() {
    ColaEventos eventos;
    uint8_t datos[8];
    VERIFICAR(eventos.serializar(datos, sizeof(datos)) == 4 && datos[1] == 0);
    uint32_t numero = 0;
    VERIFICAR(eventos.restaurar(datos, 4, AMBOS, numero) == 0);
}

// ---------------------------------------------------------------------------
// Detectores de eventos
// ---------------------------------------------------------------------------

void bateriaHisteresis() {
    DetectorBateria d;
    VERIFICAR(!d.revisar(false, 3600));
    VERIFICAR(d.revisar(true, 3490));        // primera vez con la bandera: evento
    VERIFICAR(!d.revisar(true, 3480));       // sigue baja: nada
    VERIFICAR(!d.revisar(false, 3510));      // vaivén sobre el umbral: no rearma
    VERIFICAR(!d.revisar(true, 3495));       // vuelve a bajar: misma descarga, nada
    VERIFICAR(!d.revisar(false, 0));         // sin dato: no rearma
    VERIFICAR(!d.revisar(false, BATERIA_RECUPERADA_MV - 1));
    VERIFICAR(d.avisada());
    VERIFICAR(!d.revisar(false, BATERIA_RECUPERADA_MV));  // cargada: rearma
    VERIFICAR(!d.avisada());
    VERIFICAR(d.revisar(true, 3450));        // descarga nueva: evento nuevo
}

void episodioEsperas() {
    Episodio e(25000, 60000);
    VERIFICAR(e.actualizar(true, 0) == EPISODIO_SIN_CAMBIO);
    VERIFICAR(e.actualizar(true, 24999) == EPISODIO_SIN_CAMBIO);
    VERIFICAR(e.actualizar(false, 25000) == EPISODIO_SIN_CAMBIO);  // se cortó: cuenta de cero
    VERIFICAR(e.actualizar(true, 30000) == EPISODIO_SIN_CAMBIO);
    VERIFICAR(e.actualizar(true, 55000) == EPISODIO_EMPIEZA);
    for (uint32_t t = 56000; t < 200000; t += 1000) {
        VERIFICAR(e.actualizar(true, t) == EPISODIO_SIN_CAMBIO);  // una sola vez por episodio
    }
    VERIFICAR(e.actualizar(false, 200000) == EPISODIO_SIN_CAMBIO);
    VERIFICAR(e.actualizar(true, 230000) == EPISODIO_SIN_CAMBIO);   // el radar va y viene
    VERIFICAR(e.actualizar(false, 231000) == EPISODIO_SIN_CAMBIO);
    VERIFICAR(e.actualizar(false, 290999) == EPISODIO_SIN_CAMBIO);
    VERIFICAR(e.actualizar(false, 291000) == EPISODIO_TERMINA);
    VERIFICAR(!e.activo());
    VERIFICAR(e.actualizar(true, 300000) == EPISODIO_SIN_CAMBIO);
    VERIFICAR(e.actualizar(true, 325000) == EPISODIO_EMPIEZA);      // episodio nuevo
}

void episodioVueltaMillis() {
    Episodio e(25000, 60000);
    const uint32_t casi = 0xFFFFFFFFu - 10000;
    VERIFICAR(e.actualizar(true, casi) == EPISODIO_SIN_CAMBIO);
    VERIFICAR(e.actualizar(true, casi + 25000) == EPISODIO_EMPIEZA);  // pasó por 0
}

void colganteAusenteUnaVez() {
    RegistroColgantes r;
    uint8_t ids[RegistroColgantes::MAXIMO];
    r.mensaje(1, false, 3900, 0);
    r.mensaje(2, false, 3900, 600000);
    VERIFICAR(r.revisarAusentes(COLGANTE_AUSENTE_MS - 1, ids, 8) == 0);
    VERIFICAR(r.revisarAusentes(COLGANTE_AUSENTE_MS, ids, 8) == 1 && ids[0] == 1);
    VERIFICAR(r.ausente(1) && !r.ausente(2));
    for (uint32_t t = COLGANTE_AUSENTE_MS; t < 3 * COLGANTE_AUSENTE_MS; t += 60000) {
        const size_t n = r.revisarAusentes(t, ids, 8);
        VERIFICAR(n == 0 || (n == 1 && ids[0] == 2));  // el 1 no se repite
    }
    VERIFICAR(r.ausente(2));
    const ResultadoMensaje m = r.mensaje(1, false, 3900, 3 * COLGANTE_AUSENTE_MS);
    VERIFICAR(m.volvio && !m.bateria_baja && !r.ausente(1));
    VERIFICAR(r.revisarAusentes(4 * COLGANTE_AUSENTE_MS, ids, 8) == 1 && ids[0] == 1);  // nuevo
}

void colganteBateriaPorId() {
    RegistroColgantes r;
    VERIFICAR(r.mensaje(1, true, 3400, 0).bateria_baja);
    VERIFICAR(!r.mensaje(1, true, 3400, 1000).bateria_baja);
    VERIFICAR(r.mensaje(2, true, 3400, 2000).bateria_baja);  // otro colgante, otra descarga
    VERIFICAR(!r.mensaje(1, false, 3900, 3000).bateria_baja);
    VERIFICAR(r.mensaje(1, true, 3400, 4000).bateria_baja);
}

void colgantesLlenos() {
    RegistroColgantes r;
    for (uint8_t id = 1; id <= RegistroColgantes::MAXIMO; id++) {
        r.mensaje(id, false, 3900, id * 1000u);
    }
    r.mensaje(1, false, 3900, 20000);  // el 2 queda como el más antiguo
    r.mensaje(50, false, 3900, 21000);
    VERIFICAR(r.cantidad() == RegistroColgantes::MAXIMO);
    uint8_t ids[RegistroColgantes::MAXIMO];
    const size_t n = r.revisarAusentes(21000 + COLGANTE_AUSENTE_MS, ids, RegistroColgantes::MAXIMO);
    bool esta_el_2 = false;
    for (size_t k = 0; k < n; k++) {
        esta_el_2 = esta_el_2 || ids[k] == 2;
    }
    VERIFICAR(n == RegistroColgantes::MAXIMO && !esta_el_2);
}

// Un reinicio en medio de una descarga o de una ausencia no repite el aviso.
void detectoresSobrevivenReinicio() {
    RegistroColgantes antes;
    uint8_t ids[RegistroColgantes::MAXIMO];
    antes.mensaje(1, true, 3400, 0);
    antes.mensaje(2, false, 3900, 0);
    antes.revisarAusentes(COLGANTE_AUSENTE_MS, ids, 8);  // los dos ausentes
    antes.mensaje(1, true, 3400, COLGANTE_AUSENTE_MS + 1);  // el 1 vuelve, con batería baja
    VERIFICAR(antes.cambio());
    uint8_t datos[2 + RegistroColgantes::TAMANO_SERIALIZADO];
    const size_t largo = serializarEstado(antes, true, datos, sizeof(datos));
    VERIFICAR(largo == 2 + 2 + 2 * 2);

    RegistroColgantes despues;
    bool radar = false;
    const uint32_t arranque = 5000;
    VERIFICAR(restaurarEstado(datos, largo, arranque, despues, radar) && radar);
    VERIFICAR(despues.cantidad() == 2 && !despues.cambio());
    VERIFICAR(!despues.mensaje(1, true, 3400, arranque).bateria_baja);  // misma descarga
    VERIFICAR(despues.ausente(2));
    // Solo el 1 (sin mensajes desde el arranque) pasa a ausente; el 2 ya lo estaba y no se repite.
    VERIFICAR(despues.revisarAusentes(arranque + 2 * COLGANTE_AUSENTE_MS, ids, 8) == 1 &&
              ids[0] == 1);
    VERIFICAR(despues.mensaje(2, false, 3900, arranque + 1).volvio);
    // El 1 se conoce desde el arranque: sin mensajes, ausente a COLGANTE_AUSENTE_MS de ahí.
    RegistroColgantes otro;
    restaurarEstado(datos, largo, arranque, otro, radar);
    VERIFICAR(otro.revisarAusentes(arranque + COLGANTE_AUSENTE_MS - 1, ids, 8) == 0);
    VERIFICAR(otro.revisarAusentes(arranque + COLGANTE_AUSENTE_MS, ids, 8) == 1 && ids[0] == 1);
    datos[0] = 9;
    VERIFICAR(!restaurarEstado(datos, largo, arranque, otro, radar));
}

void baseDesconectada() {
    const uint64_t t = 1791234567890ULL;
    VERIFICAR(duracionDesconexion(false, 0, t) == 0);                 // primera vez: sin anterior
    VERIFICAR(duracionDesconexion(true, t, t + 60000) == 0);          // estado normal cada 60 s
    VERIFICAR(duracionDesconexion(true, t, t + BASE_DESCONECTADA_MIN_MS - 1) == 0);
    VERIFICAR(duracionDesconexion(true, t, t + BASE_DESCONECTADA_MIN_MS) == BASE_DESCONECTADA_MIN_MS);
    VERIFICAR(duracionDesconexion(true, t, t + 720000) == 720000);
    VERIFICAR(duracionDesconexion(true, t, t - 1000) == 0);           // hora hacia atrás
    char texto[24];
    textoDuracion(45000, texto, sizeof(texto));
    VERIFICAR_TEXTO(texto, "45 s");
    textoDuracion(720000, texto, sizeof(texto));
    VERIFICAR_TEXTO(texto, "12 min");
    textoDuracion((3 * 3600 + 5 * 60 + 59) * 1000ULL, texto, sizeof(texto));
    VERIFICAR_TEXTO(texto, "3 h 5 min");
    textoDuracion((2 * 86400 + 4 * 3600) * 1000ULL, texto, sizeof(texto));
    VERIFICAR_TEXTO(texto, "2 d 4 h");
}

// ---------------------------------------------------------------------------
// Telegram
// ---------------------------------------------------------------------------

PeticionEvento avisoDe(TipoEvento tipo, const char *origen, const char *detalle) {
    PeticionEvento e = eventoDe(tipo, origen, AMBOS);
    std::snprintf(e.detalle, sizeof(e.detalle), "%s", detalle);
    e.millis_creado = 1000;
    return e;
}

void telegramTextos() {
    char t[200];
    mensajes_telegram::texto(avisoDe(EVENTO_BOTON, "colgante1", "boton del colgante"), 2000, t,
                             sizeof(t));
    VERIFICAR_TEXTO(t, "ALERTA: botón de auxilio del colgante 1. Atender de inmediato.");
    mensajes_telegram::texto(avisoDe(EVENTO_BATERIA_BAJA, "colgante2", "3420 mV"), 2000, t,
                             sizeof(t));
    VERIFICAR_TEXTO(t, "Batería baja en el colgante 2 (3420 mV). Hay que cargarlo.");
    mensajes_telegram::texto(
        avisoDe(EVENTO_COLGANTE_AUSENTE, "colgante1", "sin mensajes por 45 min"), 2000, t,
        sizeof(t));
    VERIFICAR_TEXTO(t, "El colgante 1 no se comunica (sin mensajes por 45 min). Revisar la "
                       "batería y el alcance.");
    mensajes_telegram::texto(avisoDe(EVENTO_BASE_DESCONECTADA, "base01", "sin conexión por 12 min"),
                             2000, t, sizeof(t));
    VERIFICAR_TEXTO(t, "La base base01 volvió a estar en línea (sin conexión por 12 min).");
    mensajes_telegram::texto(avisoDe(EVENTO_REINICIO, "base01", "watchdog de tareas"), 2000, t,
                             sizeof(t));
    VERIFICAR_TEXTO(t, "La base base01 se reinició de forma inesperada: watchdog de tareas.");
    mensajes_telegram::texto(avisoDe(EVENTO_RADAR_SIN_DATOS, "base01", "sin datos por 30 s"), 2000,
                             t, sizeof(t));
    VERIFICAR(std::strstr(t, "El radar de la base base01 no envía datos (sin datos por 30 s)") == t);
    // Atrasado y de antes de un reinicio.
    mensajes_telegram::texto(avisoDe(EVENTO_BATERIA_BAJA, "colgante2", "3420 mV"), 1000 + 720000,
                             t, sizeof(t));
    VERIFICAR(std::strstr(t, "Hay que cargarlo. (ocurrió hace 12 min)") != nullptr);
    PeticionEvento previo = avisoDe(EVENTO_BOTON, "colgante1", "");
    previo.previo_reinicio = true;
    mensajes_telegram::texto(previo, 5, t, sizeof(t));
    VERIFICAR(std::strstr(t, "(aviso de antes de un reinicio de la base)") != nullptr);
    char chico[16];
    mensajes_telegram::texto(avisoDe(EVENTO_BOTON, "colgante1", ""), 2000, chico, sizeof(chico));
    VERIFICAR(std::strlen(chico) == sizeof(chico) - 1);  // recortado, terminado en '\0'
    VERIFICAR(mensajes_telegram::esUrgente(EVENTO_BOTON));
    VERIFICAR(!mensajes_telegram::esUrgente(EVENTO_BATERIA_BAJA));
}

void telegramJson() {
    char json[160];
    formato_nube::jsonTelegram("-1001234567890", "Batería \"baja\"", json, sizeof(json));
    VERIFICAR_TEXTO(json, "{\"chat_id\":\"-1001234567890\",\"text\":\"Batería \\\"baja\\\"\"}");
    uint64_t v = 0;
    VERIFICAR(formato_nube::extraerEntero(
                  "{\"ok\":false,\"error_code\":429,\"parameters\":{\"retry_after\": 17}}",
                  "retry_after", v) && v == 17);
    VERIFICAR(!formato_nube::extraerEntero("{\"retry_after\":\"x\"}", "retry_after", v));
    VERIFICAR(!formato_nube::extraerEntero("{\"ok\":true}", "retry_after", v));
    char texto[40];
    VERIFICAR(formato_nube::extraerTexto("{\"ok\":false,\"description\":\"Unauthorized\"}",
                                         "description", texto, sizeof(texto)));
    VERIFICAR_TEXTO(texto, "Unauthorized");
    VERIFICAR_TEXTO(formato_nube::nombreEvento(EVENTO_REINICIO), "REINICIO");
}

void telegramIntervalos() {
    LimiteTelegram l;
    VERIFICAR(l.puedeEnviar(0, false) && l.puedeEnviar(0, true));
    l.enviado(10000, false);
    VERIFICAR(!l.puedeEnviar(10000 + TELEGRAM_INTERVALO_MIN_MS - 1, false));
    VERIFICAR(l.espera(10000 + 1000, false) == TELEGRAM_INTERVALO_MIN_MS - 1000);
    VERIFICAR(l.puedeEnviar(10000 + TELEGRAM_INTERVALO_URGENTE_MS, true));  // un BOTON espera menos
    VERIFICAR(l.puedeEnviar(10000 + TELEGRAM_INTERVALO_MIN_MS, false));
}

void telegramTopePorHora() {
    LimiteTelegram l;
    uint32_t t = 0;
    for (int i = 0; i < TELEGRAM_MAX_POR_HORA; i++) {
        VERIFICAR(l.puedeEnviar(t, false));
        l.enviado(t, false);
        t += 60000;
    }
    VERIFICAR(!l.puedeEnviar(t, false));               // tope de la hora
    VERIFICAR(l.puedeEnviar(t, true));                 // el BOTON no cuenta ni espera el tope
    l.enviado(t, true);
    VERIFICAR(!l.puedeEnviar(3600000 - 1, false));
    VERIFICAR(l.puedeEnviar(3600000, false));          // el primero salió hace 1 h
    l.enviado(3600000, false);
    // El tope sigue lleno: el siguiente más antiguo salió en 60000 y se libera a la hora.
    VERIFICAR(!l.puedeEnviar(3600000 + 59999, false));
    VERIFICAR(l.puedeEnviar(3600000 + 60000, false));
}

void telegramEsperaTrasError() {
    LimiteTelegram l;
    l.fallo(0, 0);
    VERIFICAR(l.esperaError() == TELEGRAM_ESPERA_ERROR_MS);
    VERIFICAR(!l.puedeEnviar(TELEGRAM_ESPERA_ERROR_MS - 1, true));  // también el BOTON: no hay red
    VERIFICAR(l.puedeEnviar(TELEGRAM_ESPERA_ERROR_MS, true));
    for (int i = 0; i < 10; i++) {
        l.fallo(0, 0);
    }
    VERIFICAR(l.esperaError() == TELEGRAM_ESPERA_ERROR_MAXIMA_MS);
    l.fallo(0, 45000);  // 429 con retry_after 45 s: manda el servidor
    VERIFICAR(l.esperaError() == 45000 && !l.puedeEnviar(44999, true));
    l.enviado(50000, true);
    VERIFICAR(l.esperaError() == 0);
    l.fallo(60000, 0);
    VERIFICAR(l.esperaError() == TELEGRAM_ESPERA_ERROR_MS);  // empieza de nuevo
    l.sinError();
    VERIFICAR(l.puedeEnviar(60000 + TELEGRAM_INTERVALO_MIN_MS, false));
}

}  // namespace

int main() {
    ejecutar("ventana: vacia, sin objetivo", ventanaVacia);
    ejecutar("ventana: ignora tramas en 0 y NaN; usa |fase_total|", ventanaIgnoraCeros);
    ejecutar("ventana: mediana con cantidad impar", ventanaMedianaImpar);
    ejecutar("ventana: mediana con cantidad par (como numpy)", ventanaMedianaPar);
    ejecutar("ventana: umbral 0.03 incluido", ventanaUmbral);
    ejecutar("ventana: deslizante de 30 s", ventanaDeslizante);
    ejecutar("ventana: capacidad, se pisan las mas antiguas", ventanaCapacidad);
    ejecutar("ventana: tasa real de 16 tramas/s", ventanaTasaReal);
    ejecutar("ventana: vuelta de millis()", ventanaVueltaMillis);
    ejecutar("lectura: completa (ejemplo de modelo-datos.md)", lecturaCompleta);
    ejecutar("lectura: sin objetivo, solo obligatorios", lecturaSinObjetivo);
    ejecutar("lectura: modulo en 0, se omite", lecturaModuloEnCero);
    ejecutar("lectura: rangos de las reglas y diferida", lecturaRangos);
    ejecutar("lectura: buffer chico", lecturaBufferChico);
    ejecutar("lectura: clave de 13 digitos", claveDeLectura);
    ejecutar("hora del servidor desde millis()", horaServidor);
    ejecutar("estado", estado);
    ejecutar("colgante", colgante);
    ejecutar("evento", evento);
    ejecutar("cuerpo de signInWithPassword", inicioSesion);
    ejecutar("respuestas de Firebase Auth y errores", respuestasAuth);
    ejecutar("enteros de las respuestas", enteros);
    ejecutar("cola: orden eventos, colgantes, lecturas", colaOrden);
    ejecutar("cola: un envio fallido no saca la peticion", colaReintentoNoSaca);
    ejecutar("cola: lecturas llenas descartan la mas antigua", colaLecturasLlena);
    ejecutar("cola: las lecturas nunca desplazan eventos", colaEventosNuncaDesplazados);
    ejecutar("cola: confirmar tras una lectura pisada", colaConfirmarTrasPisada);
    ejecutar("cola: colgante reemplaza al pendiente del mismo id", colaColganteReemplaza);
    ejecutar("cola: mas colgantes que lugares", colaColgantesLlena);
    ejecutar("cuerpo: modo segun codigo y cabeceras (204 sin largo)", cuerpoModo);
    ejecutar("cuerpo: vacio y largo 0", cuerpoVacio);
    ejecutar("cuerpo: Content-Length, no consume de mas", cuerpoLargo);
    ejecutar("cuerpo: chunked entero y de a un byte", cuerpoChunked);
    ejecutar("cuerpo: chunked con trailer y solo LF", cuerpoChunkedTrailerYLf);
    ejecutar("cuerpo: chunked mal formado", cuerpoChunkedInvalido);
    ejecutar("cuerpo: no cabe, se consume y se marca", cuerpoRecortado);
    ejecutar("cuerpo: hasta que el servidor cierra", cuerpoHastaCierre);
    ejecutar("vigilante: tarea sana, nunca actua", vigilanteTareaSana);
    ejecutar("vigilante: corta a los 45 s y reinicia 30 s despues", vigilanteCortaYReinicia);
    ejecutar("vigilante: el corte recupera la tarea", vigilanteCorteRecupera);
    ejecutar("vigilante: la alerta local pospone el reinicio", vigilanteAlertaPospone);
    ejecutar("vigilante: tarea sin vueltas fuera de una peticion", vigilanteTareaSinVueltas);
    ejecutar("vigilante: vuelta de millis()", vigilanteVueltaMillis);
    ejecutar("vigilante: marca posterior a la revision (carrera con loop)", vigilanteMarcaPosterior);
    ejecutar("vigilante: metodo y ruta largos se recortan", vigilanteRutaLarga);

    ejecutar("offline: al reconectar, eventos y despues lecturas en orden", offlineOrdenAlReconectar);
    ejecutar("offline: lecturas con limite, se descartan las mas viejas", offlineLecturasLimite);
    ejecutar("offline: destinos Firebase y Telegram por separado", offlineEventosDestinos);
    ejecutar("offline: un BOTON nunca se pierde con la cola llena", offlineBotonNuncaSePierde);
    ejecutar("offline: eventos sobreviven a un reinicio (NVS)", offlineEventosSobrevivenReinicio);
    ejecutar("offline: cola de eventos vacia", offlineSerializaVacia);
    ejecutar("eventos: bateria baja con histeresis", bateriaHisteresis);
    ejecutar("eventos: episodio con esperas de inicio y fin", episodioEsperas);
    ejecutar("eventos: episodio con vuelta de millis()", episodioVueltaMillis);
    ejecutar("eventos: colgante ausente una vez por episodio", colganteAusenteUnaVez);
    ejecutar("eventos: bateria baja por colgante", colganteBateriaPorId);
    ejecutar("eventos: mas colgantes que lugares", colgantesLlenos);
    ejecutar("eventos: estado de los detectores tras un reinicio", detectoresSobrevivenReinicio);
    ejecutar("eventos: base desconectada y duraciones", baseDesconectada);
    ejecutar("telegram: textos de los avisos", telegramTextos);
    ejecutar("telegram: cuerpo de sendMessage y respuestas", telegramJson);
    ejecutar("telegram: intervalo minimo entre mensajes", telegramIntervalos);
    ejecutar("telegram: tope por hora, el BOTON no espera", telegramTopePorHora);
    ejecutar("telegram: espera tras error y retry_after", telegramEsperaTrasError);
    std::printf("\n%d casos, %d verificaciones, %d casos fallidos\n", casos, verificaciones,
                casos_fallidos);
    return casos_fallidos == 0 ? 0 : 1;
}
