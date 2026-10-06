# firmware/base-radar

Firmware del XIAO ESP32-C6 que viene en el kit Seeed MR60BHA2.

## Módulos (`src/`)

| Módulo | Responsabilidad |
|---|---|
| `radar` | Leer respiración, latido, presencia y distancia del radar por UART |
| `espnow_rx` | Recibir mensajes del colgante, filtrar duplicados, responder ACK |
| `red_wifi` | Conexión WiFi STA y reconexión |
| `firebase_cliente` | Login del dispositivo y escritura en Realtime Database por REST |
| `telegram` | Mensajes al cuidador por la API de bots |
| `alerta_local` | Buzzer y LED; funciona sin red |
| `cola_offline` | Guardar lecturas cuando no hay red y reenviarlas al volver |

`radar` está implementado (ver `docs/radar.md`); los demás están vacíos con `TODO`. `main.cpp`
imprime una línea CSV por lectura del radar, que se captura con `analisis/capturar_serial.py`.

## Configuración

- `include/config.h`: versión, pines, periodos, rangos válidos del radar y tiempo de "radar sin
  datos".
- `include/secrets.h`: credenciales. Copiar de `include/secrets.example.h`. No se sube al repo.

## Hardware del kit

- Radar: UART0, RX = GPIO17 (D7), TX = GPIO16 (D6), 115200 baudios. Confirmado con el kit.
- Buzzer y LED: puerto Grove, D0 (GPIO0) y D10 (GPIO18), según el mapa de pines de Seeed. Falta
  probarlos con el buzzer conectado. El kit también trae un LED RGB WS2812 en D1.
- Al cerrar el puerto serie con DTR/RTS activos, la base se reinicia (ver `docs/radar.md`).

## Pendiente de confirmar con el módulo

- Que D0 y D10 del Grove funcionen para el buzzer y el LED.
- Antena que usa el kit (el XIAO ESP32-C6 tiene selector interna/externa).
