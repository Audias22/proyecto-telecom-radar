// cola_peticiones.h
// Peticiones que loop() deja para la tarea de red (firebase_cliente). Memoria fija y acotada
// (capacidades en config.h, COLA_*):
//   - eventos:   anillo propio. Nunca los desplaza una lectura. Si se llenara (32 eventos sin
//                enviar), el evento nuevo se rechaza y se cuenta; no se pisa uno pendiente.
//   - colgantes: una entrada por id; un mensaje nuevo reemplaza al pendiente del mismo colgante.
//   - lecturas:  anillo propio; llena, se descarta la lectura más antigua.
// Orden de salida: eventos, colgantes, lecturas (docs/fallos.md, escenario 4).
//
// Lógica pura y sin candados: firebase_cliente.cpp la protege con un mutex. Cada petición lleva
// un número; confirmar() solo saca la petición si sigue siendo la misma que se tomó, así una
// lectura nueva que pisó a la que se estaba enviando no se pierde por error.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "formato_nube.h"

struct PeticionEvento {
    TipoEvento tipo;
    char       origen[16];   // "base01", "colgante1", ...
    char       detalle[64];
    uint32_t   millis_creado;
};

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
    size_t   colgantes;
    size_t   lecturas;
    uint32_t lecturas_descartadas;  // pisadas por cola llena
    uint32_t eventos_rechazados;    // no cupieron (no debería pasar)
};

class ColaPeticiones {
public:
    void agregarLectura(const LecturaNube &lectura);
    bool agregarEvento(const PeticionEvento &evento);  // false si no cupo
    void agregarColgante(const PeticionColgante &colgante);

    // Siguiente petición sin sacarla. Las lecturas solo salen con con_hora (sin referencia de
    // hora del servidor no hay clave ts). false si no hay nada que enviar.
    bool siguiente(bool con_hora, Peticion &peticion) const;

    // Saca la petición tomada con siguiente() (enviada o descartada a propósito).
    void confirmar(const Peticion &peticion);

    TamanosCola tamanos() const;

private:
    template <typename T, size_t N>
    struct Anillo {
        T        datos[N];
        uint32_t numeros[N];
        size_t   inicio = 0;
        size_t   cantidad = 0;

        bool lleno() const { return cantidad == N; }
        void sacar() {
            inicio = (inicio + 1) % N;
            cantidad--;
        }
        void meter(const T &dato, uint32_t numero) {
            const size_t i = (inicio + cantidad) % N;
            datos[i] = dato;
            numeros[i] = numero;
            cantidad++;
        }
    };

    struct EntradaColgante {
        bool             ocupada;
        uint32_t         numero;
        PeticionColgante colgante;
    };

    int colganteMasAntiguo() const;
    int buscarColgante(uint8_t id) const;
    int buscarLibre() const;

    Anillo<PeticionEvento, COLA_EVENTOS_CAPACIDAD> eventos_;
    Anillo<LecturaNube, COLA_LECTURAS_CAPACIDAD>   lecturas_;
    EntradaColgante colgantes_[COLA_COLGANTES_CAPACIDAD] = {};
    uint32_t ultimo_numero_ = 0;
    uint32_t lecturas_descartadas_ = 0;
    uint32_t eventos_rechazados_ = 0;
};
