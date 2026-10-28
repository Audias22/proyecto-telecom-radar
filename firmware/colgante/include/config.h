// config.h
// Configuración del colgante que no es secreta: versión y pines.

#pragma once

#define FIRMWARE_NOMBRE  "colgante"
#define FIRMWARE_VERSION "0.1.0"

// Registros de diagnostico por USB CDC. En 0, main no inicializa Serial ni emite sus registros;
// se recomienda deshabilitarlos en la operacion normal para reducir tiempo despierto y consumo.
// La espera al monitor USB solo se hace en el arranque en frio, nunca al despertar de deep sleep.
#define REGISTROS_SERIAL_HABILITADOS 1
#define REGISTROS_SERIAL_ESPERA_MS   2000UL

// Pines propuestos. Por confirmar con el módulo y el circuito armado.
// - El botón debe estar en GPIO0-GPIO5: son los únicos que despiertan al ESP32-C3 de deep sleep.
//   Se evita GPIO2 porque es pin de arranque (strapping).
// - La medición de batería necesita un pin con ADC1 (GPIO0-GPIO4).
#define PIN_BOTON        3
#define PIN_BATERIA_ADC  1
#define PIN_BUZZER       10

// Potencia maxima de transmision, en unidades de 0.25 dBm (como esp_wifi_set_max_tx_power();
// rango 8-84, es decir 2-21 dBm). Medido en banco con el ESP32-C3 Super Mini a ~3 m con linea de
// vista (pruebas/resultados/2026-10-12_colgante_banco.md): 34 (8.5 dBm) dio -91.1 dBm de RSSI
// promedio y 80 (20 dBm), -85.4 dBm. En esta placa bajar la potencia no mejora la senal; con 80 se
// ganan ~6 dB. Aparte, la altura y la orientacion del receptor cambian mucho el RSSI.
#define COLGANTE_TX_POTENCIA_QDBM 80

// Medicion de bateria. Con 1 se envia BATERIA_SIMULADA_MV sin tocar el ADC; solo para desarrollar
// sin bateria, no es una medicion. Con la bateria y el divisor armados (2026-10-27) se mide.
#define BATERIA_MODO_SIMULADO          0
#define BATERIA_SIMULADA_MV         3900U
#define BATERIA_LECTURA_INVALIDA_MV     0U

// Divisor armado: OUT+ del TP4056 -- 100 kohm -- GPIO1 -- 100 kohm -- GND, sin capacitor (no hay
// 1 Mohm, 470 kohm ni 10 nF). Factor 2: con 4.2 V la bateria deja 2.1 V en GPIO1. Corriente
// continua del divisor, tambien en deep sleep: 4.2 V / 200 kohm = 21 uA (~0.5 mAh por dia).
#define BATERIA_RESISTENCIA_SUPERIOR_OHM   100000UL
#define BATERIA_RESISTENCIA_INFERIOR_OHM   100000UL
#define BATERIA_TENSION_MAXIMA_MV             4200U

// ADC real: 12 bits, atenuacion ADC_11db y promedio de lecturas calibradas. En arduino-esp32 3.3.7
// ADC_11db es la ADC_ATTEN_DB_12 de ESP-IDF (hal/adc_types.h: ADC_ATTEN_DB_11 = ADC_ATTEN_DB_12).
// Hoja de datos ESP32-C3 v2.4, tabla 5-6: rango efectivo 0-750 mV (ATTEN0), 0-1050 (ATTEN1),
// 0-1300 (ATTEN2) y 0-2500 mV (ATTEN3), con error total de +-35 mV en ATTEN3. Los 2.1 V solo
// caben en ATTEN3.
#define BATERIA_ADC_RANGO_ATTEN3_MV             2500U
#define BATERIA_ADC_RESOLUCION_BITS              12U
#define BATERIA_ADC_MUESTRAS                      16U

// Una lectura del pin por encima de este valor se descarta (centinela 0). Peor caso esperado:
// 4.24 V (4.2 V +1 % del TP4056) con resistencias de 5 % da 2227 mV, mas 35 mV de error del ADC,
// 2262 mV. 2400 queda arriba de eso y abajo del final del rango de ATTEN3 (2500 mV).
#define BATERIA_ADC_LIMITE_MV                   2400U

// Impedancia de la fuente vista por el ADC: 100 k || 100 k = 50 kohm, sin capacitor que entregue
// la carga del muestreo. Espressif caracteriza el ADC con 100 nF externos (tabla 5-5), asi que este
// caso no esta cubierto por la hoja de datos. Se acepta porque la tension de la bateria cambia
// en horas: la primera conversion se descarta y se promedian BATERIA_ADC_MUESTRAS lecturas, lo que
// reduce el ruido (la hoja recomienda promediar). Riesgo que queda: un error fijo por la carga del
// capacitor de muestreo a traves de 50 kohm, que el promedio no corrige, y ruido del radio si se
// mide cerca de una transmision. Por confirmar con el modulo: comparar con el multimetro
// (pruebas_fisicas.md, seccion 4).

// Tiempos del boton. La espera de liberacion siempre termina al alcanzar el limite,
// aunque el boton permanezca presionado o el contacto siga rebotando.
#define BOTON_ANTIRREBOTE_MS          30UL
#define BOTON_ESPERA_LIBERACION_MS  5000UL

// Buzzer: modulo de buzzer activo de 3 pines (VCC, I/O, GND) con transistor PNP S8550 y 1 kohm en
// la base. VCC a 3V3 (no 5V: con 5V el PNP no se apaga con 3.3 V en I/O), GND a GND, I/O a
// PIN_BUZZER. Suena con I/O en bajo. En 0, para un buzzer que suena con el pin en alto.
#define BUZZER_ACTIVO_EN_BAJO 1

// Duracion de los sonidos.
#define BUZZER_CONFIRMACION_MS       120UL
#define BUZZER_ERROR_MS              500UL
#define BUZZER_PAUSA_ERROR_MS        250UL
#define BUZZER_ERROR_REPETICIONES      3U

// Intervalo del heartbeat solo para pruebas en banco, en segundos. En 0 se usa el normal de
// protocolo.h (HEARTBEAT_INTERVALO_S, 15 min). Volver a 0 antes de usar el colgante en serio.
#define HEARTBEAT_PRUEBA_S 0U

// Si el boton sigue bajo al dormir, se usa temporalmente solo el timer. Este intervalo corto
// permite comprobar de nuevo la liberacion sin entrar en un ciclo de wake inmediato.
#define BOTON_REVISION_ATASCADO_S      5U
#define BOTON_REVISION_LECTURA_MS    100UL

// Evita que un error persistente de configuracion provoque reintentos en un bucle caliente.
#define ENERGIA_ERROR_REINTENTO_MS  1000UL
