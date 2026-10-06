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

Todos están vacíos con `TODO`. `main.cpp` por ahora solo imprime nombre y versión.

## Configuración

- `include/config.h`: versión, pines y periodos. Los pines del buzzer y LED están por confirmar.
- `include/secrets.h`: credenciales. Copiar de `include/secrets.example.h`. No se sube al repo.

## Pendiente de confirmar con el módulo

- Pines de la UART del radar y protocolo de tramas (o uso de la librería de Seeed).
- Pines libres del XIAO dentro del kit para buzzer y LED.
- Antena que usa el kit (el XIAO ESP32-C6 tiene selector interna/externa).
