# pruebas

Datos de las mediciones y sus resultados.

- `crudos/`: datos tal como salen del equipo (logs del monitor serial, exportaciones de
  Firebase en CSV o JSON). No se editan a mano.
- `resultados/`: tablas, gráficas y conclusiones generadas a partir de los crudos.

## Nombres de archivo

`AAAA-MM-DD_tema_detalle.ext`, por ejemplo:

- `2026-10-10_radar_respiracion_distancia100cm.csv`
- `2026-10-11_espnow_latencia_ack.txt`
- `2026-10-12_colgante_consumo_deepsleep.csv`

## Registro de cada prueba

Junto a cada archivo crudo, o en `resultados/`, anotar:

- Fecha, quién la hizo y versión de firmware.
- Montaje: distancia, posición de la persona, obstáculos, canal WiFi.
- Qué se midió y con qué instrumento (por ejemplo, conteo manual de respiraciones con
  cronómetro).
- Resultado y conclusión en una o dos líneas.

Pendientes que se resuelven aquí: latencia y tasa de éxito del ACK, consumo del colgante en deep
sleep, curva de descarga de la batería, umbral de apnea, tamaño real de las lecturas en Firebase.
