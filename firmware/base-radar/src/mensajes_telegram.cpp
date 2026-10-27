#include "mensajes_telegram.h"

#include <stdio.h>
#include <string.h>

#include "detectores.h"

namespace mensajes_telegram {

namespace {

constexpr uint32_t ATRASO_AVISADO_MS = 60000;
constexpr const char *PREFIJO_COLGANTE = "colgante";

// "colgante 1" para "colgante1"; "base base01" para el resto. El artículo va en cada frase.
void nombreOrigen(const char *origen, char *nombre, size_t tamano) {
    const size_t largo = strlen(PREFIJO_COLGANTE);
    if (strncmp(origen, PREFIJO_COLGANTE, largo) == 0 && origen[largo] != '\0') {
        snprintf(nombre, tamano, "colgante %s", origen + largo);
    } else {
        snprintf(nombre, tamano, "base %s", origen);
    }
}

int frase(const PeticionEvento &e, const char *nombre, char *texto, size_t tamano) {
    switch (e.tipo) {
        case EVENTO_BOTON:
            return snprintf(texto, tamano, "ALERTA: botón de auxilio del %s. Atender de inmediato.",
                            nombre);
        case EVENTO_BATERIA_BAJA:
            return snprintf(texto, tamano, "Batería baja en el %s (%s). Hay que cargarlo.", nombre,
                            e.detalle);
        case EVENTO_COLGANTE_AUSENTE:
            return snprintf(texto, tamano,
                            "El %s no se comunica (%s). Revisar la batería y el alcance.", nombre,
                            e.detalle);
        case EVENTO_RADAR_SIN_DATOS:
            return snprintf(texto, tamano,
                            "El radar de la %s no envía datos (%s). Revisar el cable y la "
                            "alimentación.",
                            nombre, e.detalle);
        case EVENTO_BASE_DESCONECTADA:
            return snprintf(texto, tamano, "La %s volvió a estar en línea (%s).", nombre, e.detalle);
        case EVENTO_REINICIO:
            return snprintf(texto, tamano, "La %s se reinició de forma inesperada: %s.", nombre,
                            e.detalle);
        default:
            return snprintf(texto, tamano, "Aviso %s (%s).", formato_nube::nombreEvento(e.tipo),
                            nombre);
    }
}

void agregarAtraso(const PeticionEvento &e, uint32_t ahora, char *texto, size_t tamano) {
    const size_t largo = strlen(texto);
    if (e.previo_reinicio) {
        snprintf(texto + largo, tamano - largo, " (aviso de antes de un reinicio de la base)");
        return;
    }
    const uint32_t atraso = ahora - e.millis_creado;
    if (atraso >= ATRASO_AVISADO_MS) {
        char duracion[24];
        textoDuracion(atraso, duracion, sizeof(duracion));
        snprintf(texto + largo, tamano - largo, " (ocurrió hace %s)", duracion);
    }
}

}  // namespace

size_t texto(const PeticionEvento &evento, uint32_t ahora, char *texto, size_t tamano) {
    if (tamano == 0) {
        return 0;
    }
    char nombre[32];
    nombreOrigen(evento.origen, nombre, sizeof(nombre));
    if (frase(evento, nombre, texto, tamano) < 0) {
        texto[0] = '\0';
        return 0;
    }
    agregarAtraso(evento, ahora, texto, tamano);
    return strlen(texto);
}

bool esUrgente(TipoEvento tipo) {
    return tipo == EVENTO_BOTON;
}

}  // namespace mensajes_telegram

// ---------------------------------------------------------------------------
// Límite de frecuencia
// ---------------------------------------------------------------------------

namespace {

constexpr uint32_t UNA_HORA_MS = 3600000;

uint32_t faltan(uint32_t ahora, uint32_t desde, uint32_t duracion) {
    const uint32_t pasado = ahora - desde;
    return pasado < duracion ? duracion - pasado : 0;
}

}  // namespace

bool LimiteTelegram::puedeEnviar(uint32_t ahora, bool urgente) const {
    return espera(ahora, urgente) == 0;
}

uint32_t LimiteTelegram::espera(uint32_t ahora, bool urgente) const {
    const uint32_t por_error =
        espera_error_ms_ != 0 ? faltan(ahora, marca_error_ms_, espera_error_ms_) : 0;
    const uint32_t por_envios = esperaEnvios(ahora, urgente);
    return por_error > por_envios ? por_error : por_envios;
}

uint32_t LimiteTelegram::esperaEnvios(uint32_t ahora, bool urgente) const {
    uint32_t resultado = 0;
    if (hubo_envio_) {
        resultado = faltan(ahora, ultimo_envio_ms_,
                           urgente ? TELEGRAM_INTERVALO_URGENTE_MS : TELEGRAM_INTERVALO_MIN_MS);
    }
    if (!urgente && cantidad_ == TELEGRAM_MAX_POR_HORA) {
        const uint32_t por_hora = faltan(ahora, envios_[inicio_], UNA_HORA_MS);
        resultado = por_hora > resultado ? por_hora : resultado;
    }
    return resultado;
}

void LimiteTelegram::enviado(uint32_t ahora, bool urgente) {
    hubo_envio_ = true;
    ultimo_envio_ms_ = ahora;
    espera_error_ms_ = 0;
    if (urgente) {
        return;
    }
    if (cantidad_ == TELEGRAM_MAX_POR_HORA) {
        inicio_ = (inicio_ + 1) % TELEGRAM_MAX_POR_HORA;
        cantidad_--;
    }
    envios_[(inicio_ + cantidad_) % TELEGRAM_MAX_POR_HORA] = ahora;
    cantidad_++;
}

void LimiteTelegram::fallo(uint32_t ahora, uint32_t pedido_por_servidor_ms) {
    espera_error_ms_ = espera_error_ms_ == 0 ? TELEGRAM_ESPERA_ERROR_MS : espera_error_ms_ * 2;
    if (espera_error_ms_ > TELEGRAM_ESPERA_ERROR_MAXIMA_MS) {
        espera_error_ms_ = TELEGRAM_ESPERA_ERROR_MAXIMA_MS;
    }
    if (pedido_por_servidor_ms > espera_error_ms_) {
        espera_error_ms_ = pedido_por_servidor_ms;
    }
    marca_error_ms_ = ahora;
}
