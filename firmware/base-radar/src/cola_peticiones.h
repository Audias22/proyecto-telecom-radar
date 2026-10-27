// cola_peticiones.h
// Peticiones que loop() deja para la tarea de red (firebase_cliente y telegram). Memoria fija y
// acotada (capacidades en config.h, COLA_*):
//   - eventos:   ColaEventos (cola_offline.h). Para Firebase y Telegram; nunca los desplaza una
//                lectura y se guardan en NVS mientras estén pendientes.
//   - colgantes: una entrada por id; un mensaje nuevo reemplaza al pendiente del mismo colgante.
//   - lecturas:  ColaLecturas (cola_offline.h); llena, se descarta la lectura más antigua.
// Orden de salida a Firebase: eventos, colgantes, lecturas en orden de medición (docs/fallos.md,
// escenario 4). Telegram tiene su propio orden: primero los BOTON.
//
// Lógica pura y sin candados: firebase_cliente.cpp la protege con un mutex. Cada petición lleva
// un número; confirmar() solo saca la petición si sigue siendo la misma que se tomó, así una
// lectura nueva que pisó a la que se estaba enviando no se pierde por error.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cola_offline.h"
#include "config.h"
#include "formato_nube.h"

struct PeticionColgante {
    uint8_t  id;
    uint16_t bateria_mv;
    int8_t   rssi;
};

enum TipoPeticion : uint8_t {
    PETICION_NINGUNA,
    PETICION_EVENTO,
    PETICION_COLGANTE,
    PETICION_LECTURA,
};

struct Peticion {
    TipoPeticion     tipo;
    uint32_t         numero;
    PeticionEvento   evento;
    PeticionColgante colgante;
    LecturaNube      lectura;
};

struct TamanosCola {
    size_t   eventos;
    size_t   eventos_firebase;      // pendientes de Firebase
    size_t   eventos_telegram;      // pendientes de Telegram
    size_t   colgantes;
    size_t   lecturas;
    uint32_t lecturas_descartadas;  // pisadas por cola llena
    uint32_t eventos_rechazados;    // no cupieron (no debería pasar)
    uint32_t eventos_reemplazados;  // reemplazados por un BOTON con la cola llena
};

class ColaPeticiones {
public:
    void agregarLectura(const LecturaNube &lectura);
    ResultadoAgregar agregarEvento(const PeticionEvento &evento);
    void agregarColgante(const PeticionColgante &colgante);

    // Siguiente petición para Firebase, sin sacarla. Las lecturas solo salen con con_hora (sin
    // referencia de hora del servidor no hay clave ts). false si no hay nada que enviar.
    bool siguiente(bool con_hora, Peticion &peticion) const;

    // Saca la petición tomada con siguiente() (enviada o descartada a propósito).
    void confirmar(const Peticion &peticion);

    // Siguiente aviso para Telegram (BOTON primero) y su confirmación.
    bool siguienteTelegram(PeticionEvento &evento, uint32_t &numero) const;
    void confirmarTelegram(uint32_t numero);

    // Guardado en NVS de los eventos pendientes (ColaEventos).
    bool eventosCambiaron() const { return eventos_.cambio(); }
    size_t serializarEventos(uint8_t *buffer, size_t tamano);  // limpia el cambio
    int restaurarEventos(const uint8_t *datos, size_t largo, uint8_t destinos_activos);

    TamanosCola tamanos() const;

private:
    struct EntradaColgante {
        bool             ocupada;
        uint32_t         numero;
        PeticionColgante colgante;
    };

    int colganteMasAntiguo() const;
    int buscarColgante(uint8_t id) const;
    int buscarLibre() const;

    ColaEventos     eventos_;
    ColaLecturas    lecturas_;
    EntradaColgante colgantes_[COLA_COLGANTES_CAPACIDAD] = {};
    uint32_t ultimo_numero_ = 0;
};
