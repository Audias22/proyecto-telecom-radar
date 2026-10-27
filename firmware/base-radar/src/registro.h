// registro.h
// Una línea por serial con la hora y el módulo: "# [   52400 ms] telegram: ...". Empieza con '#'
// como todos los registros (analisis/capturar_serial.py la ignora). Se puede llamar desde
// cualquier tarea.

#pragma once

#include <stdarg.h>

void registrar(const char *modulo, const char *formato, ...)
    __attribute__((format(printf, 2, 3)));
void registrarLista(const char *modulo, const char *formato, va_list args);
