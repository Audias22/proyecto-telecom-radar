#include "alerta_local.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include "config.h"

namespace alerta_local {

namespace {

struct Color {
    uint8_t rojo;
    uint8_t verde;
    uint8_t azul;
};

bool operator==(const Color &a, const Color &b) {
    return a.rojo == b.rojo && a.verde == b.verde && a.azul == b.azul;
}

constexpr Color APAGADO = {0, 0, 0};
constexpr Color VERDE_REPOSO = {0, LED_BRILLO_REPOSO, 0};
constexpr Color ROJO = {LED_BRILLO_ALERTA, 0, 0};
// Por confirmar con el módulo: el tono exacto del amarillo en este WS2812.
constexpr Color AMARILLO = {LED_BRILLO_ALERTA, LED_BRILLO_ALERTA / 2, 0};

PatronAlerta patron = PATRON_NINGUNO;
uint32_t inicio_patron_ms = 0;
uint32_t inicio_prueba_ms = 0;
uint32_t duracion_prueba_ms = 0;

Color color_escrito = {1, 1, 1};  // distinto de todos, para forzar la primera escritura
bool buzzer_escrito = true;

bool esUrgente(PatronAlerta p) {
    return p == PATRON_BOTON || p == PATRON_APNEA;
}

uint32_t duracion(PatronAlerta p) {
    return esUrgente(p) ? ALERTA_LOCAL_DURACION_MS : AVISO_LOCAL_DURACION_MS;
}

uint32_t medioPeriodo(PatronAlerta p) {
    return esUrgente(p) ? ALERTA_PARPADEO_MS : AVISO_PARPADEO_MS;
}

// true en la mitad encendida del parpadeo.
bool faseEncendida(uint32_t transcurrido, uint32_t medio_periodo) {
    return (transcurrido / medio_periodo) % 2 == 0;
}

// rgbLedWrite() manda los 24 bits por RMT (~30 us). Solo se escribe cuando cambia el color.
void escribirLed(const Color &c) {
    if (c == color_escrito) {
        return;
    }
    rgbLedWrite(PIN_LED, c.rojo, c.verde, c.azul);
    color_escrito = c;
}

void escribirBuzzer(bool sonando) {
    const bool encender = sonando && BASE_BUZZER_HABILITADO;
    if (encender == buzzer_escrito) {
        return;
    }
    digitalWrite(PIN_BUZZER, encender ? HIGH : LOW);
    buzzer_escrito = encender;
}

bool pruebaActiva(uint32_t ahora) {
    return duracion_prueba_ms > 0 && ahora - inicio_prueba_ms < duracion_prueba_ms;
}

void terminarPatronVencido(uint32_t ahora) {
    if (patron != PATRON_NINGUNO && ahora - inicio_patron_ms >= duracion(patron)) {
        patron = PATRON_NINGUNO;
    }
}

}  // namespace

void iniciar() {
    pinMode(PIN_BUZZER, OUTPUT);
    escribirBuzzer(false);
    // Fuerza de salida máxima: el buzzer no tiene transistor (ver BUZZER_FUERZA_GPIO en config.h).
    gpio_set_drive_capability(static_cast<gpio_num_t>(PIN_BUZZER), BUZZER_FUERZA_GPIO);
    escribirLed(VERDE_REPOSO);
}

void activar(PatronAlerta nuevo) {
    if (nuevo == PATRON_NINGUNO) {
        silenciar();
        return;
    }
    if (esUrgente(patron) && !esUrgente(nuevo)) {
        return;  // un aviso no interrumpe una alerta
    }
    patron = nuevo;
    inicio_patron_ms = millis();
}

void silenciar() {
    patron = PATRON_NINGUNO;
    duracion_prueba_ms = 0;
}

void probarBuzzer(uint32_t duracion_ms) {
    inicio_prueba_ms = millis();
    duracion_prueba_ms = duracion_ms;
}

PatronAlerta patronActivo() {
    return patron;
}

void actualizar() {
    const uint32_t ahora = millis();
    terminarPatronVencido(ahora);

    const uint32_t transcurrido = ahora - inicio_patron_ms;
    const bool encendido = faseEncendida(transcurrido, medioPeriodo(patron));
    Color color = VERDE_REPOSO;
    bool sonando = false;
    if (esUrgente(patron)) {
        color = encendido ? ROJO : APAGADO;
        sonando = encendido;
    } else if (patron == PATRON_AVISO) {
        color = encendido ? AMARILLO : APAGADO;
    }

    escribirLed(color);
    escribirBuzzer(sonando || pruebaActiva(ahora));
}

}  // namespace alerta_local
