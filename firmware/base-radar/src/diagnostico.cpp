// Firmware de diagnóstico del radar (entorno "diagnostico" de platformio.ini).
// Se compila solo con -DDIAGNOSTICO; el firmware normal usa main.cpp.
//
// Fases:
//   1. CRUDO (60 s con la orden 'd', 150 s con 'D'): lee la UART sin la librería, valida checksums, cuenta tramas por
//      tipo y vuelca en hex algunas tramas de cada tipo y las de fase con fase_total en 0 o con
//      |fase_total| > 2, junto con su decodificación little-endian.
//   2. MODULO (FASE_MODULO_MS): usa radar.cpp (la misma lectura del firmware normal) y cuenta
//      lecturas por segundo, para comparar con el conteo crudo.
//   3. VERSION: envía una consulta de versión (tipo 0xFFFF, sin datos y luego con 01 01 00 00) y
//      escucha la respuesta. Es solo lectura; no cambia la configuración del radar.
// Todo se imprime como comentarios '#'.

#ifdef DIAGNOSTICO

#include <Arduino.h>
#include <HardwareSerial.h>
#include <math.h>
#include <string.h>

#include "config.h"
#include "radar.h"

static const uint32_t FASE_CRUDO_CORTA_MS = 60000;   // orden 'd'
static const uint32_t FASE_CRUDO_LARGA_MS = 150000;  // orden 'D' (prueba con persona)
static const uint32_t FASE_MODULO_MS = 30000;
static const uint32_t ESPERA_VERSION_MS = 3000;
static const int MAX_TIPOS = 16;
static const int VOLCADOS_POR_TIPO = 3;
static const int VOLCADOS_ESPECIALES = 30;

static HardwareSerial uart(RADAR_UART_NUM);

struct ConteoTipo {
    uint16_t tipo;
    uint32_t total;
    uint32_t en_segundo;
    int volcados;
};

static ConteoTipo tipos[MAX_TIPOS];
static int num_tipos = 0;
static uint32_t errores_cabecera = 0;
static uint32_t errores_datos = 0;
static uint32_t bytes_descartados = 0;
static size_t max_disponible = 0;
static int volcados_especiales = 0;

static uint32_t ultima_fase_ms = 0;
static uint32_t fases_total = 0, fases_cero = 0, fases_grandes = 0;
static uint32_t fase_crudo_ms = 60000;
static const int MAX_TEXTOS = 400;
static const int MAX_CAMBIOS = 150;
static int textos_impresos = 0;
static uint8_t ultimo_valor[3][8];
static int cambios_impresos[3] = {0, 0, 0};

static uint8_t trama[600];
static size_t largo_trama = 0;

static uint8_t checksum(const uint8_t *datos, size_t n) {
    uint8_t c = 0;
    for (size_t i = 0; i < n; i++) c ^= datos[i];
    return ~c;
}

static ConteoTipo &conteo(uint16_t tipo) {
    for (int i = 0; i < num_tipos; i++) {
        if (tipos[i].tipo == tipo) return tipos[i];
    }
    tipos[num_tipos] = {tipo, 0, 0, 0};
    return tipos[num_tipos < MAX_TIPOS - 1 ? num_tipos++ : num_tipos];
}

static float leerFloat(const uint8_t *p) {
    float f;
    memcpy(&f, p, 4);  // el ESP32-C6 es little-endian, igual que el protocolo
    return f;
}

static float leerFloatManual(const uint8_t *p) {
    // Decodificación explícita little-endian, independiente del orden de la CPU.
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void volcar(const char *motivo, const uint8_t *t, size_t n) {
    uint16_t tipo = (t[5] << 8) | t[6];
    uint16_t largo = (t[3] << 8) | t[4];
    Serial.printf("# %s t=%lu tipo=%04X largo=%u hex:", motivo, (unsigned long)millis(), tipo, largo);
    for (size_t i = 0; i < n; i++) Serial.printf(" %02X", t[i]);
    Serial.println();
    const uint8_t *d = t + 8;
    if (largo % 4 == 0 && largo > 0) {
        Serial.print("#   floats LE (memcpy | manual):");
        for (uint16_t i = 0; i < largo; i += 4) {
            Serial.printf(" %.9g|%.9g", leerFloat(d + i), leerFloatManual(d + i));
        }
        Serial.println();
        Serial.print("#   u32 LE:");
        for (uint16_t i = 0; i < largo; i += 4) {
            uint32_t u;
            memcpy(&u, d + i, 4);
            Serial.printf(" %lu", (unsigned long)u);
        }
        Serial.println();
    }
}

static void procesarTrama() {
    uint16_t largo = (trama[3] << 8) | trama[4];
    uint16_t tipo = (trama[5] << 8) | trama[6];
    if (checksum(trama, 7) != trama[7]) {
        errores_cabecera++;
        return;
    }
    if (checksum(trama + 8, largo) != trama[8 + largo]) {
        errores_datos++;
        return;
    }
    ConteoTipo &c = conteo(tipo);
    c.total++;
    c.en_segundo++;
    if (tipo == 0x0100) {
        // Texto de depuración del firmware del radar.
        if (textos_impresos < MAX_TEXTOS) {
            textos_impresos++;
            Serial.printf("# texto t=%lu: ", (unsigned long)millis());
            for (uint16_t i = 0; i < largo; i++) {
                char ch = (char)trama[8 + i];
                Serial.print(ch >= 32 && ch < 127 ? ch : '.');
            }
            Serial.println();
        }
        return;
    }
    if (tipo == 0x0A17 || tipo == 0x0A29 || tipo == 0x0F09) {
        // Valores crudos cuando cambian, para deducir qué significan.
        int k = tipo == 0x0A17 ? 0 : (tipo == 0x0A29 ? 1 : 2);
        if (largo <= sizeof(ultimo_valor[0]) && memcmp(ultimo_valor[k], trama + 8, largo) != 0 &&
            cambios_impresos[k] < MAX_CAMBIOS) {
            memcpy(ultimo_valor[k], trama + 8, largo);
            cambios_impresos[k]++;
            Serial.printf("# cambio t=%lu tipo=%04X datos:", (unsigned long)millis(), tipo);
            for (uint16_t i = 0; i < largo; i++) Serial.printf(" %02X", trama[8 + i]);
            if (largo == 8) {
                uint32_t u0;
                memcpy(&u0, trama + 8, 4);
                Serial.printf(" | u32 %lu float %.6g", (unsigned long)u0, leerFloat(trama + 12));
                Serial.printf(" | floats %.6g %.6g", leerFloat(trama + 8), leerFloat(trama + 12));
            }
            Serial.println();
        }
    }
    if (c.volcados < VOLCADOS_POR_TIPO) {
        c.volcados++;
        volcar("muestra", trama, largo_trama);
    }
    if (tipo == 0x0A13 && largo >= 12) {
        uint32_t ahora = millis();
        uint32_t desde_anterior = ahora - ultima_fase_ms;
        ultima_fase_ms = ahora;
        float total = leerFloat(trama + 8);
        bool cero = total == 0.0f;
        bool grande = fabsf(total) > 2.0f;
        if (cero) fases_cero++;
        if (grande) fases_grandes++;
        fases_total++;
        // Solo se vuelcan las de fase_resp distinta de 0, para no repetir el arranque (todo en 0).
        if ((cero || grande) && leerFloat(trama + 12) != 0.0f && volcados_especiales < VOLCADOS_ESPECIALES) {
            volcados_especiales++;
            Serial.printf("# (%lu ms desde la trama de fase anterior)\n", (unsigned long)desde_anterior);
            volcar(cero ? "fase_total_cero" : "fase_total_grande", trama, largo_trama);
        }
    }
}

static void leerCrudo() {
    size_t disponible = uart.available();
    if (disponible > max_disponible) max_disponible = disponible;
    while (uart.available()) {
        uint8_t b = uart.read();
        if (largo_trama == 0 && b != 0x01) {
            bytes_descartados++;
            continue;
        }
        trama[largo_trama++] = b;
        if (largo_trama >= 8) {
            uint16_t largo = (trama[3] << 8) | trama[4];
            if (largo > 512) {
                bytes_descartados += largo_trama;
                largo_trama = 0;
                continue;
            }
            if (largo_trama == 8u + largo + 1u) {
                procesarTrama();
                largo_trama = 0;
            }
        }
    }
}

static void imprimirSegundoCrudo() {
    Serial.printf("# crudo/s seg=%lu estado0A29=%u presencia0F09=%u", (unsigned long)(millis() / 1000),
                  ultimo_valor[1][0], ultimo_valor[2][0]);
    for (int i = 0; i < num_tipos; i++) {
        Serial.printf(" %04X:%lu", tipos[i].tipo, (unsigned long)tipos[i].en_segundo);
        tipos[i].en_segundo = 0;
    }
    Serial.println();
}

static void enviarConsultaVersion(const uint8_t *datos, uint16_t largo) {
    uint8_t f[16];
    f[0] = 0x01;
    f[1] = 0x00; f[2] = 0x01;              // ID
    f[3] = largo >> 8; f[4] = largo & 0xFF;
    f[5] = 0xFF; f[6] = 0xFF;              // tipo 0xFFFF
    f[7] = checksum(f, 7);
    size_t n = 8;
    if (largo) {
        memcpy(f + 8, datos, largo);
        f[8 + largo] = checksum(datos, largo);
        n = 9 + largo;
    }
    Serial.print("# enviada consulta de version:");
    for (size_t i = 0; i < n; i++) Serial.printf(" %02X", f[i]);
    Serial.println();
    uart.write(f, n);
}

static void reiniciarContadores() {
    num_tipos = 0;
    errores_cabecera = errores_datos = bytes_descartados = 0;
    max_disponible = 0;
    volcados_especiales = 0;
    textos_impresos = 0;
    memset(ultimo_valor, 0, sizeof(ultimo_valor));
    memset(cambios_impresos, 0, sizeof(cambios_impresos));
    fases_total = fases_cero = fases_grandes = 0;
    largo_trama = 0;
}

static void correrCrudo() {
    uart.setRxBufferSize(32 * 1024);
    uart.begin(115200);
    Serial.printf("# FASE CRUDO %lu ms\n", (unsigned long)fase_crudo_ms);
    uint32_t t0 = millis(), proximo = t0 + 1000;
    while (millis() - t0 < fase_crudo_ms) {
        leerCrudo();
        if ((int32_t)(millis() - proximo) >= 0) {
            imprimirSegundoCrudo();
            proximo += 1000;
        }
    }
    float segundos = fase_crudo_ms / 1000.0f;
    Serial.println("# RESUMEN CRUDO");
    for (int i = 0; i < num_tipos; i++) {
        Serial.printf("#   tipo %04X: %lu tramas (%.2f/s)\n", tipos[i].tipo, (unsigned long)tipos[i].total,
                      tipos[i].total / segundos);
    }
    Serial.printf("#   errores checksum cabecera %lu, datos %lu, bytes descartados %lu, max bytes en buffer %u\n",
                  (unsigned long)errores_cabecera, (unsigned long)errores_datos,
                  (unsigned long)bytes_descartados, (unsigned)max_disponible);
    Serial.printf("#   tramas de fase: %lu, con fase_total en 0: %lu, con |fase_total| > 2: %lu\n",
                  (unsigned long)fases_total, (unsigned long)fases_cero, (unsigned long)fases_grandes);
}

static void correrVersion() {
    // Con la UART cruda todavía abierta.
    static const uint8_t datos_ld[4] = {0x01, 0x01, 0x00, 0x00};
    for (int intento = 0; intento < 2; intento++) {
        uint32_t antes_ffff = conteo(0xFFFF).total;
        enviarConsultaVersion(intento == 0 ? nullptr : datos_ld, intento == 0 ? 0 : 4);
        uint32_t t1 = millis();
        while (millis() - t1 < ESPERA_VERSION_MS) leerCrudo();
        Serial.printf("# respuestas 0xFFFF recibidas: %lu\n", (unsigned long)(conteo(0xFFFF).total - antes_ffff));
    }
}

static void correrModulo() {
    Serial.printf("# FASE MODULO %lu ms (radar.cpp)\n", (unsigned long)FASE_MODULO_MS);
    radar::iniciar();
    uint32_t t2 = millis(), proximo2 = t2 + 1000;
    uint32_t fases_antes = radar::tramasFase(), totales_antes = radar::tramasTotales();
    uint32_t lecturas = 0, lecturas_segundo = 0;
    while (millis() - t2 < FASE_MODULO_MS) {
        radar::actualizar();
        LecturaRadar l;
        while (radar::siguienteLectura(l)) {
            lecturas++;
            lecturas_segundo++;
        }
        if ((int32_t)(millis() - proximo2) >= 0) {
            Serial.printf("# modulo/s lecturas:%lu\n", (unsigned long)lecturas_segundo);
            lecturas_segundo = 0;
            proximo2 += 1000;
        }
    }
    float s2 = FASE_MODULO_MS / 1000.0f;
    Serial.printf("# RESUMEN MODULO: tramas de fase %lu (%.2f/s), tramas reconocidas %lu (%.2f/s), lecturas entregadas %lu\n",
                  (unsigned long)(radar::tramasFase() - fases_antes), (radar::tramasFase() - fases_antes) / s2,
                  (unsigned long)(radar::tramasTotales() - totales_antes), (radar::tramasTotales() - totales_antes) / s2,
                  (unsigned long)lecturas);
}

void setup() {
    Serial.begin(115200);
}

// Espera órdenes desde la PC (así el lector ya está conectado cuando empieza y no se pierde nada):
//   'd': crudo 60 s + consulta de versión + lectura con radar.cpp 30 s. Una vez por arranque,
//        porque radar.cpp deja abierta su propia UART.
//   'D': solo crudo 150 s (prueba con persona). Se puede repetir.
void loop() {
    static bool modulo_usado = false;
    static uint32_t ultimo_aviso = 0;
    int orden = Serial.read();
    if (orden == 'd' || orden == 'D') {
        if (modulo_usado) {
            Serial.println("# la lectura con radar.cpp ya se usó; reiniciar la placa para otra corrida");
        } else {
            reiniciarContadores();
            Serial.printf("# DIAGNOSTICO %s v%s orden '%c'\n", FIRMWARE_NOMBRE, FIRMWARE_VERSION, orden);
            fase_crudo_ms = orden == 'D' ? FASE_CRUDO_LARGA_MS : FASE_CRUDO_CORTA_MS;
            correrCrudo();
            if (orden == 'd') {
                correrVersion();
                uart.end();
                correrModulo();
                modulo_usado = true;
            } else {
                uart.end();
            }
            Serial.println("# FIN DIAGNOSTICO");
        }
    }
    if (millis() - ultimo_aviso >= 2000) {
        Serial.println("# diagnostico listo: enviar 'd' (completo) o 'D' (150 s crudo) para empezar");
        ultimo_aviso = millis();
    }
    delay(10);
}

#endif  // DIAGNOSTICO
