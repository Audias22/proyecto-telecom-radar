// telegram.h
// Envío de alertas al cuidador por la API de bots de Telegram (HTTPS, sendMessage).

#pragma once

namespace telegram {

// TODO: POST a https://api.telegram.org/bot<TELEGRAM_TOKEN>/sendMessage con TELEGRAM_CHAT_ID.
// Devuelve false si falla, para que quien llama reintente.
bool enviarMensaje(const char *texto);

}  // namespace telegram
