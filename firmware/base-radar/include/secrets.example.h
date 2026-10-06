// secrets.example.h
// Plantilla de credenciales de la base. NO poner datos reales aquí.
//
// Uso: copiar este archivo como include/secrets.h y llenar los valores.
// secrets.h está en .gitignore y no se sube al repositorio.

#pragma once

// WiFi de la casa (solo 2.4 GHz)
#define WIFI_SSID     "nombre_de_la_red"
#define WIFI_CLAVE    "clave_de_la_red"

// Firebase (consola de Firebase > Configuración del proyecto)
#define FIREBASE_URL      "https://tu-proyecto-default-rtdb.firebaseio.com"
#define FIREBASE_API_KEY  "api_key_web_del_proyecto"

// Cuenta de Firebase Authentication (correo/contraseña) que usa este dispositivo para escribir
#define DISPOSITIVO_EMAIL  "base01@tu-proyecto.example"
#define DISPOSITIVO_CLAVE  "clave_de_la_cuenta_del_dispositivo"

// Identificador de esta base en la base de datos (/dispositivos/{BASE_ID})
#define BASE_ID  "base01"

// Bot de Telegram (token de @BotFather y chat_id del cuidador o del grupo)
#define TELEGRAM_TOKEN    "123456789:token_del_bot"
#define TELEGRAM_CHAT_ID  "-1001234567890"
