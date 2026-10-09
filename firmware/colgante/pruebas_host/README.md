# Pruebas host del colgante

Pruebas unitarias de la lógica pura del colgante que se compilan y ejecutan en un PC, sin
ESP32-C3, sin base física, sin Arduino y sin WiFi.

`test_logica.cpp` incluye los headers reales y no duplica su lógica:

- `firmware/comun/protocolo/protocolo.h` (paquete, flags, canales, tiempos).
- `src/espnow_tx.h`, namespace `espnow_tx::logica` (`construirPaquete`, `ackValido`,
  `prepararFlags`, `canalValido`).
- `src/bateria.h`, namespace `bateria::logica` (`esBaja`, `lecturaBateriaValida`,
  `lecturaAdcValida`, `convertirAdcABateria`).
- `src/despertar.h`, `despertar::decidir()`: decisión ALERTA / HEARTBEAT / ninguna al arrancar.
- `include/config.h` (umbrales y constantes de batería).

No usa `secrets.h` ni ningún archivo `.cpp` del firmware. No necesita un framework de pruebas:
cada caso imprime `[ OK  ]` o `[FALLO]`, y el programa termina con código `0` solo si todos los
casos pasan.

Estado actual: **35 casos y 538 verificaciones**. Además, dos `static_assert` comprueban en
compilación que `decidir()` produce ALERTA y HEARTBEAT en sus casos principales.

## Casos

| Área | Qué se verifica |
|---|---|
| Paquete | `sizeof == 8`, desplazamiento de cada campo, bytes exactos del ejemplo ALERTA y del ACK de `docs/protocolo.md`, secuencia en little-endian |
| Construcción | Versión `1`, tipo ALERTA `1` / HEARTBEAT `2`, id `7`/`255`, secuencias `0`, `1`, `0x7FFF`, `0x8000`, `0xFFFF`; sin `FLAG_REINTENTO`, `FLAG_BARRIDO` ni bits reservados |
| Batería | `0`, `3499` → baja; `3500`, `3900` → no baja; el flag del paquete coincide con `esBaja`; `0 mV` es el centinela inválido; límite ADC `1500/1501 mV`; divisor `1343 → 4200 mV` |
| Flags | Valores de bits; `FLAG_ARRANQUE_FRIO` solo con `arranque_frio`; las cuatro combinaciones de `prepararFlags`; conserva flags previos; idempotencia |
| Retransmisión | Los `ACK_REINTENTOS` intentos del canal guardado y los `13 × ACK_REINTENTOS_BARRIDO` del barrido conservan secuencia, versión, tipo, id y batería; `FLAG_REINTENTO` desde el segundo intento; barrido con `FLAG_REINTENTO | FLAG_BARRIDO`; el ACK de la secuencia original confirma cualquier retransmisión |
| ACK | Aceptado con y sin `FLAG_ACK_DUPLICADO` y en secuencias límite; rechazado por versión, tipo (ALERTA, HEARTBEAT, 0, 4, eco del propio paquete), id y secuencia (incluido el byte-swap) |
| Canales | `0` y `14..255` inválidos; `1..13` válidos (barrido completo `0..255`); `CANAL_POR_DEFECTO` válido |
| Tiempos | Peor caso `3×100 + 13×2×50 = 1600 ms`, como en `docs/protocolo.md` |
| Despertar | ALERTA por wake GPIO con el bit de GPIO3; pulsación breve ya liberada sigue siendo ALERTA; HEARTBEAT por temporizador; ninguna transmisión en arranque frío o reinicio inesperado, en recuperación de botón atascado (con prioridad sobre cualquier otra entrada), en wake GPIO ajeno ni en causa no operativa; valores de enum desconocidos no transmiten; tabla explícita de las 24 combinaciones con acción y motivo |

### Decisión de despertar

`main.cpp` conserva la lectura del hardware (`energia::recuperandoBotonAtascado()`,
`esp_reset_reason()`, `esp_sleep_get_wakeup_cause()`, `boton::despertoPorBoton()`), la convierte a
`despertar::Entradas` y llama a `despertar::decidir()`. La tabla de las 24 combinaciones
(recuperación × reinicio × causa × bit del botón) está escrita fila por fila como especificación;
no recalcula el resultado con la lógica de producción.

`Entradas` no tiene campo para el nivel actual del botón: la decisión depende solo del estado de
wake retenido por el RTC, de modo que una pulsación breve ya liberada no puede descartar la alerta.

El caso `ack: campos no validados` documenta el comportamiento actual: `ackValido()` no revisa
`bateria_mv` ni los bits reservados del ACK. No es un fallo del protocolo (que no exige descartarlos),
pero si se endurece la validación esta prueba debe actualizarse.

## Limitaciones

- La retransmisión se prueba componiendo las funciones puras de la misma forma que
  `enviarIntentos()`; esa función, `enviar()`, el barrido real, la cola de ACK, el filtro por MAC
  y por canal dependen de ESP-NOW y FreeRTOS y no se ejecutan aquí.
- La decisión de despertar se prueba en `despertar::decidir()`. La conversión de los valores de
  ESP-IDF a `despertar::Causa` / `despertar::Reinicio` y la lectura del hardware quedan en
  `main.cpp` y no se ejecutan en host; tampoco se prueba que el ESP32-C3 reporte realmente esas
  causas ni el bit de GPIO3 tras deep sleep.
- La persistencia RTC de secuencia, canal y recuperación del botón, el ADC real, el antirrebote
  real, el buzzer y el deep sleep no se prueban.
- Estas pruebas no son evidencia de funcionamiento físico del colgante: no ejercitan radio,
  alcance, consumo ni la interacción con una base real.
- El formato binario se verifica en un host little-endian (x86-64), igual que el ESP32-C3. El
  compilador del PC no es el del firmware; la compilación con PlatformIO sigue siendo necesaria.

## Ejecutar

Windows (PowerShell), desde `firmware/colgante/pruebas_host`:

```powershell
# Solo si no hay g++ ni clang++ en PATH: compilador C++ distribuido por pip (una sola vez)
python -m pip install ziglang==0.13.0

powershell -ExecutionPolicy Bypass -File .\ejecutar.ps1
```

La primera ejecución con ziglang compila la `libunwind` de zig y muestra advertencias
`-Wdll-attribute-on-redeclaration` de archivos dentro de `site-packages\ziglang`. No provienen del
proyecto; desde la segunda ejecución quedan en caché y no aparecen.

Linux, macOS o Git Bash con `g++` o `clang++`:

```bash
./ejecutar.sh
```

Comando manual equivalente:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  -I../../comun/protocolo -I../include -I../src \
  test_logica.cpp -o build/test_logica && ./build/test_logica
```

El ejecutable queda en `build/`, que está ignorado por Git.

Después de cambiar la lógica, compilar también el firmware desde `firmware/colgante`:

```bash
pio run -e colgante
```
