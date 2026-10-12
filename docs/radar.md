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
| `0x0A17` | Posición del objetivo (2 float, m); ver sección 12 | No |
| `0x0A29` | Estado de 2 bytes (1/2); ver sección 12 | No |

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

> Actualización: las pruebas acostado se hicieron y el resultado se mantuvo (sección 11).

## 11. Persona acostada: frecuencia, apnea y cama vacía (resultados preliminares, en evaluación)

> Estado: **en evaluación**. Estos resultados salen de 2 apneas acostado y de un solo tipo de
> detector. Antes de concluir se revisa la lectura del radar, el montaje recomendado por Seeed y
> otros detectores (sección 12).

Scripts: `analisis/validar_respiracion.py` y `analisis/detectar_apnea.py`, con los mismos
parámetros para todas las capturas. Salidas: `pruebas/resultados/validacion_respiracion/`
(`espectros.png`, `variable.png`, `errores.md`) y `pruebas/resultados/deteccion_apnea/`
(`apnea_acostado1.png`, `apnea_acostado2.png`, `actividad.png`, `deteccion.md`).

### Capturas

Persona acostada boca arriba, radar a ~50 cm apuntando al pecho, respiración guiada con un pacer de
audio. Los primeros 30 s de cada captura son de acomodo y no cuentan.

| Captura | Contenido |
|---|---|
| `2026-10-08_235538_acostado_8rpm_50cm` | pacer a 8 rpm desde los 30 s (150 s) |
| `2026-10-08_235002_acostado_15rpm_50cm` | pacer a 15 rpm desde los 30 s (147 s) |
| `2026-10-09_001141_acostado_30rpm_50cm` | pacer a 30 rpm desde los 30 s (150 s) |
| `2026-10-09_000001_acostado_variable_rapido_a_lento_50cm` | sin pacer; 7 respiraciones en 0-30 s, luego 30 en 2 min bajando de rápido a lento |
| `2026-10-09_001540_acostado_apnea1_50cm` | apnea completa de 45 a 75 s, sin moverse |
| `2026-10-09_001918_acostado_apnea2_50cm` | igual; el radar perdió a la persona desde los ~96 s (presencia 0) |
| `2026-10-09_002233_cama_vacia_50cm` | nadie en el cuarto; solo almohada, cobijas y colchón |

### Frecuencia

Mediana de las ventanas de 30 s desde los 30 s hasta el final; referencia = pacer. Entre
paréntesis, el error en rpm (tabla completa en `errores.md`).

| Pacer | Módulo | `fase_resp` FFT | `fase_resp` cruces | `fase_total` integrada cruces |
|---|---|---|---|---|
| 8 | 8.0 (0.0) | 9.1 (+1.1) | 13.7 (+5.7) | 11.2 (+3.2) |
| 15 | 16.0 (+1.0) | 12.9 (−2.1) | 15.7 (+0.7) | 12.1 (−2.9) |
| 30 | 12.0 (−18.0) | 10.4 (−19.6) | 18.2 (−11.8) | 15.7 (−14.3) |
| **Error medio acostado** | **6.3** | **7.6** | **6.1** | **6.8** |
| Error medio sentado (sección 9) | 8.9 | 8.2 | 8.4 | 7.3 |

- Acostado los errores bajan un poco, pero **ningún método sigue los 30 rpm** (errores de 10 a
  24 rpm en todas las combinaciones) y en los espectros ninguna señal tiene un pico dominante en
  la frecuencia del pacer: en 8 rpm, `fase_resp` tiene un pico en 8 pero son más altos los de 5 y
  10; en 15 rpm los picos están en 12.5 y 19; en 30 rpm hay un pico en ~28.5 (60 % del máximo) y
  el máximo está en 15.5.
- La mediana del `resp_rpm` del módulo coincidió con el pacer a 8 y 15 rpm, pero muestra a muestra
  oscila entre 0 y ~25 rpm (figuras de las apneas) y a 30 rpm se queda en 12.
- **Ritmo variable (`variable.png`)**: ninguna estimación muestra la bajada de forma clara. Las
  pendientes entre los 30 s y el final van de +0.9 a −6.2 rpm/min; la más negativa es la del
  módulo y sale casi toda de una meseta de 23 rpm entre 83 y 99 s.

### Qué cambia durante la apnea

En `apnea_acostado1.png` y `apnea_acostado2.png` (con el intervalo 45-75 s marcado):

| Señal | Durante la apnea | ¿Cambio claro? |
|---|---|---|
| `fase_resp` y su RMS de 5 s | en apnea1 el RMS sube (0.036 en 30-45 s, 0.069 en 45-60 s); en apnea2 baja poco y tiene picos | No |
| **Actividad** (mediana de \|`fase_total`\| en 5 s) | baja a la mitad: apnea1 de 0.135 a 0.06-0.07; apnea2 de 0.128 a 0.054-0.065 (después de un pico al empezar la apnea) | **Sí, en las dos** |
| `fase_total` integrada | baja en la segunda mitad (apnea1: RMS 0.93 → 0.15 en 60-75 s) | Parcial |
| `fase_latido` | baja moderadamente (RMS 0.19-0.26 → 0.11-0.20) | Débil |
| `resp_rpm` del módulo | sigue marcando valores > 0 buena parte de la apnea y llega a 0-5 rpm hacia el final; en respiración normal también cae a 0-2 | No |
| `latido_bpm` del módulo | cambia (sube hasta 120 al empezar apnea2), pero varía igual en respiración normal | No |
| Presencia, distancia | sin cambio (presencia 100 %; la distancia salta entre valores fijos todo el tiempo) | No |
| Tramas por segundo | sin cambio (16.0-16.3) | No |

La caída de actividad también aparece en la apnea sentado de la sección 10. El problema es que
**caídas parecidas ocurren en la respiración normal**, sobre todo lenta o tranquila (`actividad.png`).

### Detector y validación cruzada

Detector de la sección 10 con dos medidas: el RMS de `fase_resp` (original) y la actividad. Una
alarma solo cuenta como verdadera si la caída que la dispara empezó dentro de la apnea. Umbral y
duración se eligen en una rejilla (umbral 0.3-0.7 × referencia, duración 5-20 s) con una apnea y
se evalúan en la otra. Falsas alarmas en 12 capturas sin apnea (4 acostado, 6 sentado, cama vacía
y pared; 12.5 min con persona después de los 30 s).

| Medida | Ajuste con | Parámetros | Apnea de prueba | Apnea sentado | Falsas alarmas | Segundos en alarma |
|---|---|---|---|---|---|---|
| RMS `fase_resp` | apnea1 | 0.5, 15 s | apnea2: **no detecta** | no detecta | 4 | 90 |
| RMS `fase_resp` | apnea2 | 0.6, 5 s | apnea1: **no detecta** (2 falsas) | no detecta | 16 | 250 |
| Actividad | apnea1 | 0.4, 5 s | apnea2: **no detecta** | detecta a los 56 s (+11 s) | 4 | 123 |
| Actividad | apnea2 | 0.6, 5 s | apnea1: **detecta a los 55 s (+10 s)** (1 falsa) | detecta a los 55 s (+10 s) | 10 | 168 |

- **El RMS de `fase_resp` no sirve**: no detecta la apnea de prueba en ninguna dirección.
- **La actividad detecta en una dirección** (umbral 0.6) con ~10 s de retraso desde el inicio de
  la apnea, pero con 10 falsas alarmas en 12.5 min de respiración normal (una cada ~75 s) y 22 %
  del tiempo en alarma. Con umbral 0.4 bajan a 4 falsas, pero no detecta apnea2.
- Hay además un defecto de diseño que estas capturas dejan ver: la referencia se congela mientras
  la medida está baja. En `acostado_30rpm` la referencia se formó con el movimiento del acomodo y
  la respiración tranquila posterior quedó por debajo del umbral: una sola alarma que dura 105-115
  de los 120 s de la captura. Se puede corregir (por ejemplo, volver a aprender la referencia
  después de cierto tiempo en alarma), pero eso agrega parámetros que no se pueden validar con
  dos apneas.

### Cama vacía

| Escena | Presencia del radar | Distancia | Tramas/s | RMS `fase_resp` | Actividad | resp/latido del módulo |
|---|---|---|---|---|---|---|
| Cama vacía (nadie en el cuarto) | **100 %** | 80 cm | 15.66 | 0.002 | 0.004 | 0 / 0 |
| Pared, sin nadie | **100 %** | 132 cm | 16.06 | 0.003 | 0.017 | 0 / 108 congelado |
| Personas (13 capturas, sentado y acostado) | 85-100 % | 52-80 cm | 14.3-16.3 | 0.039-0.236 | 0.042-0.689 | variable |

Valores después de los primeros 30 s (tabla completa en `deteccion.md`).

- **La presencia del radar no distingue una cama vacía**: con almohada, cobijas y colchón reporta
  presencia todo el tiempo y una distancia fija.
- **La tasa de tramas tampoco**: baja a ~8.5-8.7 por segundo solo cuando el radar no tiene ningún
  objetivo (primer diagnóstico, y apnea2 desde los ~96 s, cuando la presencia pasó a 0), no
  cuando la cama está vacía. La hipótesis se confirma en parte: la tasa indica si el radar tiene
  un objetivo, no si hay una persona, y da la misma información que la bandera de presencia.
- **La actividad sí la distingue**: 0.004 en la cama vacía y 0.017 en la pared, contra 0.042 o
  más con una persona, es decir, al menos 2.5 veces más que la pared y 10 veces más que la cama
  vacía. El RMS de `fase_resp` también (0.002-0.003 contra 0.039 o más).

### Resultados preliminares

**Observado en todas las capturas que aplican** (sujeto a la revisión de la lectura del radar y
del montaje, sección 12):

1. Ninguna señal del radar sigue la frecuencia respiratoria en todo el rango 8-30 rpm, ni sentado
   ni acostado. A 30 rpm todos los métodos fallan por 10 rpm o más.
2. El `resp_rpm` del módulo no sirve para detectar apnea: sigue marcando respiración durante buena
   parte de las tres apneas y cae a 0-2 rpm en tramos de respiración normal.
3. La bandera de presencia y la tasa de tramas no distinguen una cama vacía (ni una pared) de una
   persona; la actividad y el RMS de `fase_resp` sí, con al menos 2.5 veces de margen en 15
   capturas (13 con persona y 2 sin persona).

**Tendencias con pocas capturas** (no se pueden afirmar como resultado):

4. La actividad baja a ~la mitad durante la apnea (3 de 3 apneas), pero caídas parecidas ocurren
   en respiración normal; con dos apneas acostado, ningún umbral detecta las dos sin una falsa
   alarma cada 1-3 minutos.
5. Acostado, la mediana del módulo coincidió con el pacer a 8 y 15 rpm (una captura cada uno).

**Estado:** con el detector probado y estas capturas, la detección de apnea no alcanza la
confiabilidad necesaria: la mejor variante detectaría una apnea con ~10 s de retraso, pero con
del orden de una falsa alarma por cada uno o dos minutos de respiración normal. Es un resultado
preliminar (2 apneas, un tipo de detector); la decisión queda pendiente de la sección 12.

### Recomendación provisional para el firmware de la base

1. **Mientras la detección de apnea esté en evaluación, el radar no dispara alarmas de apnea.**
   La alarma confiable del sistema es el botón del colgante.
2. **El radar sí reporta "persona en cama" / "cama vacía"** con la actividad: mediana de
   |`fase_total`| (descartando las tramas en 0) en una ventana de 30 s. Umbral inicial **0.03**
   (cama vacía 0.004, pared 0.017, personas ≥ 0.042); hay que confirmarlo con más capturas de cama
   vacía y de persona dormida. No usar la bandera de presencia del radar para esto. Retraso
   esperado: el de la ventana (~30 s), suficiente para un estado que cambia en minutos.
3. **Se siguen enviando a Firebase** `resp_rpm`, `latido_bpm`, la distancia y la actividad como
   datos informativos para el panel, marcados como "estimación del módulo, no validada". El panel
   no debe presentarlos como medición clínica.
4. **"Posible pausa respiratoria" queda como función experimental desactivada por defecto.** Si
   se implementa (actividad, umbral 0.6 × referencia, 5 s), solo registra un evento de baja
   prioridad para revisión, nunca una alarma, y se documenta la tasa de falsas alarmas medida aquí.
5. Si hay "radar sin datos" (sección 7) o la tasa cae a ~8.5 tramas/s, el radar no está viendo
   nada: se informa como falla del radar, no como apnea ni como cama vacía.

> Actualización (sección 13): en una noche real el radar pasó a ~8.6 tramas/s (sin objetivo)
> durante casi la mitad del sueño con la persona en la cama. La tasa baja **no** es una falla; la
> falla es "radar sin datos". La recomendación final está en la sección 13.6.

El estado "en evaluación" está registrado en `docs/fallos.md` y en los límites conocidos del
README.

## 12. Revisión: lectura del radar, montaje y detector v2

Firmware de diagnóstico: `firmware/base-radar/src/diagnostico.cpp` (entorno `diagnostico`).
Registros: `pruebas/crudos/2026-10-09_diagnostico_radar_pared.txt` y
`..._persona_sentada.txt`. Detector: `analisis/detector_apnea.py` y
`analisis/evaluar_detector_apnea.py`; resultados en `pruebas/resultados/deteccion_apnea_v2/`.

### 12.1 ¿Leemos bien el radar? Sí

| Verificación | Resultado |
|---|---|
| Formato de la trama de fases | El manual de Seeed (*Tiny Frame Interface, Breathing and Heartbeat*, rev 2.1) define `0x0A13` como 3 float little-endian en este orden: fase total, fase de respiración, fase de latido. Es lo que decodifica la librería. La decodificación con `memcpy` y una manual byte a byte en little-endian dieron los mismos valores en todas las tramas volcadas. (El manual pone `LEN = 00 04` en el ejemplo; las tramas reales traen `00 0C`, 12 bytes.) |
| Checksums | 0 errores de datos; 0-3 errores de cabecera en 60-150 s (bytes sueltos al resincronizar). |
| Tramas perdidas | En crudo llegan 16 tramas de fase por segundo; en la misma escena `radar.cpp` procesó 486 tramas de fase en 30 s (16.2/s) y entregó exactamente 486 lecturas. El buffer de la UART nunca pasó de 132 bytes ocupados (de 32 KB). |
| Ceros exactos en `fase_total` | Vienen del radar: la trama trae los bytes `00 00 00 00` con checksum válido. Casi siempre llegan ~46 ms después de la trama de fase anterior (el intervalo corto del par), y con la pared van seguidos, ~77 ms después, de un salto de ±2.7-2.9 rad. Con persona: 36 de 2431 tramas (1.5 %). |
| Picos de ±2-3 rad | También vienen del radar (bytes de la trama). Con persona aparecen además fuera del par cero/salto: 93 de 2431 tramas. |
| Versión del firmware del radar | Respondió a la consulta `0xFFFF` (sin datos) con `04 01 06 0C`: proyecto 4, **v1.6.12**, la última que publica Seeed. No hace falta actualizar. |

**No había errores de lectura.** Las conclusiones de las secciones 9 a 11 no cambian por la
lectura. Lo que sí las limita es el montaje y el tiempo de establecimiento (12.3).

### 12.2 Las tramas del radar

Con objetivo (persona o pared) llegan unas 74 tramas por segundo, de 9 tipos; sin objetivo, 7 tipos
a ~8.7/s cada uno.

| Tipo | Contenido | Tasa con objetivo | Fuente |
|---|---|---|---|
| `0x0A13` | Fase total, de respiración y de latido (3 float) | 16/s, en pares (~45 y ~76 ms) | Manual de Seeed y librería |
| `0x0A14` | Respiración (float, rpm) | ~8/s | Manual y librería |
| `0x0A15` | Latido (float, bpm) | 1-7/s (variable) | Manual y librería |
| `0x0A16` | Bandera de objetivo (u32) + distancia (float, cm) | ~8/s | Manual y librería |
| `0x0F09` | Presencia (2 bytes; la librería lee el primero) | ~8/s | Librería (no está en el manual) |
| `0x0A04` | Objetivos de la nube de puntos 3D: cantidad (u32) y por objetivo x, y (float, m), índice Doppler y cluster (int32) | 0.3-8/s | Librería |
| `0x0A17` | 2 float: x ≈ 0-0.13 y y ≈ 0.3-0.6 en la pared; por los valores, posición del objetivo en metros | ~8/s | Deducido; no está en la librería ni en el manual |
| `0x0A29` | 2 bytes, valor 1 o 2. Pasa a 2 durante una trama cada ~1.45 s (pared y persona) y se quedó en 2 durante el movimiento y con la persona lejos | ~8/s | Deducido (una prueba) |
| `0x0100` | **Texto ASCII de depuración**: "breath rate = N", "heart rate = N", "invalid = N" | ~9/s | Deducido; probablemente la recolección de datos que agregó el firmware v1.6.10 |

Ninguna trama trae un estado de sueño, de movimiento documentado, de apnea o una energía de la
señal. Los textos de `0x0100` repiten los ritmos (en 60 s de respiración normal: 334 "breath rate"
con valores de 3 a 26 y 66 "heart rate"). Pendiente: el diagnóstico tenía un límite de 400 textos
que se llenó a los 70 s, así que **no se vio qué dice el radar durante la apnea**; queda para la
próxima sesión.

La bandera de presencia (`0x0F09`) se quedó en 1 con la persona lejos, igual que con la cama vacía
(sección 11): confirma que no sirve para saber si hay una persona.

### 12.3 Montaje y uso recomendados por Seeed

De la wiki del kit y de la hoja de datos del MR60BHA2 (*Technical Specification*, beta, 2024-03-05):

- **Solo para sueño**: "recommended for use solely during sleep scenarios. Please refrain from
  utilising the radar's respiratory and heart rate functions whilst seated at a desk or during
  exercise".
- **Montaje inclinado para sueño**: a **1 m de altura, directamente sobre la cabecera de la cama,
  inclinado 45° hacia el centro de la cama**, con el pecho a **1.5 m o menos**; la normal del radar
  apuntando a la zona del pecho.
- Montaje lateral (alternativa): a la altura del pecho, a 1.5 m o menos.
- Distancia al pecho para respiración y latido: 0.4 a 1.5 m.
- **Tiempo para establecer la detección: 1 minuto.** "Respiratory and heart rate are weak reflection
  signals, radar signal processing requires a period of data accumulation".
- Medir en reposo; con movimientos grandes la medición se detiene. Las fallas esporádicas son
  normales.
- Evitar varios radares juntos, cortinas que se mueven, ventiladores, superficies metálicas o
  espejos grandes, y medir a través de vidrio o madera.
- Firmware: v1.6.10 "optimized breathing mechanics to resolve the issue where breath hold duration
  does not decrease"; v1.6.12 "fixed target loss when stationary within 1.5 meters". El nuestro
  ya es v1.6.12.

**Comparación con lo que hicimos:**

| Recomendado | Nuestras pruebas |
|---|---|
| Solo sueño, acostado | Sentado (secciones 8-10) y acostado (sección 11) |
| 1 m sobre la cabecera, 45° hacia el centro de la cama | Radar a ~50 cm del pecho, de frente o de lado, colocado a ojo |
| Esperar ~1 min a que se establezca la detección | Apneas a los 45 s del inicio; los primeros 30 s de acomodo se usaron para formar la referencia |
| Sin movimiento | Acomodo al inicio; persona sentada |

Las secciones 8 a 11 se hicieron fuera del montaje y del uso recomendados. Sus resultados valen
para esas condiciones; **no prueban** que el radar no sirva en el montaje recomendado.

### 12.4 Detector v2

Cambios respecto al de las secciones 10 y 11 (fijados antes de ver resultados):

- Medidas por segundo: actividad (mediana de |`fase_total`|), RMS de `fase_resp`, proporción de
  energía de la fase integrada en 0.1-0.6 Hz frente a 0.05-3 Hz, y como apoyo opcional que el
  `resp_rpm` del módulo sea ≤ 8. Ventanas de 5 o 10 s.
- Solo se arma tras 30 s de respiración clara (actividad de la historia ≥ 0.03; con la pared y la
  cama vacía nunca se arma). La proporción de banda no sirve para esto: la pared da 0.40, igual
  que una persona.
- Duración mínima de la caída: 20 s. La referencia (mediana de los últimos 60 s normales) se
  reaprende si la medida lleva 60 s baja.

**Criterio de éxito, fijado de antemano:** detectar en ≤ 30 s desde el inicio de la apnea, con ≤ 1
falsa alarma cada 10 min de respiración normal.

**Validación cruzada** (se eligen los parámetros con dos apneas y se prueba con la tercera; las
falsas alarmas se cuentan en las 12 capturas sin apnea, 12.5 min con persona, que no se usan para
elegir):

| Apnea de prueba | Configuración elegida | Detección de la apnea de prueba | Falsas alarmas | Por 10 min | ¿Cumple? |
|---|---|---|---|---|---|
| acostado1 | actividad, 10 s, umbral 0.7 | a los 26 s | 4 | 3.2 | no |
| acostado2 | actividad, 10 s, umbral 0.7 | a los 29 s | 4 | 3.2 | no |
| sentado | actividad, 5 s, umbral 0.8 | no detecta | 4 | 3.2 | no |

**Cota optimista** (eligiendo viendo las tres apneas): la única configuración que detecta las
tres a tiempo (actividad, 10 s, 0.7: a los 26, 29 y 26 s) da 3.2 falsas alarmas cada 10 min. Con
≤ 1 falsa alarma cada 10 min, lo mejor detecta 1 de 3.

- **El criterio no se cumple con estos datos**, ni siquiera en la cota optimista.
- Es una mejora clara sobre el detector anterior: de una falsa alarma cada ~75 s a una cada ~3
  min, y la misma configuración salió elegida en dos de las tres validaciones.
- La actividad fue la mejor medida en todas las combinaciones; el RMS de `fase_resp`, la
  proporción de banda y el apoyo del módulo no mejoraron nada.
- Las 4 falsas alarmas caen en **respiración lenta o tranquila**: captura de 8 rpm (90 y 115 s),
  de 30 rpm (60 s; la referencia se formó con el acomodo) y de ritmo variable (128 s, ya en el
  tramo lento). Al respirar lento, el pecho se mueve despacio y la actividad baja casi como en una
  apnea.
- **Tendencia, no resultado**: 3 apneas y 12.5 min de respiración normal no alcanzan para estimar
  ni la sensibilidad (3 de 3 detectadas es compatible, con 95 % de confianza, con una sensibilidad
  real de apenas ~30 %) ni la tasa de falsas alarmas.

### 12.5 Prueba mínima que falta (una sesión de ~50 min)

Para decidir con datos, en el montaje y uso recomendados:

1. **Montaje**: persona acostada boca arriba; radar en la pared a 1 m sobre la cabecera,
   inclinado 45° hacia el centro de la cama; medir con cinta la distancia al pecho (≤ 1.5 m).
2. **Encender el radar y esperar 3 minutos** antes de empezar (más que el minuto de
   establecimiento).
3. **10 apneas de 25 s**, cada una precedida por 2 minutos de respiración normal (~25 min).
   Con 10 de 10 detectadas, la sensibilidad sería al menos ~70 % con 95 % de confianza; con 3
   apneas no se puede decir nada.
4. **20 minutos de respiración normal sin apneas**, incluidos 5 minutos lentos (pacer a 8 rpm, el
   caso que da falsas alarmas) y dos cambios de postura. Para afirmar ≤ 1 falsa alarma cada 10
   minutos con 95 % de confianza hacen falta al menos ~37 minutos de respiración normal sin
   ninguna falsa alarma; sumando los tramos normales entre apneas (~20 min) y estos 20 minutos se
   llega a ~40.
5. **3 minutos de cama vacía** al final.
6. Captura continua de toda la sesión, con un guion de audio que indique cuándo aguantar la
   respiración y que registre esos instantes en el mismo archivo, para no depender de un
   cronómetro.
7. Con el firmware de diagnóstico sin límite de textos, para ver qué reporta el radar (`0x0100` y
   `0x0A29`) durante las apneas.

El detector y sus parámetros quedan fijos antes de la sesión (actividad, ventana de 10 s, umbral
0.7, 20 s, referencia de 60 s), y la sesión se usa solo para evaluarlos.

## 13. Noche completa de sueño real y decisión para el firmware

Captura: `pruebas/crudos/2026-10-12_024855_noche_cama.csv` (7 h 45 min, 324 443 filas). Script:
`analisis/analizar_noche.py`; resultados en `pruebas/resultados/noche_2026-10-12_024855_noche_cama/`
(`resumen.md` con todas las tablas, `noche.png`, `alarmas.png`, `respiracion.png`). Ningún
parámetro se ajustó con esta noche: todos vienen de las secciones 11 y 12, y las variantes del
detector (25 y 30 s) se fijaron antes de correrlo.

**Condiciones**: radar más alto que la cama, inclinado hacia el pecho (parecido al montaje de
Seeed, sin medir). Durante la noche se movió y terminó apuntando más al frente (no se sabe cuándo).
Sin ventilador. Una persona que durmió casi toda la noche boca abajo y se dio vueltas. Se asume que
no hubo apneas: toda alarma durante el sueño cuenta como falsa.

### 13.1 Vista de la noche (`noche.png`)

- **La captura tiene 7 huecos** sin filas (1 059 s en total, todos después de las 6 h; el mayor de
  473 s). `ms` sigue creciendo, así que la base no se reinició: se perdió el registro en la
  computadora o el USB, no el radar. Se excluyen de todo el análisis y el detector corre por
  separado en cada tramo continuo.
- **El radar pierde el objetivo durante tramos largos con la persona dormida**: presencia 0 y
  ~8.6 tramas/s (sin tramas de fase con dato), incluido un tramo de ~1.6 h seguidas (de 3.7 a
  5.3 h). Es el 48 % del sueño con datos (3.69 h con objetivo de 7.03 h).
- **Cambio de nivel de la distancia**: el mayor salto (mediana de 30 min antes y después) está a
  las **3 h 10 min**, de ~40 a ~80 cm. Antes la distancia estuvo en 35-65 cm; después pasó por
  80-140 cm, luego el tramo sin objetivo, 109-115 cm entre 5.3 y 6 h y 75-90 cm hasta el final.
  Es compatible con que el sensor se movió alrededor de las 3.2 h, pero un cambio de postura
  también cambia la distancia: **no se puede confirmar cuándo se movió**.
- Antes y después de las 3.2 h la actividad mediana con objetivo es la misma (0.049 y 0.048),
  pero los minutos con objetivo y actividad < 0.03 pasan de 11 % a 20 %, y los minutos sin objetivo
  de 40 % a 53 %: la señal empeora después del cambio (tendencia).
- `resp_rpm` y `latido_bpm` del módulo pasan la mayor parte del tiempo en 0, también con objetivo
  (13.5).

### 13.2 Segmentos

| Segmento | Tramo | Criterio |
|---|---|---|
| De pie (preparación) | 0:00-0:04:09 | antes del primer evento anotado |
| Vacío | 0:04:09-0:07:51 | lectura ~3100 (sale) a ~5800 (vuelve) |
| De pie, a ~1.5 m a un costado | 0:07:51-0:11:15 | lectura ~5800 a ~7800 |
| Vacío | 0:11:15-0:13:10 | lectura ~7800 a ~9600 |
| De pie (vuelve) | 0:13:10-0:13:29 | lectura ~9600 a ~9900 |
| En cama despierta | 0:13:29-0:24:00 | de "se acuesta" al inicio del sueño |
| Dormida | 0:24:00-7:44:00 | 440 min (7.03 h con datos) |
| En cama despierta | último minuto | despierta y detiene la captura |

- Los eventos se pasan de número de lectura (fila) a tiempo con la columna `ms`.
- **Inicio del sueño**: primer tramo de 10 minutos seguidos, después de acostarse, sin ningún minuto
  con actividad mediana > 0.1 (los minutos sin objetivo no cuentan como movimiento). 0.1 está por
  encima de la respiración tranquila acostado (0.04-0.07, sección 11) y por debajo del movimiento
  de acomodarse (0.2-0.3). Da las 0:24, 10.5 min después de acostarse ("unos minutos" con el
  celular). Es una estimación: no hay registro del momento real en que se durmió.
- Como los eventos son aproximados, para evaluar se descartan ±30 s alrededor de cada cambio.

Las señales coinciden con los eventos: actividad de cuarto vacío (0.005) entre la primera salida y
el regreso, y actividad de 0.1-0.3 al acostarse. Con la persona de pie a un costado, el radar no
la vio la mayor parte del tiempo (estaba fuera del cono).

### 13.3 Criterio de cama vacía (actividad de 30 s ≥ 0.03)

Por segundo; sin objetivo del radar (sin tramas de fase con dato) cuenta como "sin persona".

| Segmento | Segundos | Aciertos | Persona y el radar sin objetivo | Persona con objetivo y actividad < 0.03 |
|---|---|---|---|---|
| Vacío | 216 | **94.9 %** | – | – |
| De pie | 334 | 31.7 % | 67.7 % | 0.6 % |
| En cama despierta | 580 | 56.9 % | 43.1 % | 0 % |
| **Dormida** | 25 136 | **45.5 %** | 43.2 % | 11.3 % |

- **Sin nadie, el criterio acierta** (95 %, igual que en la sección 11). Con este montaje no da
  falsos "persona".
- **Con la persona dormida falla más de la mitad del tiempo.** El 43 % es porque el radar no tiene
  objetivo (el criterio no puede hacer nada: no hay fase) y el 11 % porque la actividad con
  objetivo queda por debajo de 0.03 (por ejemplo, de 0.85 a 1.0 h, actividad ~0.012 con presencia
  1: entre la cama vacía, 0.004, y la pared, 0.017).
- Es decir: "actividad ≥ 0.03" significa "hay una persona"; lo contrario **no** significa "cama
  vacía".

### 13.4 Falsas alarmas del detector de apnea durante el sueño (`alarmas.png`)

Detector v2 sin cambios (actividad, ventana 10 s, umbral 0.7, referencia 60 s). Contexto de cada
alarma, fijado antes de verlas, mirando desde 60 s antes del inicio de la caída: **sin objetivo**
(hubo segundos sin dato), **movimiento** (la actividad pasó de 2 × la referencia) o **quieto**.

| Duración mínima | Falsas alarmas | **Por hora de sueño** (IC 95 %) | Por hora con objetivo | Movimiento | Quieto | Sin objetivo |
|---|---|---|---|---|---|---|
| 20 s (v2) | 43 | **6.1** (4.4-8.2) | 11.7 | 4 | 24 | 15 |
| 25 s | 36 | **5.1** (3.6-7.1) | 9.8 | 4 | 18 | 14 |
| 30 s | 35 | **5.0** (3.5-6.9) | 9.5 | 4 | 17 | 14 |

Las 3 apneas de las capturas anteriores (retraso desde el inicio de la apnea; criterio ≤ 30 s) y
falsas alarmas en las 12 capturas cortas sin apnea:

| Duración mínima | acostado1 | acostado2 | sentado | A tiempo | Falsas en capturas cortas |
|---|---|---|---|---|---|
| 20 s | +26 s | +29 s | +26 s | **3/3** | 4 (3.2 cada 10 min) |
| 25 s | +31 s | +34 s | no detecta | 0/3 | 3 (2.4) |
| 30 s | +36 s | no detecta | no detecta | 0/3 | 2 (1.6) |

- **Ninguna variante es usable como alarma**: entre 5 y 6 falsas alarmas por hora de sueño, de 10 a
  12 por hora en que el radar ve a la persona (≈ 2 cada 10 min, el doble del criterio de la
  sección 12.4). Una noche de 7 h daría 35-43 avisos falsos.
- **Alargar la duración casi no ayuda y pierde las apneas**: de 20 a 30 s las falsas alarmas bajan
  solo ~19 % (43 → 35), mientras la detección pasa de 3/3 a tiempo a 0/3 a tiempo (1/3 tarde).
  La duración mínima más el retardo de la ventana dejan las variantes de 25 y 30 s fuera del límite
  de 30 s casi por construcción.
- **Dónde caen** (con 20 s): la mayoría en **tramos quietos** (24), pocas tras un cambio de postura
  (4). Se agrupan en dos rachas: 3:00-3:28 (7 alarmas, alrededor del posible movimiento del
  sensor) y 6:37-6:56 (13 en 20 min, con la actividad oscilando entre 0.01 y 0.1). 15 tienen una
  pérdida de objetivo cerca: el v2 conserva el inicio de la caída mientras no hay dato, así que el
  tiempo sin objetivo cuenta como "caída". Aun quitando esas 15 quedan 28 (4 por hora de sueño,
  IC 95 % 2.6-5.8).

### 13.5 Respiración durante el sueño (`respiracion.png`)

Ventanas de 30 s (sin solaparse) en el sueño con objetivo en todas las tramas: 307 de ~843 (36 %).

| Estimación | Mediana | P10-P90 | En 0 | 10-20 rpm |
|---|---|---|---|---|
| Módulo (`resp_rpm`, mediana por ventana) | **0** | 0-16 | **57 %** | 23 % |
| Propia: FFT de `fase_resp` | 13.7 | 9.2-18.2 | 0 % | 81 % |
| Propia: cruces de `fase_resp` | 15.4 | 12.5-21.0 | 0 % | 83 % |

- **El módulo no da un valor la mayor parte del sueño**: aun con objetivo, el 56 % de las tramas
  de `resp_rpm` están en 0; cuando da un valor, la mediana es 13 rpm (plausible). Por hora, las
  ventanas en 0 van de 17 % a 91 %. No hay registro de la postura por hora, así que no se puede
  atribuir a "boca abajo" con certeza; es consistente con que la persona estuvo boca abajo casi toda
  la noche y con que Seeed solo garantiza la medición en reposo (tendencia).
- **Lo propio da valores plausibles (13.7-15.4 rpm), pero eso no lo valida**: con la pared dio
  15-21 rpm (sección 8) y no siguió el pacer en 8-30 rpm (secciones 9 y 11). Que caiga en 10-20 rpm
  es lo esperable de un filtro con esa banda, haya o no respiración que seguir.
- En el 64 % restante del sueño no hay estimación: el radar no tenía objetivo o lo perdió en algún
  momento de la ventana.

### 13.6 Recomendación final para el firmware de la base

1. **Persona en cama**: actividad = mediana de |`fase_total`| sin las tramas en 0, ventana de 30 s,
   umbral **0.03**. Tres estados para el panel:
   - actividad ≥ 0.03: **"persona detectada"**;
   - radar con datos pero sin objetivo (presencia 0, ~8.6 tramas/s) o actividad < 0.03:
     **"sin persona detectada"**, que **no** es "cama vacía" ni dispara nada;
   - "radar sin datos" (sección 7): falla del radar, aviso de mantenimiento.

   Esperado: con el cuarto vacío, ~95 % de acierto; con una persona dormida en un montaje como
   este, "persona detectada" solo ~45 % del tiempo. La regla 8 de `fallos.md` ("persona fuera de la
   cama") queda como estado informativo, nunca como alarma.
2. **"Posible pausa respiratoria"**: **desactivada por defecto y nunca como alarma.** Si se activa
   para registrar eventos de revisión: actividad, ventana 10 s, umbral 0.7, 20 s, referencia de
   60 s (v2 sin cambios; 25 y 30 s no se usan porque pierden las apneas sin bajar casi las falsas
   alarmas). Su ficha:
   - Falsas alarmas esperadas: **~6 por hora de sueño** en esta noche (~12 por hora con objetivo);
     3.2 cada 10 min en las capturas cortas despiertas.
   - Detección medida: **3 de 3 apneas** de 20-30 s, a los 26-29 s, en capturas cortas
     supervisadas (acostado boca arriba a ~50 cm o sentado). No medida en este montaje ni dormida;
     con 3 apneas la sensibilidad real podría ser de apenas ~30 % (sección 12.4).
   - Al perder el objetivo, el firmware debe reiniciar el conteo de la caída en vez de seguir
     sumando tiempo sin dato. Es un cambio de diseño que sale de esta noche y no se evaluó.
3. **La alarma confiable de apnea sigue siendo el botón del colgante.**
4. `resp_rpm`, `latido_bpm`, la distancia y la actividad se siguen enviando como datos informativos
   ("estimación del módulo, no validada"); el panel debe mostrar "sin dato" cuando el módulo da 0 o
   no hay objetivo, no "0 rpm".

### 13.7 Límites conocidos

- **Posición boca abajo y vueltas**: el radar perdió a la persona el 48 % del sueño, el módulo dio
  0 rpm en el 57 % de las ventanas con objetivo y la actividad quedó bajo 0.03 en el 11 % del
  sueño.
- **Sensor que se mueve**: el cambio de nivel a las ~3.2 h coincide con más tiempo sin objetivo y
  con una racha de falsas alarmas. El radar debe ir fijo (soporte atornillado, no apoyado) y con el
  ángulo medido.
- **Movimiento**: solo 4 de 43 falsas alarmas siguieron a un movimiento; el problema principal son
  los tramos quietos con una actividad que oscila por debajo de la referencia.
- Una sola noche, una persona, montaje sin medir y huecos de captura después de las 6 h.

### 13.8 Qué es demostrado y qué es tendencia

**Demostrado con esta noche** (el conteo alcanza y la conclusión no depende de los detalles):

1. Con este montaje y una persona dormida (casi toda la noche boca abajo), el radar no tuvo
   objetivo durante ~la mitad del sueño. Una actividad baja o la falta de objetivo no prueban que la
   cama esté vacía.
2. Con el cuarto vacío, el criterio de actividad 0.03 no dio falsos "persona" (95 % de acierto,
   coherente con la sección 11).
3. El detector v2 da del orden de 5-6 falsas alarmas por hora de sueño en las tres variantes (35-43
   alarmas; aun el extremo bajo del intervalo de confianza pasa de 3 por hora). No sirve como
   alarma.
4. Las variantes de 25 y 30 s no detectan a tiempo ninguna de las 3 apneas anteriores.

**Tendencias** (una noche, sin registro de postura ni del momento en que se movió el sensor):

5. El sensor se movió alrededor de las 3.2 h y eso empeoró la señal.
6. La pérdida de objetivo y los 0 del módulo se deben sobre todo a la postura boca abajo.
7. La respiración durante el sueño estuvo en 13-15 rpm (las estimaciones propias son plausibles,
   pero no están validadas).
8. La detección real de apneas durante el sueño no se midió. Para medirla sigue haciendo falta la
   sesión de la sección 12.5, con el radar fijo y el ángulo medido.
