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

## 8. Procesamiento propio de la respiración

Script: `analisis/estimar_respiracion.py`. Pruebas: `analisis/test_estimar_respiracion.py`.
Resultados por captura: `pruebas/resultados/<captura>/resumen.txt` y `grafica.png`.

### Por qué

En la captura `2026-10-06_172200_persona_sentada_84cm` (persona sentada a ~84 cm, conteo manual
de 15-16 respiraciones en el minuto) el `resp_rpm` del módulo dio una mediana de 5 rpm y se quedó
en 0 durante dos tramos de ~5 s y ~14 s mientras la persona respiraba normal. Con un valor que cae
a 0 sin que la persona deje de respirar, una alerta de apnea basada en él daría falsas alarmas.

### Método

1. **Remuestreo** de `fase_resp` a 10 Hz por interpolación lineal (el radar entrega muestras cada
   ~45/76 ms alternados, ver sección 4).
2. **Tendencia y filtro:** se quita la tendencia lineal y se aplica un pasabanda Butterworth de
   orden 2 entre 0.1 y 0.6 Hz (6-36 rpm), ida y vuelta (`sosfiltfilt`, sin desfase).
3. **Ventana deslizante** de 30 s (300 muestras) con paso de 1 s. En cada ventana:
   - **FFT:** ventana Hann, zero-padding a 4096 puntos (resolución 0.0024 Hz = 0.15 rpm), pico
     dentro de la banda e interpolación parabólica sobre el logaritmo de la magnitud.
   - **Cruces por cero ascendentes** con histéresis: un cruce cuenta solo si antes la señal bajó
     de −0.05 × RMS. Frecuencia = (cruces − 1) / tiempo entre el primer y el último cruce, con el
     instante de cada cruce interpolado.
   - **RMS** de la señal filtrada.
4. La estimación de cada ventana se asigna al final de la ventana, que es cuando estaría
   disponible en tiempo real.

Todo se puede portar al ESP32: buffer circular de 300 muestras, FFT de tamaño fijo, filtro IIR
en dos biquads. Lo único que cambia es el filtro ida y vuelta, que en tiempo real se aplica sobre
el buffer de la ventana (es de tamaño fijo) o se reemplaza por el filtro causal aceptando su
retardo.

Las pruebas con una señal sintética (seno de 0.25 Hz + ruido + deriva + el muestreo irregular del
radar, 120 s) dan errores máximos de 0.04 rpm (FFT) y 0.05 rpm (cruces) en las 91 ventanas.

### Resultados

Mediana por ventana y, entre paréntesis, mínimo y máximo de las ventanas. La columna "módulo" es
la mediana de `resp_rpm` crudo en toda la captura.

| Captura | Conteo manual | Módulo | FFT | Cruces | RMS filtrada |
|---|---|---|---|---|---|
| Persona sentada ~84 cm (`172200`) | 15.5 | 5 | 13.7 (12.6-18.4) | **15.3** (13.6-19.6) | 0.054 (0.035-0.055) |
| Persona sentada ~80 cm (`161758`) | sin conteo | 17 | 15.6 (**7.1**-16.5) | 15.4 (14.3-17.9) | 0.191 (0.159-0.205) |
| Persona frente a la computadora (`162052`) | sin conteo | 18 | 16.8 (15.2-21.6) | 16.2 (13.8-18.5) | 0.186 (0.093-0.329) |
| Pared, sin nadie (`163155`) | sin persona | 0 | 19.3 (15.3-21.3) | 20.5 (19.2-21.7) | **0.003** (0.0027-0.0031) |

Error contra el conteo manual en la captura `172200`: módulo −10.5 rpm, FFT −1.8 rpm, cruces
−0.2 rpm.

### Observaciones

> Actualización (sección 9): estas observaciones salieron de una sola captura con conteo
> manual. Con cuatro capturas etiquetadas (lenta, normal, rápida), ningún estimador sigue la
> respiración real; la conclusión de que los cruces por cero son el estimador más estable no se
> sostiene. Los parámetros de esta sección también cambiaron (rango 6-35 rpm, pasabanda
> 0.08-0.70 Hz).

- **El valor del módulo no sirve para detectar apnea.** En la captura con conteo manual se
  equivocó por 10 rpm y pasó varios segundos en 0 con la persona respirando.
- **Cruces por cero fue el estimador más estable** en las tres capturas con persona. La FFT falla
  cuando hay dos ritmos de magnitud parecida en la ventana: en `172200` el espectro tiene picos en
  ~13.8 y ~18.3 rpm (la respiración fue irregular, con dos respiraciones profundas a los 18 y 27 s)
  y el pico más alto salta entre ellos. En `161758` las últimas 12 ventanas dieron ~7.5 rpm porque
  una oscilación lenta de la línea base, cerca del borde inferior de la banda (0.1 Hz), superó al
  pico de la respiración, que se ve claramente cada ~4 s en la señal.
- **La histéresis de los cruces es sensible.** Con 0.2 × RMS la mediana en `172200` fue 13.1 rpm
  (se perdían respiraciones pequeñas porque las profundas inflan el RMS); con 0.1 × RMS, 15.0;
  con 0.05 × RMS, 15.3; sin histéresis, 15.7. Se dejó 0.05 × RMS. **Ese valor se eligió con la
  única captura que tiene conteo manual**, así que hace falta validarlo con más capturas.
- **Las rpm por sí solas no distinguen una persona de una pared**: con la pared, el ruido
  filtrado también parece periódico y ambos estimadores dan 15-21 rpm. Lo que sí las separa es el
  RMS: la pared dio 0.003 y las personas entre 0.035 y 0.33, al menos 11 veces más en cualquier
  ventana. El RMS también cambia mucho entre personas o distancias (0.054 a ~84 cm contra ~0.19
  en las otras dos), así que el umbral tiene que quedar bien por debajo de 0.035. Se fija en el
  paso de detección, con más capturas.
- Al final de `172200` hay un salto grande de la fase cruda en el último segundo (probablemente
  un movimiento) que deforma el borde de la señal filtrada; afecta solo a la última ventana.
- Pendiente: más capturas con conteo manual (varias distancias, acostado, respiración lenta y
  pausas de respiración simuladas) para validar los estimadores y fijar los umbrales.

## 9. Validación con capturas etiquetadas (resultado negativo)

Script: `analisis/validar_respiracion.py`. Salidas: `pruebas/resultados/validacion_respiracion/`
(`espectros.png`, `errores.md`).

### Capturas

Persona sentada a ~84 cm, quieta. Conteo manual del segundo 30 al 90 (la de `172200`, de todo el
minuto, porque dura 62 s).

| Captura | Conteo manual | Módulo (mediana en el tramo) |
|---|---|---|
| `2026-10-06_172200_persona_sentada_84cm` | 15.5 | 5 |
| `2026-10-07_222846_normal_84cm` | 14 | 17 |
| `2026-10-07_223440_lenta_84cm` | 8 | 3 |
| `2026-10-07_223850_rapida_84cm` | 31 | 14 |
| `2026-10-07_224314_apnea_84cm` | apnea de 45 a 65 s | ver sección 10 |

### ¿Alguna señal tiene su pico en la frecuencia del conteo?

No. En `espectros.png` (espectro de cada señal en el tramo del conteo, con el conteo marcado):

- **`fase_resp`**: la energía queda entre ~6 y ~28 rpm, con varios picos de altura parecida y casi
  nada por encima de 30 rpm. En la lenta los picos están en ~10, ~15 y ~20 rpm (no en 8); en la
  rápida, en ~6-13 rpm (no en 31). En el tiempo es una señal suave con oscilaciones de 3-4 s
  sin importar el ritmo real. Esto es consistente con la hipótesis: el módulo entrega
  `fase_resp` ya filtrada en una banda alrededor de un ritmo típico y no sigue la respiración
  real fuera de ella. La respiración de 31 rpm no aparece.
- **`fase_total`**: no se comporta como una fase envuelta, sino como un **incremento de fase entre
  tramas**. Oscila alrededor de 0 con picos aislados de ±2-3 rad y casi no tiene saltos de −π a π
  que desenvolver en la lenta y la normal; su variación entre muestras crece con la velocidad de
  la respiración (diferencia mediana 0.10 en la normal, 0.64 en la rápida).
  - Las muestras en 0.000000 exacto (0.27-0.44 por segundo) caen **siempre** en la trama que sigue
    al intervalo corto (~45 ms) de cada par de tramas; se descartan como tramas sin dato.
  - **Desenvolverla (`unwrap`) la empeora**: en la rápida, el unwrap interpreta los picos de ruido
    como vueltas completas y fabrica escalones de hasta −25 rad. Su espectro es ruido de banda
    ancha.
  - **Integrarla** (mediana de 3 muestras + suma acumulada) da una señal parecida a `fase_resp` en
    la normal y la lenta, lo que apoya que `fase_resp` es esa misma señal filtrada. Su espectro
    concentra la energía en frecuencias bajas y tampoco tiene pico en el conteo.

### Errores

Estimador de `estimar_respiracion.py` con los mismos parámetros para las cuatro capturas: rango
6-35 rpm, pasabanda 0.08-0.70 Hz, ventana de 30 s, verificación de subarmónico (umbral 0.5),
autocorrelación y cruces por cero (histéresis 0.05 × RMS). Mediana de las ventanas dentro del
tramo del conteo. Entre paréntesis, el error en rpm.

| Captura | Conteo | Módulo | `fase_resp` FFT | `fase_resp` autocorr. | `fase_resp` cruces | `fase_total` desenv. FFT | `fase_total` integr. autocorr. | `fase_total` integr. cruces |
|---|---|---|---|---|---|---|---|---|
| persona_sentada_84cm | 15.5 | 5.0 (−10.5) | 7.6 (−7.9) | 6.6 (−8.9) | 14.5 (−1.0) | 13.9 (−1.6) | 6.3 (−9.2) | 16.0 (+0.5) |
| normal | 14 | 17.0 (+3.0) | 11.8 (−2.2) | 20.4 (+6.4) | 19.7 (+5.7) | 11.3 (−2.7) | 7.2 (−6.8) | 15.2 (+1.2) |
| lenta | 8 | 3.0 (−5.0) | 10.3 (+2.3) | 10.4 (+2.4) | 16.6 (+8.6) | 6.8 (−1.2) | 7.6 (−0.4) | 16.8 (+8.8) |
| rápida | 31 | 14.0 (−17.0) | 10.7 (−20.3) | 9.8 (−21.2) | 12.8 (−18.2) | 10.9 (−20.1) | 6.8 (−24.2) | 12.6 (−18.4) |
| **Error absoluto medio** | | **8.9** | **8.2** | **9.7** | **8.4** | **6.4** | **10.2** | **7.3** |

La tabla completa (las 9 combinaciones de señal y estimador) está en `errores.md`. Ninguna
combinación queda cerca del conteo en las cuatro capturas a la vez: el mejor error medio es
6.4 rpm, apenas mejor que el módulo (8.9), y todas fallan la rápida por 14-24 rpm. Las que
aciertan en una captura fallan en otra (por ejemplo, `fase_total` integrada con cruces: +0.5,
+1.2, +8.8, −18.4), así que no hay un parámetro que ajustar: la información de frecuencia no está
en las señales.

Sobre los armónicos: la verificación de subarmónico y la autocorrelación funcionan con señales
sintéticas (pruebas con un armónico al doble de amplitud que la fundamental), pero con los datos
reales no ayudan porque el problema no es un armónico sino que no hay pico en la frecuencia real.
La verificación de subarmónico incluso empeoró la FFT de `fase_resp` en `172200` (de 13.7 a
7.6 rpm), porque encontró un pico de ruido a la mitad de la frecuencia.

El RMS de las señales sí crece con el ritmo (`fase_resp`: 0.12 en la lenta, 0.09 en la normal,
0.24 en la rápida; `fase_total` integrada: 1.4, 0.8 y 4.1), pero no de forma monótona ni
calibrable con cuatro capturas.

### Posibles causas y siguiente paso

No se pueden distinguir con estos datos; quedan como hipótesis:

1. `fase_resp` está filtrada por el módulo (lo indica el espectro) y no sirve para ritmos fuera de
   ~6-28 rpm.
2. `fase_total` llega a ~16 tramas/s; si es un incremento de fase, con respiración rápida la fase
   puede cambiar más de π entre tramas y el incremento se envuelve (aliasing), lo que explicaría
   que el ruido crezca con la velocidad.
3. Posición: persona sentada a ~84 cm. La aplicación de Seeed para este sensor es el monitoreo de
   sueño; el pecho de una persona sentada se mueve menos de frente al radar y el movimiento del
   cuerpo pesa más.

Siguiente prueba sugerida: persona acostada, radar a 40-60 cm del pecho, respiración marcada con
metrónomo (8, 15 y 30 rpm) durante 2 minutos cada una, y apneas de 20-30 s. Si con eso tampoco hay
pico en la frecuencia del metrónomo, la estimación propia de frecuencia queda descartada con este
módulo y el sistema debe apoyarse en otra señal para la apnea.

## 10. Detección de apnea por caída de amplitud (resultado negativo)

Script: `analisis/detectar_apnea.py`. Salidas: `pruebas/resultados/deteccion_apnea/`
(`apnea.png`, `relacion.png`, `deteccion.md`).

### Detector

No estima la frecuencia. Una vez por segundo compara el RMS de los últimos 5 s de `fase_resp`
filtrada (pasabanda **causal**, como correría en el ESP32) con una referencia: la mediana del RMS
de 5 s de los últimos 45 s de respiración normal. Si con presencia el RMS queda por debajo de
0.5 × referencia durante 10 s seguidos, da la alarma.

- La referencia se congela mientras el RMS está bajo, para que la apnea no la contamine.
- Solo se arma con al menos 30 s de historia y con referencia ≥ 0.01 (la pared dio 0.003 y las
  personas al menos 0.035): un objeto quieto nunca "respiró", así que no puede dejar de hacerlo.
- Parámetros fijados antes de ver los resultados: 5 s (pedido), 45 s (dentro de 30-60 s), 0.5,
  10 s (definición clínica de apnea).

Retraso mínimo del diseño con una señal ideal (amplitud constante que cae a 0 de golpe), a
15 rpm: ~0.7 s de retardo del filtro causal + ~3.75 s hasta que el RMS de 5 s baja de la mitad
(hace falta que el 75 % de la ventana ya no tenga respiración) + 10 s de duración mínima =
**~14.5 s**. A 8 rpm el retardo del filtro sube a ~1.8 s.

### Resultado

| Captura | Alarmas | Resultado |
|---|---|---|
| apnea (45-65 s) | ninguna | **apnea no detectada** |
| persona_sentada_84cm | 45 s | 1 falsa alarma |
| normal | ninguna | sin falsas alarmas |
| lenta | 51 s, 86 s | 2 falsas alarmas |
| rápida | ninguna | sin falsas alarmas |
| pared | ninguna | sin falsas alarmas (nunca se arma) |
| persona_sentada_80cm | ninguna | sin falsas alarmas |
| persona_frente_computadora_50cm | 46 s | 1 falsa alarma |

Con la rejilla de umbrales 0.3-0.6 y duraciones de 5, 10 y 15 s (`deteccion.md`), **ninguna
combinación detecta la apnea**, y las más sensibles dan hasta 6 falsas alarmas en total. El
retraso de detección no se puede medir porque no hubo detección.

### Por qué falla

En `apnea.png` se ve que el RMS de `fase_resp` **no baja durante la apnea**: entre 45-48 s y
58-63 s está entre los valores más altos de la captura (~0.25, el doble de la referencia), y
entre ~51 y 58 s baja a ~0.06-0.09, que apenas roza el umbral (0.06) alrededor de los 57 s. El mínimo de
toda la captura (0.04) está a los 75 s, con la persona ya respirando. En `fase_total` integrada hay
una excursión grande entre 56 y 60 s, también dentro de la apnea. El `resp_rpm` del módulo marca
15-25 rpm durante toda la apnea.

En las capturas sin apnea, la relación RMS corto / referencia baja de 0.5 durante tramos largos de
respiración normal (en la lenta, ~10 s seguidos entre 75 y 86 s), lo que produce las falsas
alarmas. Es decir, la amplitud de `fase_resp` en ventanas de 5 s varía tanto con la respiración
normal como con la apnea, y no las distingue.

Observación sin confirmar: el RMS de `fase_latido` bajó de 0.27 (0-45 s) a 0.16 (45-65 s) durante
la apnea, pero en la respiración normal también pasó por valores de ~0.13 (10-20 s). Con una sola
captura de apnea no se puede saber si sirve.

### Conclusión

Con las capturas actuales (persona sentada a ~84 cm), ninguna señal del radar permite estimar la
frecuencia respiratoria en 8-31 rpm ni detectar una apnea de 20 s por caída de amplitud. Antes de
seguir con el firmware de detección hay que repetir las capturas en la posición de uso real
(acostado, radar sobre el pecho) con la prueba sugerida en la sección 9. Si el resultado se
mantiene, la detección de apnea por radar no es viable con este módulo y hay que replantear ese
requisito del proyecto (por ejemplo, limitar el radar a presencia y apoyarse en el botón).
