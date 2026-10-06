// Firmware de la base (XIAO ESP32-C6 del kit MR60BHA2).
//
// Por ahora lee el radar e imprime una línea CSV por lectura. El formato es fijo porque lo usa
// analisis/capturar_serial.py:
//   - Líneas que empiezan con '#': comentarios (arranque, avisos). No son datos.
//   - Encabezado: ms,valida,presencia,distancia_cm,resp_rpm,latido_bpm,fase_total,fase_resp,fase_latido
//   - Datos: valida y presencia como 0/1; valores no recibidos como "nan".

#include <Arduino.h>
#include <protocolo.h>

#include "config.h"
#include "radar.h"

static const char *ENCABEZADO_CSV =
    "ms,valida,presencia,distancia_cm,resp_rpm,latido_bpm,fase_total,fase_resp,fase_latido";

static void imprimirLectura(const LecturaRadar &l) {
    Serial.printf("%lu,%d,%d,%.1f,%.2f,%.2f,%.6f,%.6f,%.6f\n",
                  (unsigned long)l.millis_medicion, l.valida ? 1 : 0, l.presencia ? 1 : 0,
                  l.distancia_cm, l.resp_rpm, l.latido_bpm,
                  l.fase_total, l.fase_resp, l.fase_latido);
}

void setup() {
    Serial.begin(115200);
    // Con USB CDC el puerto tarda en aparecer; se espera un poco sin bloquear si no hay PC.
    unsigned long inicio = millis();
    while (!Serial && millis() - inicio < 2000) {
        delay(10);
    }

    Serial.printf("# %s v%s (protocolo v%d)\n", FIRMWARE_NOMBRE, FIRMWARE_VERSION, PROTOCOLO_VERSION);
    Serial.println(ENCABEZADO_CSV);

    radar::iniciar();
}

void loop() {
    radar::actualizar();

    LecturaRadar lectura;
    while (radar::siguienteLectura(lectura)) {
        imprimirLectura(lectura);
    }

    static bool sin_datos_antes = false;
    bool sin_datos = radar::sinDatos();
    if (sin_datos != sin_datos_antes) {
        Serial.printf("# radar %s (reinicios de UART: %lu)\n", sin_datos ? "sin datos" : "con datos",
                      (unsigned long)radar::reiniciosUart());
        sin_datos_antes = sin_datos;
    }
}
