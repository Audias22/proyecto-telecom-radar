# Pruebas host del colgante

Pruebas unitarias de la lógica pura del colgante que se compilan y ejecutan en un PC, sin
ESP32-C3, sin base física, sin Arduino y sin WiFi.

`test_logica.cpp` incluye los headers reales y no duplica su lógica:

- `firmware/comun/protocolo/protocolo.h` (paquete, flags, canales, tiempos).
- `src/espnow_tx.h`, namespace `espnow_tx::logica` (`construirPaquete`, `ackValido`,
  `prepararFlags`, `canalValido`, `canalAGuardar`).
- `src/bateria.h`, namespace `bateria::logica` (`esBaja`, `lecturaBateriaValida`,
  `lecturaAdcValida`, `convertirAdcABateria`).
- `src/despertar.h`, `despertar::decidir()`: decisión ALERTA / HEARTBEAT / ninguna al arrancar.
- `firmware/comun/protocolo/duplicados.h`: regla de duplicados que aplicará la base.
- `include/config.h` (umbrales y constantes de batería).

No usa `secrets.h` ni ningún archivo `.cpp` del firmware. No necesita un framework de pruebas:
cada caso imprime `[ OK  ]` o `[FALLO]`, y el programa termina con código `0` solo si todos los
casos pasan.

Estado actual: **43 casos y 653 verificaciones**. Además, dos `static_assert` comprueban en
compilación que `decidir()` produce ALERTA y HEARTBEAT en sus casos principales.

## Casos

| Área | Qué se verifica |
|---|---|
| Paquete | `sizeof == 8`, desplazamiento de cada campo, bytes exactos del ejemplo ALERTA y del ACK con canal 7 de `docs/protocolo.md` (también armado con `ackConCanal`), secuencia en little-endian |
| Construcción | Versión `1`, tipo ALERTA `1` / HEARTBEAT `2`, id `7`/`255`, secuencias `0`, `1`, `0x7FFF`, `0x8000`, `0xFFFF`; sin `FLAG_REINTENTO`, `FLAG_BARRIDO` ni bits reservados |
| Batería | `0`, `3499` → baja; `3500`, `3900` → no baja; el flag del paquete coincide con `esBaja`; `0 mV` es el centinela inválido; límite ADC `2400/2401 mV` (dentro de los 2500 mV de ATTEN3) y peor caso esperado `2262 mV` válido; divisor 100 k / 100 k: `2100 → 4200`, `1750 → 3500`, `1931 → 3862 mV` |
| Flags | Valores de bits; `FLAG_ARRANQUE_FRIO` solo con `arranque_frio`; las cuatro combinaciones de `prepararFlags`; conserva flags previos; idempotencia |
| Retransmisión | Los `ACK_REINTENTOS` intentos del canal guardado y los `13 × ACK_REINTENTOS_BARRIDO` del barrido conservan secuencia, versión, tipo, id y batería; `FLAG_REINTENTO` desde el segundo intento; barrido con `FLAG_REINTENTO | FLAG_BARRIDO`; el ACK de la secuencia original confirma cualquier retransmisión |
| ACK | Aceptado con y sin `FLAG_ACK_DUPLICADO` y en secuencias límite; rechazado por versión, tipo (ALERTA, HEARTBEAT, 0, 4, eco del propio paquete), id y secuencia (incluido el byte-swap) |
| Canal en el ACK | `ackConCanal` arma flags y canal; `canalAGuardar` guarda el canal que anuncia la base (1..13) aunque el ACK se oiga en otro canal (caso medido: base en 7, ACK oído en 6); con `0` o fuera de rango (`14`, `255`, `256`, `263`, `3900`, `0xFFFF`) guarda el canal de recepción; `263` no se trunca a 7 |
| Canales | `0` y `14..255` inválidos; `1..13` válidos (barrido completo `0..255`); `CANAL_POR_DEFECTO` válido |
| Tiempos | Peor caso `3×100 + 13×2×50 = 1600 ms`, como en `docs/protocolo.md` |
| Duplicados | El primer mensaje tras arrancar conserva `FLAG_ARRANQUE_FRIO` en reintentos y barrido, y si la base ya lo procesó (ACK perdido) lo toma como duplicado; sin historia, nuevo; reintento de la última secuencia, duplicado; secuencia 1 con `FLAG_ARRANQUE_FRIO` tras reiniciar, nueva; desborde 65535 → 0; `secuenciaInicial` mezcla las dos mitades del valor aleatorio y, recorriendo los 65 536 sorteos, solo uno choca con la última secuencia de la base |
| Despertar | ALERTA por wake GPIO con el bit de GPIO3; pulsación breve ya liberada sigue siendo ALERTA; HEARTBEAT por temporizador; HEARTBEAT al encender (cualquier causa y bit del botón); ninguna transmisión en reinicio por fallo, en recuperación de botón atascado (con prioridad sobre cualquier otra entrada), en wake GPIO ajeno ni en causa no operativa; valores de enum desconocidos no transmiten; tabla explícita de las 36 combinaciones con acción y motivo |

### Decisión de despertar

`main.cpp` conserva la lectura del hardware (`energia::recuperandoBotonAtascado()`,
`esp_reset_reason()`, `esp_sleep_get_wakeup_cause()`, `boton::despertoPorBoton()`), la convierte a
`despertar::Entradas` y llama a `despertar::decidir()`. La tabla de las 36 combinaciones
(recuperación × reinicio × causa × bit del botón) está escrita fila por fila como especificación;
no recalcula el resultado con la lógica de producción.

`Entradas` no tiene campo para el nivel actual del botón: la decisión depende solo del estado de
wake retenido por el RTC, de modo que una pulsación breve ya liberada no puede descartar la alerta.

El caso `ack: campos no validados` documenta el comportamiento actual: `ackValido()` no revisa
el campo de canal (`bateria_mv`) ni los bits reservados del ACK; un canal fuera de rango solo hace
que se guarde el canal de recepción. No es un fallo del protocolo (que no exige descartarlos),
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
