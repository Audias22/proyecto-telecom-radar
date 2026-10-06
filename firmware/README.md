# firmware

Código de los dos microcontroladores, cada uno como proyecto PlatformIO independiente.

| Carpeta | Placa | Función |
|---|---|---|
| `base-radar/` | XIAO ESP32-C6 (kit MR60BHA2) | Lee el radar, recibe al colgante, alerta local, Firebase y Telegram |
| `colgante/` | ESP32-C3 Super Mini | Botón de auxilio, deep sleep, envío por ESP-NOW |
| `comun/` | | Librería compartida: `protocolo.h` |

Ambos usan la plataforma pioarduino (arduino-esp32 v3) con la versión fija en `platformio.ini`.

Compilar desde la raíz del repositorio:

```
pio run -d firmware/base-radar
pio run -d firmware/colgante
```

Cargar y abrir el monitor serial:

```
pio run -d firmware/colgante -t upload
pio device monitor -d firmware/colgante
```

Cada proyecto necesita `include/secrets.h`, copiado de `include/secrets.example.h`.
