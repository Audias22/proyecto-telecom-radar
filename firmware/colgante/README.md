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
- Por confirmar con el módulo: si el TP4056 usado incluye protección de descarga (DW01); si no,
  el firmware debe dejar de transmitir por debajo de un voltaje mínimo.
- Por confirmar con el módulo: consumo real en deep sleep de la placa completa (LED de encendido,
  regulador, divisor de batería). Define la duración de la batería.
- Mientras el colgante duerme, el puerto USB desaparece. Para cargar firmware, mantener BOOT
  presionado al conectar si el puerto no aparece.
