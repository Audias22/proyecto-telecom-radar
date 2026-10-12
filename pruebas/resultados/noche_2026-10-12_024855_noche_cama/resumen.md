# Noche completa: 2026-10-12_024855_noche_cama

Duración 7:45:00 (27900 s), 324443 filas. Huecos de la captura (sin filas > 5 s): 7, 1059 s en total.

| Hueco | Desde | Duración (s) |
|---|---|---|
| 1 | 6:03:05 | 124 |
| 2 | 6:05:41 | 154 |
| 3 | 6:08:28 | 21 |
| 4 | 6:16:17 | 73 |
| 5 | 7:08:28 | 193 |
| 6 | 7:12:59 | 473 |
| 7 | 7:44:38 | 19 |

## Eventos

| Lectura | Evento | Tiempo |
|---|---|---|
| 3100 | sale | 0:04:09 (249 s) |
| 5800 | vuelve (de pie a un costado) | 0:07:51 (471 s) |
| 7800 | sale | 0:11:15 (675 s) |
| 9600 | vuelve | 0:13:10 (790 s) |
| 9900 | se acuesta | 0:13:29 (809 s) |

Mayor cambio de nivel de la distancia (mediana de 30 min antes y después): a las 3:10:00, de 40 a 80 cm.

## Segmentos

| Segmento | Desde | Hasta | Duración | Detalle |
|---|---|---|---|---|
| de pie | 0:00:00 | 0:04:09 | 4.2 min | preparación de la captura |
| vacío | 0:04:09 | 0:07:51 | 3.7 min | sale del cuarto |
| de pie | 0:07:51 | 0:11:15 | 3.4 min | de pie a ~1.5 m, a un costado |
| vacío | 0:11:15 | 0:13:10 | 1.9 min | sale otra vez |
| de pie | 0:13:10 | 0:13:29 | 0.3 min | vuelve |
| en cama despierta | 0:13:29 | 0:24:00 | 10.5 min | acostada, con el celular |
| dormida | 0:24:00 | 7:44:00 | 440.0 min | boca abajo casi toda la noche |
| en cama despierta | 7:44:00 | 7:45:00 | 1.0 min | despierta y detiene la captura |

## Criterio de cama vacía (actividad 30 s ≥ 0.03)

Por segundo, sin los ±30 s alrededor de cada cambio de segmento ni los huecos.

| Segmento | Segundos evaluados | Esperado | Aciertos | Persona sin objetivo del radar | Persona con objetivo y actividad < 0.03 |
|---|---|---|---|---|---|
| vacío | 216 | vacío | **94.9 %** | – | – |
| de pie | 334 | persona | **31.7 %** | 67.7 % | 0.6 % |
| en cama despierta | 580 | persona | **56.9 %** | 43.1 % | 0.0 % |
| dormida | 25136 | persona | **45.5 %** | 43.2 % | 11.3 % |

## Detector de apnea v2 durante el sueño

Actividad, ventana 10 s, umbral 0.7, referencia 60 s; corre en cada tramo continuo de la captura. Sueño con datos: 7.03 h; con objetivo del radar: 3.69 h.

| Duración mínima | Falsas alarmas en el sueño | Por hora de sueño | Por hora con objetivo | Movimiento | Quieto | Sin objetivo | Fuera del sueño |
|---|---|---|---|---|---|---|---|
| 20 s | 43 | **6.12** | 11.66 | 4 | 24 | 15 | 3 |
| 25 s | 36 | **5.12** | 9.76 | 4 | 18 | 14 | 2 |
| 30 s | 35 | **4.98** | 9.49 | 4 | 17 | 14 | 2 |

Alarmas con 20 s (instante; inicio de la caída; contexto): 1:04:37 (1:04:17, quieto); 1:14:38 (1:14:18, sin objetivo); 1:22:00 (1:21:40, sin objetivo); 1:28:39 (1:28:19, movimiento); 1:42:15 (1:41:55, sin objetivo); 2:01:37 (2:01:17, sin objetivo); 2:02:45 (2:02:25, quieto); 2:27:38 (2:27:18, sin objetivo); 2:52:12 (2:51:52, sin objetivo); 3:00:22 (3:00:02, sin objetivo); 3:04:07 (3:03:47, quieto); 3:11:02 (3:10:42, sin objetivo); 3:18:17 (3:17:57, quieto); 3:23:36 (3:23:16, sin objetivo); 3:24:39 (3:24:19, quieto); 3:27:46 (3:27:26, quieto); 5:17:19 (5:16:59, sin objetivo); 5:18:54 (5:18:34, quieto); 5:19:45 (5:19:25, quieto); 5:26:30 (5:26:10, sin objetivo); 5:34:48 (5:34:28, quieto); 5:59:57 (5:59:37, quieto); 6:01:28 (6:01:08, quieto); 6:13:05 (6:12:45, sin objetivo); 6:14:05 (6:13:45, quieto); 6:19:25 (6:19:05, quieto); 6:28:06 (6:27:46, movimiento); 6:37:57 (6:37:37, quieto); 6:38:44 (6:38:24, quieto); 6:41:46 (6:41:26, quieto); 6:43:13 (6:42:53, quieto); 6:44:02 (6:43:42, quieto); 6:45:25 (6:45:05, quieto); 6:48:21 (6:48:01, sin objetivo); 6:49:05 (6:48:45, quieto); 6:51:15 (6:50:55, quieto); 6:52:31 (6:52:11, quieto); 6:54:04 (6:53:44, quieto); 6:54:36 (6:54:16, quieto); 6:55:34 (6:55:14, sin objetivo); 7:01:14 (7:00:54, movimiento); 7:01:57 (7:01:37, movimiento); 7:08:06 (7:07:46, sin objetivo)

Alarmas con 25 s (instante; inicio de la caída; contexto): 1:04:42 (1:04:17, quieto); 1:14:43 (1:14:18, sin objetivo); 1:22:05 (1:21:40, sin objetivo); 1:28:44 (1:28:19, movimiento); 1:42:20 (1:41:55, sin objetivo); 2:01:42 (2:01:17, sin objetivo); 2:02:50 (2:02:25, quieto); 2:27:43 (2:27:18, sin objetivo); 2:52:17 (2:51:52, sin objetivo); 3:00:27 (3:00:02, sin objetivo); 3:04:12 (3:03:47, quieto); 3:18:22 (3:17:57, quieto); 3:23:41 (3:23:16, sin objetivo); 3:24:44 (3:24:19, quieto); 3:27:51 (3:27:26, quieto); 5:17:24 (5:16:59, sin objetivo); 5:18:59 (5:18:34, quieto); 5:26:35 (5:26:10, sin objetivo); 5:34:53 (5:34:28, quieto); 6:00:02 (5:59:37, quieto); 6:01:33 (6:01:08, quieto); 6:13:10 (6:12:45, sin objetivo); 6:19:30 (6:19:05, quieto); 6:28:11 (6:27:46, movimiento); 6:38:02 (6:37:37, quieto); 6:38:49 (6:38:24, quieto); 6:43:18 (6:42:53, quieto); 6:44:07 (6:43:42, quieto); 6:45:30 (6:45:05, quieto); 6:48:26 (6:48:01, sin objetivo); 6:49:10 (6:48:45, quieto); 6:51:20 (6:50:55, quieto); 6:55:39 (6:55:14, sin objetivo); 7:01:19 (7:00:54, movimiento); 7:02:02 (7:01:37, movimiento); 7:08:11 (7:07:46, sin objetivo)

Alarmas con 30 s (instante; inicio de la caída; contexto): 1:04:47 (1:04:17, quieto); 1:14:48 (1:14:18, sin objetivo); 1:22:10 (1:21:40, sin objetivo); 1:28:49 (1:28:19, movimiento); 1:42:25 (1:41:55, sin objetivo); 2:01:47 (2:01:17, sin objetivo); 2:02:55 (2:02:25, quieto); 2:27:48 (2:27:18, sin objetivo); 2:52:22 (2:51:52, sin objetivo); 3:00:32 (3:00:02, sin objetivo); 3:04:17 (3:03:47, quieto); 3:18:27 (3:17:57, quieto); 3:23:46 (3:23:16, sin objetivo); 3:24:49 (3:24:19, quieto); 3:27:56 (3:27:26, quieto); 5:17:29 (5:16:59, sin objetivo); 5:19:04 (5:18:34, quieto); 5:26:40 (5:26:10, sin objetivo); 5:34:58 (5:34:28, quieto); 6:00:07 (5:59:37, quieto); 6:01:38 (6:01:08, quieto); 6:13:15 (6:12:45, sin objetivo); 6:19:35 (6:19:05, quieto); 6:28:16 (6:27:46, movimiento); 6:38:07 (6:37:37, quieto); 6:38:54 (6:38:24, quieto); 6:43:23 (6:42:53, quieto); 6:44:12 (6:43:42, quieto); 6:48:31 (6:48:01, sin objetivo); 6:49:15 (6:48:45, quieto); 6:51:25 (6:50:55, quieto); 6:55:44 (6:55:14, sin objetivo); 7:01:24 (7:00:54, movimiento); 7:02:07 (7:01:37, movimiento); 7:08:16 (7:07:46, sin objetivo)

### Las 3 apneas de las capturas anteriores

| Duración mínima | acostado1 | acostado2 | sentado | Falsas alarmas en las 12 capturas cortas | Por 10 min |
|---|---|---|---|---|---|
| 20 s | +26 s | +29 s | +26 s | 4 | 3.2 |
| 25 s | +31 s | +34 s | no detecta | 3 | 2.4 |
| 30 s | +36 s | no detecta | no detecta | 2 | 1.6 |

## Respiración durante el sueño

Ventanas de 30 s (sin solaparse) con objetivo en todas las tramas: 307 de ~843 (36 % del sueño con datos).

| Estimación | Ventanas | P10 | P25 | Mediana | P75 | P90 | = 0 | 0-6 rpm | 10-20 rpm |
|---|---|---|---|---|---|---|---|---|---|
| Módulo (mediana de resp_rpm) | 307 | 0.0 | 0.0 | **0.0** | 9.0 | 16.0 | 57 % | 13 % | 23 % |
| Propia: FFT de fase_resp | 307 | 9.2 | 12.4 | **13.7** | 15.4 | 18.2 | 0 % | 0 % | 81 % |
| Propia: cruces de fase_resp | 307 | 12.5 | 13.7 | **15.4** | 17.8 | 21.0 | 0 % | 0 % | 83 % |

resp_rpm crudo con presencia en el sueño: 192117 tramas, 56 % en 0, mediana de los valores > 0: 13 rpm.

| Hora | Ventanas con objetivo | Módulo: mediana | Módulo: ventanas en 0 | FFT: mediana | Cruces: mediana |
|---|---|---|---|---|---|
| 0-1 h | 11 | 0.0 | 91 % | 13.7 | 14.2 |
| 1-2 h | 47 | 1.0 | 49 % | 13.5 | 14.4 |
| 2-3 h | 69 | 0.0 | 72 % | 14.9 | 17.0 |
| 3-4 h | 36 | 2.2 | 36 % | 14.3 | 15.3 |
| 5-6 h | 67 | 0.0 | 84 % | 13.2 | 16.2 |
| 6-7 h | 71 | 4.0 | 30 % | 13.3 | 14.6 |
| 7-8 h | 6 | 11.0 | 17 % | 13.1 | 13.5 |
