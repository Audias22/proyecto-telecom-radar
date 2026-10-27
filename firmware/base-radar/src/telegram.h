// telegram.h
// Avisos al cuidador por la API de bots de Telegram: POST https://api.telegram.org/bot<TOKEN>/
// sendMessage con TELEGRAM_CHAT_ID (secrets.h). README de la base: cómo crear el bot.
//
// Corre en la tarea de red (firebase_cliente.cpp), que en cada vuelta atiende primero Telegram y
// después Firebase. Tiene su propia conexión TLS (keep-alive, TELEGRAM_REUSO_MAX_MS), su propia
// espera tras error y su límite de frecuencia (LimiteTelegram, config.h TELEGRAM_*): si Firebase
// falla o está desactivado, Telegram sale igual. Usa peticion_https.h, así que tiene los mismos
// límites de tiempo, el mismo vigilante y la misma verificación del certificado (bundle de CA).
//
// Con TELEGRAM_TOKEN o TELEGRAM_CHAT_ID con los valores de ejemplo de secrets.example.h (o sin
// definir), queda desactivado y lo avisa por serial y en el comando "e".

#pragma once

namespace telegram {

enum ResultadoTelegram {
    TELEGRAM_OK,
    TELEGRAM_REINTENTAR,  // red, 5xx o 429: el aviso queda en la cola y se reintenta
    TELEGRAM_RECHAZADO,   // 400/401/403/404: token, chat o mensaje inválidos; reintentar no sirve
};

// En setup(). Revisa secrets.h; no abre ninguna conexión.
void iniciar();
bool activo();

// Desde la tarea de red. listo(): el límite de frecuencia y la espera tras error permiten enviar
// ahora. enviar() bloquea la tarea de red lo que tarde la petición (acotada por peticion_https).
bool listo(bool urgente);
ResultadoTelegram enviar(const char *texto, bool urgente);

// Desde la tarea de red: cierra la conexión sin uso por TELEGRAM_REUSO_MAX_MS, o siempre (sin
// WiFi). Libera el heap del TLS.
void cerrarSiInactiva();
void cerrarConexion();

// Comando "e".
void imprimirEstado();

}  // namespace telegram
