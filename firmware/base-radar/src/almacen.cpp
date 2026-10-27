#include "almacen.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs.h>

#include <cstring>

#include "registro.h"

namespace almacen {

namespace {

constexpr const char *ESPACIO = "base";
constexpr size_t DIFERIDO_MAX = 64;

struct Diferido {
    bool    pendiente;
    char    clave[16];
    uint8_t datos[DIFERIDO_MAX];
    size_t  largo;
};

SemaphoreHandle_t candado = nullptr;
nvs_handle_t manejador = 0;
Diferido diferido = {};

class Bloqueo {
public:
    Bloqueo() { xSemaphoreTake(candado, portMAX_DELAY); }
    ~Bloqueo() { xSemaphoreGive(candado); }
};

bool escribirSinCandado(const char *clave, const void *datos, size_t largo) {
    const esp_err_t error = largo == 0 ? nvs_erase_key(manejador, clave)
                                       : nvs_set_blob(manejador, clave, datos, largo);
    if ((error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) || nvs_commit(manejador) != ESP_OK) {
        registrar("nvs", "ERROR al guardar \"%s\" (%u B): %s", clave, static_cast<unsigned>(largo),
                  esp_err_to_name(error));
        return false;
    }
    return true;
}

}  // namespace

// initArduino() ya hizo nvs_flash_init() (y borró la partición si estaba dañada).
bool iniciar() {
    candado = xSemaphoreCreateMutex();
    if (candado == nullptr || nvs_open(ESPACIO, NVS_READWRITE, &manejador) != ESP_OK) {
        registrar("nvs", "ERROR: NVS no disponible; los eventos pendientes no sobreviven a un "
                         "reinicio");
        candado = nullptr;
        return false;
    }
    return true;
}

size_t leer(const char *clave, void *datos, size_t tamano) {
    if (candado == nullptr) {
        return 0;
    }
    Bloqueo b;
    size_t largo = tamano;
    return nvs_get_blob(manejador, clave, datos, &largo) == ESP_OK ? largo : 0;
}

bool escribir(const char *clave, const void *datos, size_t largo) {
    if (candado == nullptr) {
        return false;
    }
    Bloqueo b;
    return escribirSinCandado(clave, datos, largo);
}

void pedirEscritura(const char *clave, const void *datos, size_t largo) {
    if (candado == nullptr || largo > DIFERIDO_MAX) {
        return;
    }
    Bloqueo b;
    strlcpy(diferido.clave, clave, sizeof(diferido.clave));
    memcpy(diferido.datos, datos, largo);
    diferido.largo = largo;
    diferido.pendiente = true;
}

void escribirPendientes() {
    if (candado == nullptr) {
        return;
    }
    Bloqueo b;
    if (diferido.pendiente) {
        diferido.pendiente = !escribirSinCandado(diferido.clave, diferido.datos, diferido.largo);
    }
}

}  // namespace almacen
