// cola_offline.h
// Lo que espera a la red (docs/fallos.md, escenario 4). Lógica pura, sin Arduino ni candados: la
// usa ColaPeticiones (cola_peticiones.h), que firebase_cliente.cpp protege con su mutex.
//
//   - ColaLecturas: lecturas sin enviar, en RAM y en orden. Llena (COLA_OFFLINE_LECTURAS, 1 h a
//     una cada 5 s), se descarta la más antigua. Se pierden con un reinicio: un millis() de antes
//     del reinicio no sirve con la referencia de hora de después (docs/modelo-datos.md, 2).
//   - ColaEventos: eventos para Firebase y avisos para Telegram. Cada uno queda hasta salir por
//     todos sus destinos, y salen en orden distinto: a Firebase el más antiguo; a Telegram
//     primero los BOTON. Nunca se descarta uno para hacer lugar, salvo que llegue un BOTON con la
//     cola llena: reemplaza al evento más antiguo que no sea BOTON. Se guarda en NVS mientras
//     haya pendientes (serializar() y restaurar()), así sobrevive a un reinicio.
//
// Cada entrada lleva un número creciente que asigna ColaPeticiones; confirmar() solo saca la
// entrada si sigue siendo la misma que se tomó.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "formato_nube.h"

enum DestinoEvento : uint8_t {
    DESTINO_FIREBASE = 1u << 0,
    DESTINO_TELEGRAM = 1u << 1,
};

struct PeticionEvento {
    TipoEvento tipo;
    uint8_t    destinos;         // DESTINO_* por los que todavía falta enviarlo
    bool       previo_reinicio;  // restaurado de NVS: millis_creado es de antes del reinicio
    char       origen[16];       // "base01", "colgante1", ...
    char       detalle[64];
    uint32_t   millis_creado;
};

class ColaLecturas {
public:
    void agregar(const LecturaNube &lectura, uint32_t numero);
    bool primera(LecturaNube &lectura, uint32_t &numero) const;
    void confirmar(uint32_t numero);

    size_t cantidad() const { return cantidad_; }
    uint32_t descartadas() const { return descartadas_; }

private:
    LecturaNube datos_[COLA_OFFLINE_LECTURAS];
    uint32_t    numeros_[COLA_OFFLINE_LECTURAS];
    size_t      inicio_ = 0;
    size_t      cantidad_ = 0;
    uint32_t    descartadas_ = 0;
};

enum ResultadoAgregar : uint8_t {
    EVENTO_AGREGADO,
    EVENTO_AGREGADO_REEMPLAZANDO,  // un BOTON con la cola llena reemplazó a otro evento
    EVENTO_RECHAZADO,              // llena de BOTON, o sin destinos
};

class ColaEventos {
public:
    // Tamaño máximo de serializar(): cabecera y todas las entradas.
    static constexpr size_t TAMANO_SERIALIZADO = 4 + COLA_EVENTOS_CAPACIDAD * sizeof(PeticionEvento);

    ResultadoAgregar agregar(const PeticionEvento &evento, uint32_t numero);

    // Firebase: el más antiguo con ese destino pendiente. Telegram: el BOTON más antiguo y, si no
    // hay, el más antiguo.
    bool siguiente(DestinoEvento destino, PeticionEvento &evento, uint32_t &numero) const;

    // Quita el destino de la entrada (enviado, o rechazado sin remedio). Sin destinos, la libera.
    void confirmar(DestinoEvento destino, uint32_t numero);

    size_t cantidad() const;
    size_t pendientes(DestinoEvento destino) const;
    uint32_t rechazados() const { return rechazados_; }
    uint32_t reemplazados() const { return reemplazados_; }

    // Algo cambió desde el último limpiarCambio(): hay que volver a guardar en NVS.
    bool cambio() const { return cambio_; }
    void limpiarCambio() { cambio_ = false; }

    // Entradas en orden de llegada. Devuelve los bytes escritos (0 si no cupo).
    size_t serializar(uint8_t *buffer, size_t tamano) const;

    // Agrega las entradas de un serializar() anterior, marcadas previo_reinicio y solo con los
    // destinos de destinos_activos; ultimo_numero da los números nuevos. Devuelve cuántas
    // agregó, o -1 si los datos no son de este formato.
    int restaurar(const uint8_t *datos, size_t largo, uint8_t destinos_activos,
                  uint32_t &ultimo_numero);

private:
    struct Entrada {
        bool           ocupada;
        uint32_t       numero;
        PeticionEvento evento;
    };

    int buscar(uint32_t numero) const;
    int libre() const;
    int masAntiguo(DestinoEvento destino, bool solo_boton) const;
    int masAntiguoNoBoton() const;
    int siguienteEnOrden(bool hay_anterior, uint32_t anterior) const;

    Entrada  entradas_[COLA_EVENTOS_CAPACIDAD] = {};
    uint32_t rechazados_ = 0;
    uint32_t reemplazados_ = 0;
    bool     cambio_ = false;
};
