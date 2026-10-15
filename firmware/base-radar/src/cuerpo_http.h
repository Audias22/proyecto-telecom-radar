// cuerpo_http.h
// Cuerpo de una respuesta HTTP/1.1, armado por partes en un buffer fijo. Reemplaza a
// HTTPClient::getString(), que en arduino-esp32 3.3.7 no tiene límite de tiempo: con un 204 sin
// Content-Length (todas las escrituras con ?print=silent) espera a que el servidor cierre la
// conexión, ~75 s con keep-alive o para siempre si ese cierre se pierde (docs/fallos.md,
// escenario 5).
//
// Lógica pura, sin Arduino: firebase_cliente.cpp le pasa los bytes que llegan del socket y pone
// el límite de tiempo; las pruebas host (pruebas_host/) la usan directo.

#pragma once

#include <stddef.h>
#include <stdint.h>

enum ModoCuerpo : uint8_t {
    CUERPO_VACIO,         // 1xx, 204 y 304: no hay cuerpo aunque no venga Content-Length
    CUERPO_LARGO,         // Content-Length (Firebase Realtime Database)
    CUERPO_CHUNKED,       // Transfer-Encoding: chunked (Firebase Auth)
    CUERPO_HASTA_CIERRE,  // ni largo ni chunked: termina cuando el servidor cierra
};

namespace cuerpo_http {

// largo: Content-Length, o -1 si no vino.
ModoCuerpo modo(int codigo, int32_t largo, bool chunked);

}  // namespace cuerpo_http

class CuerpoHttp {
public:
    // destino: donde queda el cuerpo, siempre terminado en '\0'.
    CuerpoHttp(char *destino, size_t capacidad);

    void iniciar(ModoCuerpo modo, int32_t largo);

    // Procesa bytes recibidos. Devuelve cuántos consumió: con el cuerpo completo deja de consumir.
    size_t agregar(const uint8_t *datos, size_t cantidad);

    // El servidor cerró la conexión. Solo completa CUERPO_HASTA_CIERRE.
    void cerrada();

    bool completo() const { return estado_ == LISTO; }
    bool invalido() const { return estado_ == INVALIDO; }  // chunked mal formado
    // No cupo en el destino: se sigue consumiendo (la conexión queda limpia) pero se descarta.
    bool recortado() const { return recortado_; }
    size_t largo() const { return largo_; }

private:
    enum Estado : uint8_t {
        DATOS,           // CUERPO_LARGO y CUERPO_HASTA_CIERRE, o datos de un chunk
        TAMANO,          // dígitos hexadecimales del tamaño del chunk
        EXTENSION,       // ";..." después del tamaño, hasta el fin de línea
        FIN_CHUNK,       // "\r\n" después de los datos del chunk
        TRAILER,         // líneas después del chunk de tamaño 0, hasta una línea vacía
        LISTO,
        INVALIDO,
    };

    void guardar(uint8_t byte);
    void procesarChunked(uint8_t byte);
    void procesarTamano(uint8_t byte);

    char    *destino_;
    size_t   capacidad_;
    size_t   largo_ = 0;
    bool     recortado_ = false;
    ModoCuerpo modo_ = CUERPO_VACIO;
    Estado   estado_ = LISTO;
    uint32_t restante_ = 0;   // bytes que faltan del cuerpo (CUERPO_LARGO) o del chunk
    uint8_t  digitos_ = 0;    // dígitos del tamaño del chunk leídos
    bool     linea_vacia_ = true;
};
