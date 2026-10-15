// test_logica.cpp
// Pruebas unitarias en host (PC) de la lógica pura de la base: ventana de actividad del radar
// (persona / objetivo), JSON que se escribe en Firebase y cola de peticiones a la tarea de red.
//
// Incluye los headers y .cpp reales (sin Arduino); no duplica su lógica. Los JSON esperados son
// los mismos que escribe web/simulador/simulador.py, que pasó la verificación contra las reglas
// publicadas. Ver pruebas_host/README.md para compilar y ejecutar.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "cola_peticiones.h"
#include "formato_nube.h"
#include "ventana_actividad.h"

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
// Cola de peticiones
// ---------------------------------------------------------------------------

PeticionEvento eventoBoton(int n) {
    PeticionEvento e{};
    e.tipo = EVENTO_BOTON;
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
    for (uint32_t i = 0; i <= COLA_LECTURAS_CAPACIDAD; i++) {
        cola.agregarLectura(lecturaEn(i));
    }
    const TamanosCola t = cola.tamanos();
    VERIFICAR(t.lecturas == COLA_LECTURAS_CAPACIDAD);
    VERIFICAR(t.lecturas_descartadas == 1);
    Peticion p;
    VERIFICAR(cola.siguiente(true, p) && p.lectura.millis_medicion == 1);  // la 0 se descartó
}

void colaEventosNuncaDesplazados() {
    ColaPeticiones cola;
    for (int i = 0; i < COLA_EVENTOS_CAPACIDAD; i++) {
        VERIFICAR(cola.agregarEvento(eventoBoton(i)));
    }
    for (uint32_t i = 0; i < 3 * COLA_LECTURAS_CAPACIDAD; i++) {
        cola.agregarLectura(lecturaEn(i));
    }
    VERIFICAR(cola.tamanos().eventos == COLA_EVENTOS_CAPACIDAD);  // las lecturas no los tocan
    VERIFICAR(!cola.agregarEvento(eventoBoton(99)));  // sin lugar: se rechaza el nuevo
    VERIFICAR(cola.tamanos().eventos_rechazados == 1);
    Peticion p;
    VERIFICAR(cola.siguiente(true, p) && std::strcmp(p.evento.origen, "colgante0") == 0);
}

void colaConfirmarTrasPisada() {
    // La tarea de red toma la lectura más antigua; mientras la envía, loop() agrega otra con la
    // cola llena y la pisa. Confirmar la vieja no debe sacar la nueva primera.
    ColaPeticiones cola;
    for (uint32_t i = 0; i < COLA_LECTURAS_CAPACIDAD; i++) {
        cola.agregarLectura(lecturaEn(i));
    }
    Peticion enviada;
    cola.siguiente(true, enviada);
    cola.agregarLectura(lecturaEn(100));
    cola.confirmar(enviada);
    VERIFICAR(cola.tamanos().lecturas == COLA_LECTURAS_CAPACIDAD);
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

    std::printf("\n%d casos, %d verificaciones, %d casos fallidos\n", casos, verificaciones,
                casos_fallidos);
    return casos_fallidos == 0 ? 0 : 1;
}
