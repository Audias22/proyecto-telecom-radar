// secrets.example.h
// Plantilla de configuración privada del colgante. NO poner datos reales aquí.
//
// Uso: copiar este archivo como include/secrets.h y llenar los valores.
// secrets.h está en .gitignore y no se sube al repositorio.
//
// El colgante no se conecta a WiFi, Firebase ni Telegram: solo habla con la base por ESP-NOW.
// Por eso aquí no van esas credenciales; si se perdiera el colgante, no expone nada de la nube.

#pragma once

// MAC de la base en modo STA. Se obtiene del monitor serial de la base al arrancar.
#define BASE_MAC  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }

// Id de este colgante (1-255). Debe ser único si hay más de un colgante.
#define COLGANTE_ID  1
