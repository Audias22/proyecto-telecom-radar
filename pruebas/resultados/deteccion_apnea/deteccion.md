# Detección de apnea por caída de amplitud

Señal fase_resp, pasabanda causal 0.08-0.7 Hz, RMS de 5 s, referencia = mediana del RMS de los últimos 45 s normales, umbral 0.5 × referencia durante 10 s, referencia mínima 0.01, presencia obligatoria.

## Resultado con los parámetros elegidos

| Captura | Alarmas (s) | Resultado |
|---|---|---|
| apnea_84cm | ninguna | apnea NO detectada |
| persona_sentada_84cm | 45 | 1 falsa(s) alarma(s) |
| normal_84cm | ninguna | sin falsas alarmas |
| lenta_84cm | 51, 86 | 2 falsa(s) alarma(s) |
| rapida_84cm | ninguna | sin falsas alarmas |
| sin_nadie_apuntando_pared | ninguna | sin falsas alarmas |
| persona_sentada_80cm | ninguna | sin falsas alarmas |
| persona_frente_computadora_50cm | 46 | 1 falsa(s) alarma(s) |

## Sensibilidad a umbral y duración mínima

Detección en la captura de apnea (retraso desde el segundo 45) / falsas alarmas sumando todas las capturas (incluidas las de la captura de apnea fuera del intervalo).

| Umbral | 5 s | 10 s | 15 s |
|---|---|---|---|
| 0.3 | no detecta / 2 | no detecta / 0 | no detecta / 0 |
| 0.4 | no detecta / 2 | no detecta / 1 | no detecta / 1 |
| 0.5 | no detecta / 6 | no detecta / 4 | no detecta / 2 |
| 0.6 | no detecta / 6 | no detecta / 4 | no detecta / 2 |
