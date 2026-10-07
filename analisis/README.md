# analisis

Scripts de Python para procesar señales del radar fuera del microcontrolador: filtros, FFT,
comparación con conteo manual de respiraciones y ajuste de umbrales (por ejemplo, el de apnea).

Los datos de entrada se leen de `pruebas/crudos/` y las gráficas o tablas resultantes se guardan
en `pruebas/resultados/`.

## Entorno

```
cd analisis
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```

`.venv/` está en `.gitignore`.

## Scripts

| Script | Qué hace |
|---|---|
| `capturar_serial.py` | Guarda el CSV que imprime la base en `pruebas/crudos/` |
| `estimar_respiracion.py` | Estima la frecuencia respiratoria a partir de `fase_resp` (FFT y cruces por cero) |
| `test_estimar_respiracion.py` | Pruebas de `estimar_respiracion.py` con señales sintéticas |

### Capturar

```
python capturar_serial.py persona_sentada_84cm --duracion 60
```

### Estimar la respiración

```
python estimar_respiracion.py ..\pruebas\crudos\2026-10-06_172200_persona_sentada_84cm.csv --conteo 15.5
```

`--conteo` es opcional: el conteo manual en respiraciones por minuto, para calcular el error.
Escribe `resumen.txt` y `grafica.png` en `pruebas/resultados/<nombre_de_la_captura>/`. Los
parámetros (frecuencia de remuestreo, banda, ventana, tamaño de FFT, histéresis) son constantes
al principio del script. El método y los resultados están en `docs/radar.md`, sección
"Procesamiento propio".

### Pruebas

```
pytest test_estimar_respiracion.py
```

o, sin pytest:

```
python test_estimar_respiracion.py
```

Generan un seno de 0.25 Hz (15 rpm) con ruido, deriva y el muestreo irregular del radar, y
verifican que FFT y cruces den 15 ± 1 rpm en todas las ventanas.

## Convenciones

- Un script por análisis, con nombre descriptivo (`fft_respiracion.py`, `umbral_apnea.py`).
- Cada script recibe el archivo de entrada como argumento y no tiene rutas fijas de una sola
  computadora.
