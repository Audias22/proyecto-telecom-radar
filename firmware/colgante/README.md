# firmware/colgante

Firmware del ESP32-C3 Super Mini del colgante.

Ciclo: deep sleep -> despierta por botón o por timer -> envía ALERTA o HEARTBEAT por ESP-NOW ->
espera ACK (reintentos y barrido de canales) -> beep -> deep sleep. Detalle en `docs/protocolo.md`.

## Módulos (`src/`)

| Módulo | Responsabilidad |
|---|---|
| `boton` | Botón de auxilio y causa del despertar |
| `espnow_tx` | Envío, espera de ACK, reintentos, barrido, canal y secuencia en RTC |
| `energia` | Deep sleep, fuentes de despertar, beeps de confirmación y error |
| `bateria` | Voltaje de la batería por ADC |

Todos están vacíos con `TODO`. `main.cpp` por ahora solo imprime nombre y versión.

## Configuración

- `include/config.h`: versión y pines (propuestos, por confirmar).
- `include/secrets.h`: MAC de la base e id del colgante. Copiar de `include/secrets.example.h`.

## Hardware

- Botón en GPIO0-GPIO5 (únicos pines que despiertan al ESP32-C3 de deep sleep).
- Batería Li-ion 240 mAh con cargador TP4056.
- El módulo TP4056 comprado es la versión con protección (USB-C). Por confirmar con el módulo el
  voltaje de corte por descarga, para fijar `BATERIA_BAJA_MV` con margen sobre ese corte.
- La corriente de carga del módulo viene en 1 A; hay que cambiar R3 por 4.7 kΩ antes de conectar
  la batería de 240 mAh (ver nota técnica en el README principal).
- Por confirmar con el módulo: consumo real en deep sleep de la placa completa (LED de encendido,
  regulador, divisor de batería). Define la duración de la batería.
- Mientras el colgante duerme, el puerto USB desaparece. Para cargar firmware, mantener BOOT
  presionado al conectar si el puerto no aparece.
