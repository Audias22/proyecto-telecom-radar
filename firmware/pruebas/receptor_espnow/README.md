# firmware/pruebas/receptor_espnow

Receptor ESP-NOW de prueba para el colgante. Corre en un ESP32 clásico (DevKitC con módulo
WROOM-32, placa `esp32dev` en pioarduino) y hace de base en el banco de pruebas. No es la base
real: no se conecta a WiFi, no usa Firebase ni Telegram y no tiene radar.

Qué hace:

- Escucha en un canal fijo (al arrancar, `CANAL_POR_DEFECTO` = 1) sin asociarse a ningún router.
- Recibe ALERTA y HEARTBEAT con el formato de `firmware/comun/protocolo/protocolo.h`, aplica la
  regla de `duplicados.h` y responde `MSG_ACK` dentro del mismo callback de recepción, antes de
  imprimir nada. A un duplicado le responde con `FLAG_ACK_DUPLICADO`.
- Imprime una línea por mensaje recibido.
- Al arrancar imprime su MAC STA, ya en el formato de `secrets.h` del colgante.

## Compilar y cargar

Desde la raíz del repositorio:

```
pio run -d firmware/pruebas/receptor_espnow
pio run -d firmware/pruebas/receptor_espnow -t upload
pio device monitor -d firmware/pruebas/receptor_espnow
```

La primera compilación descarga el compilador Xtensa del ESP32 clásico (el colgante y la base
usan RISC-V). En Windows hay que hacerla desde PowerShell o CMD: el instalador de herramientas de
ESP-IDF falla desde Git Bash ("MSys/Mingw is not supported"). El entorno de PlatformIO debe usar
Python 3.10-3.13; pioarduino 55.03.37 rechaza 3.14 en Windows.

El DevKitC tiene un puente USB-UART (CP2102 o CH340): si Windows no muestra el puerto COM, falta
el driver del puente.

## Salida

Ejemplo de una línea (una por mensaje):

```
[    15230 ms] 34:85:18:AA:BB:CC ALERTA    id=1 seq=40213 flags=ARRANQUE_FRIO bat=3900 mV rssi=-41 dBm canal=1 NUEVO ack=OK en 182 us
```

| Campo | Significado |
|---|---|
| `[... ms]` | `millis()` del receptor al recibir |
| MAC | MAC de origen (la del colgante) |
| tipo | `ALERTA` o `HEARTBEAT` |
| `id`, `seq` | `id_dispositivo` y `secuencia` del paquete |
| `flags` | `BAT_BAJA`, `REINTENTO`, `BARRIDO`, `ARRANQUE_FRIO` separados por `|`; `-` sin flags |
| `bat` | `bateria_mv` (3900 con la batería simulada; 0 = sin dato) |
| `rssi`, `canal` | Del paquete recibido |
| `NUEVO` / `DUPLICADO` | Resultado de `duplicados::esNuevo()` |
| `ack=...` | `OK` o `OK+DUPLICADO` y el tiempo desde que entró el paquete hasta que `esp_now_send()` aceptó el ACK. No incluye el tiempo en el aire. `OMITIDO` si se usó el comando `p`; `ERROR` si `esp_now_send()` falló |

Paquetes de otro tamaño, otra versión o tipo `ACK` se imprimen como `INVALIDO` y no se responden.

## Comandos (escribir en el monitor y Enter)

| Comando | Efecto |
|---|---|
| `1` ... `13` | Cambia de canal. Sirve para probar el barrido del colgante |
| `p` | No responde el ACK del próximo mensaje nuevo. El colgante reintenta y el receptor debe marcar el reintento como `DUPLICADO` y responderlo |
| `o` | Olvida las secuencias guardadas, como si la base se reiniciara |
| `e` | Estado: MAC, canal y contadores |
| `h` | Ayuda |

Las pruebas que usan este receptor están en `firmware/colgante/pruebas_fisicas.md`.
