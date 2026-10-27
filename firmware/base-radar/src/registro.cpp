#include "registro.h"

#include <Arduino.h>

#include <cstdio>

void registrarLista(const char *modulo, const char *formato, va_list args) {
    char texto[224];
    vsnprintf(texto, sizeof(texto), formato, args);
    Serial.printf("# [%9lu ms] %s: %s\n", millis(), modulo, texto);
}

void registrar(const char *modulo, const char *formato, ...) {
    va_list args;
    va_start(args, formato);
    registrarLista(modulo, formato, args);
    va_end(args);
}
