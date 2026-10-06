# Radar MR60BHA2: interfaz y mediciones

Código: `firmware/base-radar/src/radar.*`. Capturas: `pruebas/crudos/2026-10-06_*.csv`.

## 1. Librería

- Librería oficial de Seeed: **Seeed Arduino mmWave**,
  https://github.com/Seeed-Projects/Seeed-mmWave-library (en el Library Manager de Arduino se
  llama igual).
- Fijada en `platformio.ini` por commit `ade050c` (rama `main`, v1.0.0). No está en el registro de
  PlatformIO. Las versiones v1.1.0-rc1 y v2.0.0-rc1 existen, pero el propio README de la librería
  dice que no están validadas en hardware.
- Clase `SEEED_MR60BHA2`. Getters: `getHeartBreathPhases(total, resp, latido)`,
  `getBreathRate()`, `getHeartRate()`, `getDistance()`, `isHumanDetected()`. Cada getter devuelve
  `true` solo una vez por trama nueva (limpia su bandera al leer).

Detalles del código fuente que condicionan el diseño:

- `update(timeout)` hace espera activa durante todo `timeout` (el ejemplo usa 100 ms, que
  bloquearía el loop). Con `update(0)` lee lo que haya en la UART una sola vez y procesa **una**
  trama de su cola. El módulo `radar` llama `update(0)` varias veces por vuelta de `loop()` y lee
  el getter correspondiente después de cada trama, así no se pierde ninguna muestra de fase.
- `isHumanDetected()` devuelve `false` tanto si no hay persona como si no llegó trama nueva, y
  `getDistance()` devuelve `false` cuando no hay objetivo. Por eso el módulo sobrescribe
  `handleType()` (llamando a la implementación de la librería) para leer directamente el byte de
  presencia y la bandera de objetivo.

## 2. Conexión dentro del kit

| Señal | Pin del XIAO ESP32-C6 | Fuente | Estado |
|---|---|---|---|
| UART radar -> XIAO | GPIO17 (D7, U0RXD) | `HardwareSerial(0)` del ejemplo oficial; pines por defecto de UART0 en el ESP32-C6 (`HardwareSerial.h` de arduino-esp32) | Confirmado: llegan datos |
| UART XIAO -> radar | GPIO16 (D6, U0TXD) | igual | Confirmado (misma UART) |
| Baudios | 115200 | `_UART_BAUD` de la librería | Confirmado |
| LED RGB WS2812 | GPIO1 (D1) | mapa de pines del README de la librería (v1.1.0-rc1) y ejemplo `LightRGB` | No probado |
| Sensor de luz BH1750 | GPIO22/23 (SDA/SCL) | igual | No probado |
| Puerto Grove | D0 (GPIO0) y D10 (GPIO18) | igual | Libres; propuestos para buzzer y LED, falta probarlos |

## 3. Tramas que manda el radar

Medido contando tramas por tipo con el radar sin objetivo (diagnóstico del 2026-10-06):

| Tipo | Contenido | Lo maneja la librería |
|---|---|---|
| `0x0A13` | Fases: total, respiración, latido (3 float) | Sí |
| `0x0A14` | Respiración (float, rpm) | Sí |
| `0x0A15` | Latido (float, bpm) | Sí |
| `0x0A16` | Distancia: bandera de objetivo (u32) + distancia (float) | Sí |
| `0x0F09` | Presencia (1 byte) | Sí |
| `0x0A17` | Desconocido | No |
| `0x0A29` | Desconocido | No |

Sin objetivo, los siete tipos llegaron a la misma tasa, unas 8.7 por segundo cada uno (una cada
~115 ms), con todos los valores en 0 y presencia 0.

## 4. Tasa de las tramas de fase

El módulo arma una lectura por cada trama `0x0A13`, así que la tasa de lecturas del CSV es la
tasa de muestreo de las fases. Capturas de 60 s cada una:

| Escena | Lecturas | Tasa | Intervalo entre lecturas |
|---|---|---|---|
| Sin objetivo (diagnóstico) | | 8.7 /s | ~115 ms |
| Persona sentada, ~80 cm estimados | 972 | 16.18 /s | media 61.8 ms; alterna ~45 ms y ~76 ms |
| Persona frente a la computadora | 972 | 16.20 /s | media 61.7 ms; igual patrón |
| Radar apuntando a una pared, sin nadie | 965 | 16.06 /s | media 62.3 ms |

Conclusión para el FFT (paso 6):

- Con objetivo, ~16 muestras/s: frecuencia de Nyquist ~8 Hz. La respiración (0.1-0.5 Hz) y el
  latido (0.8-3.3 Hz, es decir 50-200 bpm) quedan muy por debajo. El FFT es posible.
- Resolución en frecuencia = 1 / duración de la ventana: 30 s dan 0.033 Hz (2 rpm o 2 bpm); 60 s
  dan 0.017 Hz (1 rpm). Para respiración conviene una ventana de 30-60 s.
- El muestreo **no es uniforme**: los intervalos alternan entre ~45 ms y ~76 ms. Antes del FFT
  hay que remuestrear a una rejilla uniforme (interpolación) o usar un método para muestreo
  irregular (Lomb-Scargle). La marca `ms` es la hora de recepción en el ESP32, no la del radar.
- Sin objetivo la tasa baja a 8.7/s, pero en ese caso no hay nada que analizar.

## 5. Valores observados

| Escena | Presencia | Distancia (cm) | Resp. (rpm) | Latido (bpm) | `fase_resp` | `fase_latido` |
|---|---|---|---|---|---|---|
| Persona sentada | 100 % | 46-63, mediana 57 | mediana 17 (1-23) | mediana 78 (65-99) | ±0.57 | ±1.24 |
| Persona frente a la computadora | 100 % | 40-103, mediana 52 | mediana 18 | mediana 98 | ±0.94 | ±1.43 |
| Pared, sin nadie | 100 % | 132 fijo | 0 | 108 fijo | ±0.009 | ±0.08 |

`fase_total` varía entre -π y π (fase envuelta).

Observaciones importantes:

1. **Una pared se ve como "presencia con respiración 0".** Con el radar apuntando a una pared, el
   radar reportó presencia todo el tiempo, distancia fija, respiración 0 y un latido fijo (el último
   que había medido). Esas lecturas pasan los rangos y quedan `valida = 1`. Para la detección de
   apnea es justo el caso peligroso: hay que distinguir "persona que no respira" de "objeto
   quieto". Lo que sí las separa en las capturas es la amplitud de las fases (`fase_resp` ~60 veces
   menor con la pared) y que el latido no cambia. Se resuelve en el paso de detección.
2. **Al quitar a la persona, el latido queda congelado** en el último valor medido (108 o 82 bpm
   en distintas pruebas) y la respiración cae a 0.
3. **Distancia:** el radar reportó 46-63 cm con la persona sentada a una distancia estimada a ojo
   de ~80 cm. Falta medir con cinta para saber si es error de la estimación o del radar.
4. Los primeros segundos con persona la respiración sale baja (1-4 rpm) mientras el radar se
   estabiliza.

## 6. Captura de datos

```
cd analisis
python capturar_serial.py persona_dormida_1m --duracion 120
```

Formato del CSV (fijo):

```
ms,valida,presencia,distancia_cm,resp_rpm,latido_bpm,fase_total,fase_resp,fase_latido
```

- `valida` y `presencia`: 0/1. Valores no recibidos: `nan`.
- `valida = 1` si respiración está en 0-60 rpm, latido en 30-200 bpm, distancia en 0-300 cm y las
  tres fases existen.
- Al abrir el puerto con la configuración por defecto de pyserial, la base se reinicia cada vez
  que se cierra el puerto (los cambios de DTR/RTS reinician el ESP32-C6 por el USB serie). El
  script abre el puerto con DTR y RTS en `False`, y con eso no se reinicia (probado abriendo y
  cerrando cuatro veces). El monitor de PlatformIO sí puede reiniciar la base al conectarse.

## 7. Radar sin datos

Si no llega ninguna trama válida en `RADAR_SIN_DATOS_MS` (5 s, en `config.h`), `radar::sinDatos()`
pasa a `true`, se borran respiración, latido y distancia (para no usar valores viejos) y se
reinicia la UART; se reintenta cada 5 s mientras siga sin datos. `main.cpp` imprime un comentario
`# radar sin datos` / `# radar con datos` cuando cambia. No se probó desconectando el radar, porque
la UART está dentro del kit.
