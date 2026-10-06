// Firmware del colgante (ESP32-C3 Super Mini).
// Por ahora solo se identifica por serial. Los módulos están en src/ con su TODO.

#include <Arduino.h>
#include <protocolo.h>

#include "config.h"

void setup() {
    Serial.begin(115200);
    // Con USB CDC el puerto tarda en aparecer; se espera un poco sin bloquear si no hay PC.
    unsigned long inicio = millis();
    while (!Serial && millis() - inicio < 2000) {
        delay(10);
    }

    Serial.printf("%s v%s (protocolo v%d)\n", FIRMWARE_NOMBRE, FIRMWARE_VERSION, PROTOCOLO_VERSION);
}

void loop() {
    delay(1000);
}
