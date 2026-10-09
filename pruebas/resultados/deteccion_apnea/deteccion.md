# Detección de apnea

## Qué reporta el radar en cada escena (después de los primeros 30 s)

| Captura | Grupo | Presencia | Distancia (cm) | Tramas/s | fase_total en 0 | RMS fase_resp | Actividad | resp_rpm | latido_bpm |
|---|---|---|---|---|---|---|---|---|---|
| acostado_apnea1_50cm | acostado, apnea | 100 % | 69 | 16.21 | 4.4 % | 0.069 | 0.099 | 9 | 83 |
| acostado_apnea2_50cm | acostado, apnea | 85 % | 63 | 14.29 | 20.1 % | 0.111 | 0.089 | 11 | 95 |
| acostado_8rpm_50cm | acostado | 100 % | 63 | 16.22 | 1.6 % | 0.143 | 0.109 | 8 | 96 |
| acostado_15rpm_50cm | acostado | 100 % | 63 | 16.17 | 2.9 % | 0.204 | 0.224 | 16 | 81 |
| acostado_30rpm_50cm | acostado | 100 % | 75 | 16.18 | 5.4 % | 0.101 | 0.119 | 12 | 90 |
| acostado_variable_rapido_a_lento_50cm | acostado | 100 % | 63 | 16.20 | 1.7 % | 0.178 | 0.225 | 16 | 92 |
| persona_sentada_84cm | sentado | 98 % | 75 | 15.83 | 5.2 % | 0.039 | 0.042 | 1 | 89 |
| normal_84cm | sentado | 100 % | 80 | 16.24 | 1.6 % | 0.091 | 0.104 | 17 | 90 |
| lenta_84cm | sentado | 100 % | 75 | 16.25 | 1.5 % | 0.101 | 0.126 | 3 | 92 |
| rapida_84cm | sentado | 100 % | 80 | 16.19 | 2.0 % | 0.236 | 0.689 | 14 | 100 |
| persona_sentada_80cm | sentado | 100 % | 52 | 16.17 | 2.9 % | 0.227 | 0.391 | 15 | 76 |
| persona_frente_computadora_50cm | sentado | 100 % | 57 | 16.23 | 6.0 % | 0.107 | 0.087 | 18 | 96 |
| cama_vacia_50cm | sin persona | 100 % | 80 | 15.66 | 0.2 % | 0.002 | 0.004 | 0 | 0 |
| sin_nadie_apuntando_pared | sin persona | 100 % | 132 | 16.06 | 0.0 % | 0.003 | 0.017 | 0 | 108 |
| apnea_84cm | sentado, apnea | 100 % | 75 | 16.23 | 2.7 % | 0.149 | 0.119 | 15 | 101 |

## Medida `rms_fase_resp`

### Ajuste con acostado1, prueba con acostado2

Parámetros elegidos: umbral 0.5 × referencia, duración 15 s.

| Captura | Grupo | Resultado |
|---|---|---|
| acostado_apnea1_50cm | entrenamiento | detecta a los 69 s (retraso 24 s) |
| acostado_apnea2_50cm | **prueba** | **no detecta** |
| acostado_8rpm_50cm | acostado | 1 falsa(s) (inicio a los 128 s; 12 s en alarma) |
| acostado_15rpm_50cm | acostado | sin falsas alarmas |
| acostado_30rpm_50cm | acostado | 1 falsa(s) (inicio a los 79 s; 55 s en alarma) |
| acostado_variable_rapido_a_lento_50cm | acostado | sin falsas alarmas |
| persona_sentada_84cm | sentado | sin falsas alarmas |
| normal_84cm | sentado | sin falsas alarmas |
| lenta_84cm | sentado | sin falsas alarmas |
| rapida_84cm | sentado | sin falsas alarmas |
| persona_sentada_80cm | sentado | sin falsas alarmas |
| persona_frente_computadora_50cm | sentado | 1 falsa(s) (inicio a los 51 s; 9 s en alarma) |
| cama_vacia_50cm | sin persona | 1 falsa(s) (inicio a los 46 s; 14 s en alarma) |
| sin_nadie_apuntando_pared | sin persona | sin falsas alarmas |
| apnea_84cm | sentado, apnea (informativa) | **no detecta** |

Capturas sin apnea: **4** falsas alarmas y **90 s** en alarma, en 12.5 min de captura con persona (después de los 30 s).

### Ajuste con acostado2, prueba con acostado1

Parámetros elegidos: umbral 0.6 × referencia, duración 5 s.

| Captura | Grupo | Resultado |
|---|---|---|
| acostado_apnea1_50cm | **prueba** | **no detecta**; 2 falsa(s) fuera de la apnea |
| acostado_apnea2_50cm | entrenamiento | detecta a los 60 s (retraso 15 s) |
| acostado_8rpm_50cm | acostado | 2 falsa(s) (inicio a los 79, 99 s; 62 s en alarma) |
| acostado_15rpm_50cm | acostado | 3 falsa(s) (inicio a los 36, 58, 86 s; 10 s en alarma) |
| acostado_30rpm_50cm | acostado | 1 falsa(s) (inicio a los 48 s; 89 s en alarma) |
| acostado_variable_rapido_a_lento_50cm | acostado | 3 falsa(s) (inicio a los 51, 111, 122 s; 18 s en alarma) |
| persona_sentada_84cm | sentado | sin falsas alarmas |
| normal_84cm | sentado | 3 falsa(s) (inicio a los 38, 47, 75 s; 10 s en alarma) |
| lenta_84cm | sentado | 2 falsa(s) (inicio a los 43, 81 s; 17 s en alarma) |
| rapida_84cm | sentado | sin falsas alarmas |
| persona_sentada_80cm | sentado | sin falsas alarmas |
| persona_frente_computadora_50cm | sentado | 1 falsa(s) (inicio a los 40 s; 20 s en alarma) |
| cama_vacia_50cm | sin persona | 1 falsa(s) (inicio a los 36 s; 24 s en alarma) |
| sin_nadie_apuntando_pared | sin persona | sin falsas alarmas |
| apnea_84cm | sentado, apnea (informativa) | **no detecta** |

Capturas sin apnea: **16** falsas alarmas y **250 s** en alarma, en 12.5 min de captura con persona (después de los 30 s).


## Medida `actividad_fase_total`

### Ajuste con acostado1, prueba con acostado2

Parámetros elegidos: umbral 0.4 × referencia, duración 5 s.

| Captura | Grupo | Resultado |
|---|---|---|
| acostado_apnea1_50cm | entrenamiento | detecta a los 56 s (retraso 11 s) |
| acostado_apnea2_50cm | **prueba** | **no detecta** |
| acostado_8rpm_50cm | acostado | sin falsas alarmas |
| acostado_15rpm_50cm | acostado | 1 falsa(s) (inicio a los 146 s; 1 s en alarma) |
| acostado_30rpm_50cm | acostado | 2 falsa(s) (inicio a los 36, 145 s; 105 s en alarma) |
| acostado_variable_rapido_a_lento_50cm | acostado | sin falsas alarmas |
| persona_sentada_84cm | sentado | sin falsas alarmas |
| normal_84cm | sentado | sin falsas alarmas |
| lenta_84cm | sentado | sin falsas alarmas |
| rapida_84cm | sentado | sin falsas alarmas |
| persona_sentada_80cm | sentado | sin falsas alarmas |
| persona_frente_computadora_50cm | sentado | 1 falsa(s) (inicio a los 43 s; 17 s en alarma) |
| cama_vacia_50cm | sin persona | sin falsas alarmas |
| sin_nadie_apuntando_pared | sin persona | sin falsas alarmas |
| apnea_84cm | sentado, apnea (informativa) | detecta a los 56 s (retraso 11 s) |

Capturas sin apnea: **4** falsas alarmas y **123 s** en alarma, en 12.5 min de captura con persona (después de los 30 s).

### Ajuste con acostado2, prueba con acostado1

Parámetros elegidos: umbral 0.6 × referencia, duración 5 s.

| Captura | Grupo | Resultado |
|---|---|---|
| acostado_apnea1_50cm | **prueba** | detecta a los 55 s (retraso 10 s); 1 falsa(s) fuera de la apnea |
| acostado_apnea2_50cm | entrenamiento | detecta a los 56 s (retraso 11 s) |
| acostado_8rpm_50cm | acostado | 2 falsa(s) (inicio a los 98, 115 s; 4 s en alarma) |
| acostado_15rpm_50cm | acostado | 1 falsa(s) (inicio a los 145 s; 2 s en alarma) |
| acostado_30rpm_50cm | acostado | 1 falsa(s) (inicio a los 36 s; 115 s en alarma) |
| acostado_variable_rapido_a_lento_50cm | acostado | 2 falsa(s) (inicio a los 94, 113 s; 20 s en alarma) |
| persona_sentada_84cm | sentado | 1 falsa(s) (inicio a los 36 s; 3 s en alarma) |
| normal_84cm | sentado | 1 falsa(s) (inicio a los 44 s; 1 s en alarma) |
| lenta_84cm | sentado | 1 falsa(s) (inicio a los 83 s; 3 s en alarma) |
| rapida_84cm | sentado | sin falsas alarmas |
| persona_sentada_80cm | sentado | sin falsas alarmas |
| persona_frente_computadora_50cm | sentado | 1 falsa(s) (inicio a los 40 s; 20 s en alarma) |
| cama_vacia_50cm | sin persona | sin falsas alarmas |
| sin_nadie_apuntando_pared | sin persona | sin falsas alarmas |
| apnea_84cm | sentado, apnea (informativa) | detecta a los 55 s (retraso 10 s) |

Capturas sin apnea: **10** falsas alarmas y **168 s** en alarma, en 12.5 min de captura con persona (después de los 30 s).

