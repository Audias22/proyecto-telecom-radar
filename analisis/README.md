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
| `validar_respiracion.py` | Compara los estimadores contra las capturas con conteo manual (espectros y tabla de errores) |
| `detectar_apnea.py` | Detector de apnea por caída de amplitud; evalúa detección y falsas alarmas en todas las capturas |

### Capturar

```
python capturar_serial.py persona_sentada_84cm --duracion 60
```

Desde la terminal de VS Code en Windows (PowerShell, en la raíz del repositorio), usando el venv
sin activarlo:

```
analisis\.venv\Scripts\python analisis\capturar_serial.py persona_sentada_84cm --puerto COM3 --duracion 60
```

Si el venv todavía no existe, crearlo una vez desde la raíz:

```
python -m venv analisis\.venv
analisis\.venv\Scripts\python -m pip install -r analisis\requirements.txt
```

**El monitor serial de PlatformIO tiene que estar cerrado**: ocupa el COM3 y el script no puede
abrir el puerto (error de acceso denegado). Cerrarlo con la papelera de la terminal del monitor o
con Ctrl+C dentro de ella. El número de puerto puede cambiar entre computadoras; `pio device list`
lo muestra. Sin `--duracion`, la captura sigue hasta Ctrl+C.

### Estimar la respiración

```
python estimar_respiracion.py ..\pruebas\crudos\2026-10-06_172200_persona_sentada_84cm.csv --conteo 15.5
```

`--conteo` es opcional: el conteo manual en respiraciones por minuto, para calcular el error.
`--senal` elige la señal: `fase_resp` (por defecto), `fase_total_desenvuelta` o
`fase_total_integrada`. Escribe `resumen.txt` y `grafica.png` en
`pruebas/resultados/<nombre_de_la_captura>/` (con `_<senal>` al final si no es `fase_resp`). Los
parámetros (rango, banda, ventana, tamaño de FFT, subarmónico, histéresis) son constantes al
principio del script. El método y los resultados están en `docs/radar.md`, secciones 8 a 10.

### Validar y detectar apnea

```
python validar_respiracion.py
python detectar_apnea.py
```

Usan las capturas listadas al principio de cada script (con su conteo manual o el intervalo de
apnea) y escriben en `pruebas/resultados/validacion_respiracion/` y
`pruebas/resultados/deteccion_apnea/`.

### Pruebas

```
pytest test_estimar_respiracion.py
```

o, sin pytest:

```
python test_estimar_respiracion.py
```

Generan un seno de 0.25 Hz (15 rpm) con ruido, deriva y el muestreo irregular del radar, y
verifican que FFT, autocorrelación y cruces den 15 ± 1 rpm en todas las ventanas. También
prueban los extremos del rango (6.5 y 34 rpm) y una señal de 8 rpm con el segundo armónico más
fuerte que la fundamental.

## Convenciones

- Un script por análisis, con nombre descriptivo (`fft_respiracion.py`, `umbral_apnea.py`).
- Cada script recibe el archivo de entrada como argumento y no tiene rutas fijas de una sola
  computadora.
