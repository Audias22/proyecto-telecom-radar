// detectores.h
// Cuándo crear los eventos de la base (docs/fallos.md, "Tipos de evento"), una sola vez por
// episodio: ninguno se repite mientras dure la condición. Tiempos y umbrales en config.h.
//
//   - DetectorBateria:   BATERIA_BAJA, una vez por descarga, con histéresis.
//   - RegistroColgantes: colgantes conocidos, su batería y COLGANTE_AUSENTE.
//   - Episodio:          una condición que debe durar para empezar y para terminar
//                        (RADAR_SIN_DATOS).
//   - duracionDesconexion() y textoDuracion(): BASE_DESCONECTADA.
// El estado de los colgantes y del radar se guarda en NVS (serializarEstado()), así un reinicio
// de la base en medio de un episodio no lo avisa de nuevo.
//
// Lógica pura, sin Arduino: la usa main.cpp y la prueban las pruebas host.

#pragma once

#include <protocolo.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

class DetectorBateria {
public:
    // Un mensaje nuevo del colgante. true si debe crear el evento BATERIA_BAJA.
    bool revisar(bool bandera_baja, uint16_t bateria_mv);

    bool avisada() const { return avisada_; }
    void restaurar(bool avisada) { avisada_ = avisada; }

private:
    bool avisada_ = false;
};

enum CambioEpisodio : uint8_t {
    EPISODIO_SIN_CAMBIO,
    EPISODIO_EMPIEZA,
    EPISODIO_TERMINA,
};

class Episodio {
public:
    // espera_inicio_ms: la condición debe durar esto para empezar; espera_fin_ms: debe faltar
    // esto para terminar.
    Episodio(uint32_t espera_inicio_ms, uint32_t espera_fin_ms)
        : espera_inicio_(espera_inicio_ms), espera_fin_(espera_fin_ms) {}

    CambioEpisodio actualizar(bool condicion, uint32_t ahora);

    bool activo() const { return activo_; }
    void restaurar(bool activo) { activo_ = activo; contando_ = false; }

private:
    uint32_t espera_inicio_;
    uint32_t espera_fin_;
    bool     activo_ = false;
    bool     contando_ = false;  // la condición cambió y se espera que dure
    uint32_t desde_ms_ = 0;
};

struct ResultadoMensaje {
    bool bateria_baja;  // crear BATERIA_BAJA
    bool volvio;        // estaba ausente y volvió a comunicarse (termina el episodio)
};

class RegistroColgantes {
public:
    static constexpr size_t MAXIMO = COLA_COLGANTES_CAPACIDAD;
    static constexpr size_t TAMANO_SERIALIZADO = 2 + MAXIMO * 2;

    // Cualquier mensaje del colgante, nuevo o duplicado: los dos prueban que está vivo. Con la
    // lista llena, un id nuevo reemplaza al que lleva más tiempo sin comunicarse.
    ResultadoMensaje mensaje(uint8_t id, bool bandera_baja, uint16_t bateria_mv, uint32_t ahora);

    // Ids que acaban de pasar COLGANTE_AUSENTE_MS sin mensajes (una vez por episodio). Devuelve
    // cuántos escribió en ids.
    size_t revisarAusentes(uint32_t ahora, uint8_t *ids, size_t maximo);

    size_t cantidad() const;
    bool ausente(uint8_t id) const;

    bool cambio() const { return cambio_; }
    void limpiarCambio() { cambio_ = false; }

    // Ids conocidos con su batería avisada y su ausencia. Sin tiempos: al restaurar, cada
    // colgante cuenta su ausencia desde `ahora` (el arranque).
    size_t serializar(uint8_t *buffer, size_t tamano) const;
    bool restaurar(const uint8_t *datos, size_t largo, uint32_t ahora);

private:
    struct Colgante {
        bool            conocido;
        uint8_t         id;
        uint32_t        ultimo_ms;
        bool            ausente;
        DetectorBateria bateria;
    };

    int buscar(uint8_t id) const;
    int lugarPara(uint32_t ahora) const;

    Colgante colgantes_[MAXIMO] = {};
    bool     cambio_ = false;
};

// Lo que se guarda en NVS de los detectores: radar en episodio y colgantes.
size_t serializarEstado(const RegistroColgantes &colgantes, bool radar_sin_datos, uint8_t *buffer,
                        size_t tamano);
bool restaurarEstado(const uint8_t *datos, size_t largo, uint32_t ahora,
                     RegistroColgantes &colgantes, bool &radar_sin_datos);

// Tiempo sin contacto entre el último ultimo_contacto conocido y el nuevo (hora del servidor, ms).
// 0 si no hay anterior o si no llega a BASE_DESCONECTADA_MIN_MS.
uint64_t duracionDesconexion(bool hay_anterior, uint64_t anterior_ms, uint64_t nuevo_ms);

// "45 s", "12 min", "3 h 5 min", "2 d 4 h". Devuelve la longitud escrita.
size_t textoDuracion(uint64_t ms, char *texto, size_t tamano);
