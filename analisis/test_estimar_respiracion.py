"""Pruebas de estimar_respiracion.py con señales sintéticas.

Correr con:
    pytest test_estimar_respiracion.py
o sin pytest:
    python test_estimar_respiracion.py
"""

import numpy as np

import estimar_respiracion as er

FRECUENCIA_HZ = 0.25   # 15 rpm
TOLERANCIA_RPM = 1.0
DURACION_S = 120.0


def tiempos_como_radar(duracion_s, semilla=1):
    """Marcas de tiempo con el patrón medido en el radar: intervalos de ~45 y ~76 ms alternados,
    con unos milisegundos de variación."""
    rng = np.random.default_rng(semilla)
    t = [0.0]
    largo = True
    while t[-1] < duracion_s:
        base = 0.076 if largo else 0.045
        t.append(t[-1] + base + rng.uniform(-0.003, 0.003))
        largo = not largo
    return np.array(t)


def senal_respiracion(t, amplitud=0.3, ruido=0.05, semilla=2):
    """Seno de FRECUENCIA_HZ más ruido gaussiano y una deriva lenta, como fase_resp."""
    rng = np.random.default_rng(semilla)
    deriva = 0.002 * t
    return amplitud * np.sin(2 * np.pi * FRECUENCIA_HZ * t) + deriva + rng.normal(0.0, ruido, len(t))


def _procesar_sintetica(**kwargs):
    t = tiempos_como_radar(DURACION_S)
    fase = senal_respiracion(t, **kwargs)
    rpm_modulo = np.zeros_like(t)
    _, _, _, ventanas = er.procesar(t, fase, rpm_modulo)
    return ventanas


def test_fft_da_15_rpm():
    ventanas = _procesar_sintetica()
    error = np.abs(ventanas["fft_rpm"] - 60 * FRECUENCIA_HZ)
    assert len(error) > 0
    assert np.all(error <= TOLERANCIA_RPM), f"error máximo FFT {error.max():.2f} rpm"


def test_cruces_da_15_rpm():
    ventanas = _procesar_sintetica()
    error = np.abs(ventanas["cruces_rpm"] - 60 * FRECUENCIA_HZ)
    assert not np.any(np.isnan(error))
    assert np.all(error <= TOLERANCIA_RPM), f"error máximo cruces {error.max():.2f} rpm"


def test_rms_separa_persona_de_objeto_quieto():
    persona = _procesar_sintetica(amplitud=0.3, ruido=0.05)
    quieto = _procesar_sintetica(amplitud=0.0, ruido=0.005)
    assert np.median(persona["rms"]) > 10 * np.median(quieto["rms"])


def test_estimar_fft_interpola_entre_bins():
    # 0.2537 Hz no cae en un bin exacto de la FFT de 4096 puntos a 10 Hz (paso 0.00244 Hz).
    t = np.arange(er.MUESTRAS_VENTANA) / er.FS_HZ
    x = np.sin(2 * np.pi * 0.2537 * t)
    assert abs(er.estimar_fft(x) - 0.2537) < 0.002


if __name__ == "__main__":
    pruebas = [valor for nombre, valor in sorted(globals().items()) if nombre.startswith("test_")]
    fallas = 0
    for prueba in pruebas:
        try:
            prueba()
            print(f"OK    {prueba.__name__}")
        except AssertionError as error:
            fallas += 1
            print(f"FALLA {prueba.__name__}: {error}")
    print(f"{len(pruebas) - fallas}/{len(pruebas)} pruebas pasaron")
    raise SystemExit(1 if fallas else 0)
