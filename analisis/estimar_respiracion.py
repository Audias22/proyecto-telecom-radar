"""Estima la frecuencia respiratoria a partir de fase_resp del radar MR60BHA2.

Uso:
    python estimar_respiracion.py ../pruebas/crudos/2026-10-06_172200_persona_sentada_84cm.csv --conteo 15.5

Pasos:
    1. Remuestrea fase_resp a FS_HZ (el radar entrega muestras cada ~45/76 ms alternados).
    2. Quita la tendencia lineal y aplica un pasabanda Butterworth (ida y vuelta, sin desfase).
    3. Recorre la señal con una ventana de VENTANA_S segundos que avanza PASO_S segundos y en cada
       ventana estima la frecuencia con dos métodos:
         - pico de la FFT (ventana Hann, zero-padding a N_FFT, interpolación parabólica);
         - cruces por cero ascendentes con histéresis.
       También calcula la amplitud RMS, que separa a una persona de un objeto quieto.
    4. Escribe resumen.txt y grafica.png en pruebas/resultados/<nombre_de_la_captura>/.

Pensado para portarse al ESP32: ventana de tamaño fijo, FFT de tamaño fijo, filtro IIR en
secciones de segundo orden (biquads) y operaciones que no necesitan memoria dinámica.
"""

import argparse
import csv
import math
from pathlib import Path

import numpy as np
from scipy import signal

# --- Parámetros ---------------------------------------------------------------------------

FS_HZ = 10.0                 # frecuencia de remuestreo
BANDA_HZ = (0.1, 0.6)        # 6-36 rpm
ORDEN_FILTRO = 2             # orden del Butterworth (filtfilt lo aplica dos veces)
VENTANA_S = 30.0             # duración de la ventana de análisis
PASO_S = 1.0                 # avance entre ventanas
N_FFT = 4096                 # tamaño fijo de la FFT (zero-padding); resolución 10/4096 Hz
# Umbral de histéresis de los cruces, como fracción del RMS de la ventana. Solo debe rechazar
# ruido cerca de cero: con 0.2 se perdían respiraciones pequeñas en ventanas con respiraciones
# profundas (que inflan el RMS). Elegido con una sola captura con conteo manual; ver docs/radar.md.
FRACCION_HISTERESIS = 0.05

MUESTRAS_VENTANA = int(round(VENTANA_S * FS_HZ))
MUESTRAS_PASO = int(round(PASO_S * FS_HZ))

CARPETA_RESULTADOS = Path(__file__).resolve().parent.parent / "pruebas" / "resultados"


# --- Lectura y preparación -----------------------------------------------------------------

def cargar_csv(ruta):
    """Devuelve (t_s, fase_resp, resp_rpm) del CSV de capturar_serial.py.

    Ignora comentarios '#', filas sin fase_resp y marcas de tiempo repetidas o que retroceden
    (por ejemplo, si la base se reinició durante la captura).
    """
    t, fase, rpm = [], [], []
    with open(ruta, encoding="utf-8") as archivo:
        filas = (linea for linea in archivo if not linea.startswith("#"))
        for fila in csv.DictReader(filas):
            try:
                ms = int(fila["ms"])
                f = float(fila["fase_resp"])
                r = float(fila["resp_rpm"])
            except (TypeError, ValueError):
                continue
            if math.isnan(f) or (t and ms / 1000.0 <= t[-1]):
                continue
            t.append(ms / 1000.0)
            fase.append(f)
            rpm.append(r)
    if len(t) < 2:
        raise ValueError(f"{ruta}: no hay suficientes filas con fase_resp")
    t = np.array(t)
    return t - t[0], np.array(fase), np.array(rpm)


def remuestrear(t, x, fs=FS_HZ):
    """Interpolación lineal a una rejilla uniforme de paso 1/fs."""
    t_uniforme = np.arange(0.0, t[-1], 1.0 / fs)
    return t_uniforme, np.interp(t_uniforme, t, x)


def disenar_filtro(fs=FS_HZ):
    """Pasabanda Butterworth en secciones de segundo orden (lo que se porta como biquads)."""
    return signal.butter(ORDEN_FILTRO, BANDA_HZ, btype="bandpass", fs=fs, output="sos")


def filtrar(x, sos):
    """Quita tendencia lineal y filtra ida y vuelta (sin desfase)."""
    return signal.sosfiltfilt(sos, signal.detrend(x, type="linear"))


# --- Estimadores por ventana ---------------------------------------------------------------

def estimar_fft(ventana, fs=FS_HZ, n_fft=N_FFT):
    """Frecuencia (Hz) del pico del espectro dentro de BANDA_HZ.

    Ventana Hann, zero-padding a n_fft e interpolación parabólica sobre el logaritmo de la
    magnitud de los tres bins alrededor del pico.
    """
    x = (ventana - np.mean(ventana)) * np.hanning(len(ventana))
    magnitud = np.abs(np.fft.rfft(x, n=n_fft))
    resolucion = fs / n_fft
    k_min = int(math.ceil(BANDA_HZ[0] / resolucion))
    k_max = int(math.floor(BANDA_HZ[1] / resolucion))
    k = k_min + int(np.argmax(magnitud[k_min:k_max + 1]))

    desplazamiento = 0.0
    if k_min < k < k_max:
        a, b, c = np.log(magnitud[k - 1:k + 2] + 1e-12)
        denominador = a - 2.0 * b + c
        if denominador != 0.0:
            desplazamiento = 0.5 * (a - c) / denominador
    return (k + desplazamiento) * resolucion


def estimar_cruces(ventana, fs=FS_HZ):
    """Frecuencia (Hz) a partir de los cruces por cero ascendentes, con histéresis.

    Un cruce cuenta solo si antes la señal bajó de -umbral, así el ruido cerca de cero no
    genera cruces falsos. La frecuencia es (cruces - 1) / (tiempo entre el primero y el
    último), con el instante de cada cruce interpolado linealmente. NaN si hay menos de 2.
    """
    x = ventana - np.mean(ventana)
    umbral = FRACCION_HISTERESIS * calcular_rms(x)
    armado = False
    instantes = []
    for i in range(1, len(x)):
        if x[i] < -umbral:
            armado = True
        elif armado and x[i - 1] < 0.0 <= x[i]:
            fraccion = -x[i - 1] / (x[i] - x[i - 1])
            instantes.append((i - 1 + fraccion) / fs)
            armado = False
    if len(instantes) < 2:
        return float("nan")
    return (len(instantes) - 1) / (instantes[-1] - instantes[0])


def calcular_rms(ventana):
    return float(np.sqrt(np.mean(np.square(ventana))))


def analizar_ventanas(t_uniforme, filtrada, t_crudo, rpm_modulo, fs=FS_HZ):
    """Aplica los estimadores en ventanas deslizantes.

    Devuelve un dict de arreglos indexados por ventana. 't_fin' es el instante en que la
    estimación estaría disponible en tiempo real (final de la ventana).
    """
    resultados = {"t_fin": [], "fft_rpm": [], "cruces_rpm": [], "modulo_rpm": [], "rms": []}
    for inicio in range(0, len(filtrada) - MUESTRAS_VENTANA + 1, MUESTRAS_PASO):
        ventana = filtrada[inicio:inicio + MUESTRAS_VENTANA]
        t_ini = t_uniforme[inicio]
        t_fin = t_uniforme[inicio + MUESTRAS_VENTANA - 1]
        en_ventana = (t_crudo >= t_ini) & (t_crudo <= t_fin)

        resultados["t_fin"].append(t_fin)
        resultados["fft_rpm"].append(60.0 * estimar_fft(ventana, fs))
        resultados["cruces_rpm"].append(60.0 * estimar_cruces(ventana, fs))
        resultados["modulo_rpm"].append(float(np.median(rpm_modulo[en_ventana])))
        resultados["rms"].append(calcular_rms(ventana))
    return {clave: np.array(valores) for clave, valores in resultados.items()}


def procesar(t, fase, rpm_modulo):
    """Cadena completa: remuestreo, filtro y ventanas."""
    if t[-1] < VENTANA_S:
        raise ValueError(f"La captura dura {t[-1]:.1f} s; hacen falta al menos {VENTANA_S:.0f} s")
    t_uniforme, fase_uniforme = remuestrear(t, fase)
    filtrada = filtrar(fase_uniforme, disenar_filtro())
    ventanas = analizar_ventanas(t_uniforme, filtrada, t, rpm_modulo)
    return t_uniforme, fase_uniforme, filtrada, ventanas


# --- Salidas -------------------------------------------------------------------------------

def estadisticos(valores):
    validos = valores[~np.isnan(valores)]
    if len(validos) == 0:
        return float("nan"), float("nan")
    return float(np.median(validos)), float(np.std(validos))


def texto_resumen(nombre, t, rpm_modulo, ventanas, conteo):
    lineas = [
        f"Captura: {nombre}",
        f"Duración: {t[-1]:.1f} s, {len(t)} muestras ({(len(t) - 1) / t[-1]:.2f} por segundo)",
        f"Parámetros: remuestreo {FS_HZ:g} Hz, pasabanda {BANDA_HZ[0]:g}-{BANDA_HZ[1]:g} Hz "
        f"(Butterworth orden {ORDEN_FILTRO}, ida y vuelta), ventana {VENTANA_S:g} s, "
        f"paso {PASO_S:g} s, FFT de {N_FFT} puntos",
        f"Ventanas analizadas: {len(ventanas['t_fin'])}",
        "",
        f"{'Estimador':<28}{'mediana':>10}{'desv.':>10}" + (f"{'error vs conteo':>18}" if conteo else ""),
    ]
    filas = [
        ("Módulo (muestras crudas)", rpm_modulo),
        ("Módulo (mediana por ventana)", ventanas["modulo_rpm"]),
        ("FFT", ventanas["fft_rpm"]),
        ("Cruces por cero", ventanas["cruces_rpm"]),
    ]
    for etiqueta, valores in filas:
        mediana, desv = estadisticos(valores)
        linea = f"{etiqueta:<28}{mediana:>10.2f}{desv:>10.2f}"
        if conteo:
            linea += f"{mediana - conteo:>+18.2f}"
        lineas.append(linea)
    if conteo:
        lineas.append(f"{'Conteo manual':<28}{conteo:>10.2f}")
    lineas.append("")
    mediana_rms, desv_rms = estadisticos(ventanas["rms"])
    lineas.append(f"RMS de fase_resp filtrada por ventana: mediana {mediana_rms:.5f}, desv. {desv_rms:.5f}")
    lineas.append("Unidades: rpm (respiraciones por minuto); RMS en las unidades de fase del radar.")
    return "\n".join(lineas) + "\n"


def graficar(ruta_png, nombre, t_uniforme, fase_uniforme, filtrada, t, rpm_modulo, ventanas, conteo):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figura, (ax_senal, ax_rpm, ax_rms) = plt.subplots(3, 1, figsize=(11, 9), sharex=True)
    figura.suptitle(nombre)

    ax_senal.plot(t_uniforme, fase_uniforme, color="0.6", linewidth=0.8, label="fase_resp remuestreada")
    ax_senal.plot(t_uniforme, filtrada, color="C0", linewidth=1.2, label="filtrada (pasabanda)")
    ax_senal.set_ylabel("fase")
    ax_senal.legend(loc="upper right")

    ax_rpm.plot(t, rpm_modulo, color="0.5", linewidth=0.8, label="módulo (resp_rpm)")
    ax_rpm.plot(ventanas["t_fin"], ventanas["fft_rpm"], "o-", color="C1", markersize=3, label="FFT")
    ax_rpm.plot(ventanas["t_fin"], ventanas["cruces_rpm"], "s-", color="C2", markersize=3, label="cruces por cero")
    if conteo:
        ax_rpm.axhline(conteo, color="C3", linestyle="--", label=f"conteo manual ({conteo:g})")
    ax_rpm.set_ylabel("rpm")
    ax_rpm.set_ylim(bottom=0)
    ax_rpm.legend(loc="upper right")

    ax_rms.plot(ventanas["t_fin"], ventanas["rms"], "o-", color="C4", markersize=3)
    ax_rms.set_ylabel("RMS filtrada")
    ax_rms.set_xlabel("tiempo desde el inicio de la captura (s); estimaciones al final de cada ventana")
    ax_rms.set_ylim(bottom=0)

    for eje in (ax_senal, ax_rpm, ax_rms):
        eje.grid(True, alpha=0.3)
    figura.tight_layout()
    figura.savefig(ruta_png, dpi=110)
    plt.close(figura)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, help="captura de pruebas/crudos/")
    parser.add_argument("--conteo", type=float, help="conteo manual en rpm, para comparar")
    args = parser.parse_args()

    t, fase, rpm_modulo = cargar_csv(args.csv)
    t_uniforme, fase_uniforme, filtrada, ventanas = procesar(t, fase, rpm_modulo)

    nombre = args.csv.stem
    carpeta = CARPETA_RESULTADOS / nombre
    carpeta.mkdir(parents=True, exist_ok=True)

    resumen = texto_resumen(nombre, t, rpm_modulo, ventanas, args.conteo)
    (carpeta / "resumen.txt").write_text(resumen, encoding="utf-8")
    graficar(carpeta / "grafica.png", nombre, t_uniforme, fase_uniforme, filtrada, t, rpm_modulo,
             ventanas, args.conteo)

    print(resumen)
    print(f"Resultados en {carpeta}")


if __name__ == "__main__":
    main()
