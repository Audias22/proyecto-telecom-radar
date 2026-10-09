# Detector de apnea v2: evaluación contra el criterio de éxito

Criterio (fijado de antemano): detectar en ≤ 30 s desde el inicio de la apnea y ≤ 1 falsa alarma cada 10 min de respiración normal. Duración mínima de la caída 20 s; referencia = mediana de los últimos 60 s normales; se arma tras 30 s de respiración (actividad ≥ 0.03); la referencia se reaprende tras 60 s bajo.

Falsas alarmas en 12 capturas sin apnea (12.5 min con persona después de los 30 s; pared y cama vacía cuentan alarmas pero no minutos) más las de las capturas de apnea fuera del intervalo.

## Validación cruzada (una apnea fuera cada vez)

| Apnea de prueba | Configuración elegida con las otras dos | Apnea de prueba | Falsas alarmas | Por 10 min | Segundos en alarma | ¿Cumple? |
|---|---|---|---|---|---|---|
| acostado1 | actividad, ventana 10 s, umbral 0.7 | detecta a los +26 s del inicio | 4 | 3.2 | 87 | **no** |
| acostado2 | actividad, ventana 10 s, umbral 0.7 | detecta a los +29 s del inicio | 4 | 3.2 | 87 | **no** |
| sentado | actividad, ventana 5 s, umbral 0.8 | **no detecta** | 4 | 3.2 | 81 | **no** |

Cumple el criterio en 0 de 3 validaciones.

## Cota optimista (eligiendo viendo las tres apneas)

Las 8 mejores configuraciones (más apneas detectadas a tiempo, luego menos falsas alarmas):

| Configuración | Apneas a tiempo | acostado1 | acostado2 | sentado | Falsas alarmas | Por 10 min |
|---|---|---|---|---|---|---|
| actividad, ventana 10 s, umbral 0.7 | 3/3 | +26 s | +29 s | +26 s | 4 | 3.2 |
| actividad, ventana 10 s, umbral 0.6 | 2/3 | +28 s | – | +27 s | 3 | 2.4 |
| actividad, ventana 5 s, umbral 0.7 | 2/3 | +24 s | +26 s | – | 4 | 3.2 |
| actividad, ventana 5 s, umbral 0.8 | 2/3 | +24 s | +25 s | – | 4 | 3.2 |
| actividad, ventana 10 s, umbral 0.8 | 2/3 | – | +29 s | +26 s | 5 | 4.0 |
| actividad, ventana 10 s, umbral 0.5 | 1/3 | +30 s | – | – | 1 | 0.8 |
| actividad, ventana 5 s, umbral 0.5 | 1/3 | +26 s | – | – | 2 | 1.6 |
| actividad, ventana 5 s, umbral 0.6 | 1/3 | +26 s | – | – | 2 | 1.6 |

Mejor configuración que detecta las tres a tiempo: actividad, ventana 10 s, umbral 0.7, con 4 falsas alarmas (3.2 cada 10 min).
