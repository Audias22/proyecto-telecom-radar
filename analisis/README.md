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

## Convenciones

- Un script por análisis, con nombre descriptivo (`fft_respiracion.py`, `umbral_apnea.py`).
- Cada script recibe el archivo de entrada como argumento y no tiene rutas fijas de una sola
  computadora.
