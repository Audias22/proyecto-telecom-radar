// secrets.example.h
// Plantilla de credenciales de la base. NO poner datos reales aquí.
//
// Uso: copiar este archivo como include/secrets.h y llenar los valores. Mientras FIREBASE_URL,
// FIREBASE_API_KEY o DISPOSITIVO_CLAVE tengan los valores de ejemplo, la base no intenta
// conectarse a Firebase (lo dice por serial y en el comando "e").
// secrets.h está en .gitignore y no se sube al repositorio.

#pragma once

// WiFi de la casa (solo 2.4 GHz)
#define WIFI_SSID     "nombre_de_la_red"
#define WIFI_CLAVE    "clave_de_la_red"

// Firebase (consola de Firebase > Configuración del proyecto > General).
// FIREBASE_URL: la URL de Realtime Database tal como sale en la consola (pestaña Realtime
// Database, arriba de los datos), sin barra final. Puede terminar en firebaseio.com o en
// firebasedatabase.app según la región.
// FIREBASE_API_KEY: "Clave de API web" de la app web del proyecto. No es secreta como una
// contraseña, pero no hace falta publicarla.
#define FIREBASE_URL      "https://tu-proyecto-default-rtdb.firebaseio.com"
#define FIREBASE_API_KEY  "api_key_web_del_proyecto"

// Cuenta de Firebase Authentication (correo/contraseña) de ESTA base. Se crea en
// Authentication > Usuarios. Su uid tiene que estar en /cuentas_dispositivo con el valor BASE_ID
// (docs/modelo-datos.md); si no, las reglas rechazan todo con 401 "Permission denied". La base
// imprime el uid por serial al iniciar sesión.
#define DISPOSITIVO_EMAIL  "base01@tu-proyecto.example"
#define DISPOSITIVO_CLAVE  "clave_de_la_cuenta_del_dispositivo"

// Identificador de esta base: /dispositivos/{BASE_ID}, /lecturas/{BASE_ID} y "base" en
// /colgantes. Debe ser igual al valor de su uid en /cuentas_dispositivo.
#define BASE_ID  "base01"

// Bot de Telegram (token de @BotFather y chat_id del cuidador o del grupo)
#define TELEGRAM_TOKEN    "123456789:token_del_bot"
#define TELEGRAM_CHAT_ID  "-1001234567890"
