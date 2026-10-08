"""Estima la frecuencia respiratoria a partir de la fase del radar MR60BHA2.

Uso:
    python estimar_respiracion.py ../pruebas/crudos/2026-10-07_222846_normal_84cm.csv --conteo 14
    python estimar_respiracion.py ../pruebas/crudos/<captura>.csv --senal fase_total_integrada

Señales de entrada (--senal):
    fase_resp               la fase de respiración que entrega el módulo
    fase_total_desenvuelta  fase_total sin las muestras en 0 exacto, con unwrap
    fase_total_integrada    fase_total sin las muestras en 0 exacto, mediana de 3 y suma acumulada
                            (fase_total se comporta como un incremento de fase; ver docs/radar.md)

Pasos:
    1. Remuestrea la señal a FS_HZ (el radar entrega muestras cada ~45/76 ms alternados).
    2. Quita la tendencia lineal y aplica un pasabanda Butterworth (ida y vuelta, sin desfase).
    3. Recorre la señal con una ventana de VENTANA_S segundos que avanza PASO_S segundos y en cada
       ventana estima la frecuencia en RANGO_RPM con tres métodos:
         - pico de la FFT (Hann, zero-padding, interpolación parabólica) con verificación de
           subarmónico: si a la mitad de la frecuencia del pico hay un pico casi igual de alto,
           se toma la mitad (corrige el caso en que el armónico le gana a la fundamental);
         - autocorrelación: el retardo del pico más alto dentro del rango de periodos;
         - cruces por cero ascendentes con histéresis.
       También calcula la amplitud RMS, que separa a una persona de un objeto quieto.
    4. Escribe resumen.txt y grafica.png en pruebas/resultados/<captura>[_<senal>]/.

Pensado para portarse al ESP32: ventana de tamaño fijo, FFT de tamaño fijo, filtro IIR en
secciones de segundo orden (biquads), autocorrelación con un rango de retardos fijo.
"""

import argparse
import csv
import math
from pathlib import Path

import numpy as np
from scipy import signal

# --- Parámetros ---------------------------------------------------------------------------

FS_HZ = 10.0                 # frecuencia de remuestreo
RANGO_RPM = (6.0, 35.0)      # rango de frecuencias respiratorias que se buscan
BANDA_HZ = (0.08, 0.70)      # pasabanda: un poco más ancho que RANGO_RPM (0.10-0.58 Hz) para no
                             # atenuar los extremos del rango
ORDEN_FILTRO = 2             # orden del Butterworth (filtfilt lo aplica dos veces)
VENTANA_S = 30.0             # duración de la ventana de análisis
PASO_S = 1.0                 # avance entre ventanas
N_FFT = 4096                 # tamaño fijo de la FFT (zero-padding); resolución 10/4096 Hz
UMBRAL_SUBARMONICO = 0.5     # si el pico en f/2 mide al menos esta fracción del pico en f, se
                             # toma f/2
# Umbral de histéresis de los cruces, como fracción del RMS de la ventana. Solo debe rechazar
# ruido cerca de cero (ver docs/radar.md, sección 8).
FRACCION_HISTERESIS = 0.05

SENALES = ("fase_resp", "fase_total_desenvuelta", "fase_total_integrada")

MUESTRAS_VENTANA = int(round(VENTANA_S * FS_HZ))
MUESTRAS_PASO = int(round(PASO_S * FS_HZ))

CARPETA_RESULTADOS = Path(__file__).resolve().parent.parent / "pruebas" / "resultados"


# --- Lectura y preparación -----------------------------------------------------------------

def cargar_csv(ruta):
    """Lee un CSV de capturar_serial.py y devuelve un dict de arreglos.

    Claves: t (s desde la primera fila), fase_resp, fase_total, resp_rpm, presencia.
    Ignora comentarios '#', filas incompletas y marcas de tiempo repetidas o que retroceden
    (por ejemplo, si la base se reinició durante la captura).
    """
    columnas = {"t": [], "fase_resp": [], "fase_total": [], "resp_rpm": [], "presencia": []}
    with open(ruta, encoding="utf-8") as archivo:
        filas = (linea for linea in archivo if not linea.startswith("#"))
        for fila in csv.DictReader(filas):
            try:
                t = int(fila["ms"]) / 1000.0
                valores = {
                    "fase_resp": float(fila["fase_resp"]),
                    "fase_total": float(fila["fase_total"]),
                    "resp_rpm": float(fila["resp_rpm"]),
                    "presencia": float(fila["presencia"]),
                }
            except (TypeError, ValueError):
                continue
            if math.isnan(valores["fase_resp"]) or (columnas["t"] and t <= columnas["t"][-1]):
                continue
            columnas["t"].append(t)
            for clave, valor in valores.items():
                columnas[clave].append(valor)
    if len(columnas["t"]) < 2:
        raise ValueError(f"{ruta}: no hay suficientes filas con fase")
    datos = {clave: np.array(valores) for clave, valores in columnas.items()}
    datos["t"] = datos["t"] - datos["t"][0]
    return datos


def senal_respiracion(datos, senal):
    """Devuelve (t, x) de la señal elegida, todavía con el muestreo irregular del radar."""
    t = datos["t"]
    if senal == "fase_resp":
        return t, datos["fase_resp"]

    # fase_total trae muestras en 0.0 exacto a intervalos regulares (siempre en la trama que
    # sigue al intervalo corto); se interpretan como tramas sin dato y se descartan.
    validas = datos["fase_total"] != 0.0
    t_validas = t[validas]
    fase = datos["fase_total"][validas]
    if senal == "fase_total_desenvuelta":
        return t_validas, np.unwrap(fase)
    if senal == "fase_total_integrada":
        # La mediana de 3 quita los picos aislados de ±2-3 rad antes de acumular.
        return t_validas, np.cumsum(signal.medfilt(fase, 3))
    raise ValueError(f"señal desconocida: {senal}")


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

def _interpolar_pico(y_izq, y_centro, y_der):
    """Desplazamiento (en muestras, entre -0.5 y 0.5) del vértice de la parábola por 3 puntos."""
    denominador = y_izq - 2.0 * y_centro + y_der
    if denominador == 0.0:
        return 0.0
    return 0.5 * (y_izq - y_der) / denominador


def estimar_fft(ventana, fs=FS_HZ, n_fft=N_FFT, verificar_subarmonico=True):
    """Frecuencia (Hz) del pico del espectro dentro de RANGO_RPM.

    Ventana Hann, zero-padding a n_fft, verificación de subarmónico e interpolación parabólica
    sobre el logaritmo de la magnitud de los tres bins alrededor del pico.
    """
    x = (ventana - np.mean(ventana)) * np.hanning(len(ventana))
    magnitud = np.abs(np.fft.rfft(x, n=n_fft))
    resolucion = fs / n_fft
    k_min = int(math.ceil(RANGO_RPM[0] / 60.0 / resolucion))
    k_max = int(math.floor(RANGO_RPM[1] / 60.0 / resolucion))
    k = k_min + int(np.argmax(magnitud[k_min:k_max + 1]))

    if verificar_subarmonico:
        k_mitad = int(round(k / 2.0))
        if k_mitad - 2 >= k_min:
            vecinos = magnitud[k_mitad - 2:k_mitad + 3]
            k_sub = k_mitad - 2 + int(np.argmax(vecinos))
            if magnitud[k_sub] >= UMBRAL_SUBARMONICO * magnitud[k]:
                k = k_sub

    desplazamiento = 0.0
    if k_min < k < k_max:
        log_mag = np.log(magnitud[k - 1:k + 2] + 1e-12)
        desplazamiento = _interpolar_pico(*log_mag)
    return (k + desplazamiento) * resolucion


def estimar_autocorrelacion(ventana, fs=FS_HZ):
    """Frecuencia (Hz) a partir del pico más alto de la autocorrelación en el rango de periodos.

    Autocorrelación sesgada (dividida entre N): los picos en múltiplos del periodo decrecen, así
    que el más alto es el periodo fundamental aunque haya armónicos fuertes. NaN si no hay pico.
    """
    x = ventana - np.mean(ventana)
    n = len(x)
    retardo_min = int(math.ceil(fs * 60.0 / RANGO_RPM[1]))
    retardo_max = min(int(math.floor(fs * 60.0 / RANGO_RPM[0])), n - 2)
    acf = np.array([np.dot(x[:n - r], x[r:]) for r in range(retardo_max + 2)]) / n

    mejor, mejor_valor = -1, -np.inf
    for r in range(max(retardo_min, 1), retardo_max + 1):
        if acf[r - 1] < acf[r] >= acf[r + 1] and acf[r] > mejor_valor:
            mejor, mejor_valor = r, acf[r]
    if mejor < 0 or mejor_valor <= 0.0:
        return float("nan")
    retardo = mejor + _interpolar_pico(acf[mejor - 1], acf[mejor], acf[mejor + 1])
    return fs / retardo


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


ESTIMADORES = {
    "fft": estimar_fft,
    "autocorrelacion": estimar_autocorrelacion,
    "cruces": estimar_cruces,
}


def analizar_ventanas(t_uniforme, filtrada, t_crudo, rpm_modulo, fs=FS_HZ):
    """Aplica los estimadores en ventanas deslizantes.

    Devuelve un dict de arreglos indexados por ventana: t_ini, t_fin (instante en que la
    estimación estaría disponible en tiempo real), <estimador>_rpm, modulo_rpm y rms.
    """
    claves = ["t_ini", "t_fin", "modulo_rpm", "rms"] + [f"{nombre}_rpm" for nombre in ESTIMADORES]
    resultados = {clave: [] for clave in claves}
    for inicio in range(0, len(filtrada) - MUESTRAS_VENTANA + 1, MUESTRAS_PASO):
        ventana = filtrada[inicio:inicio + MUESTRAS_VENTANA]
        t_ini = t_uniforme[inicio]
        t_fin = t_uniforme[inicio + MUESTRAS_VENTANA - 1]
        en_ventana = (t_crudo >= t_ini) & (t_crudo <= t_fin)

        resultados["t_ini"].append(t_ini)
        resultados["t_fin"].append(t_fin)
        for nombre, estimador in ESTIMADORES.items():
            resultados[f"{nombre}_rpm"].append(60.0 * estimador(ventana, fs))
        resultados["modulo_rpm"].append(float(np.median(rpm_modulo[en_ventana])))
        resultados["rms"].append(calcular_rms(ventana))
    return {clave: np.array(valores) for clave, valores in resultados.items()}


def procesar(t, x, rpm_modulo, t_modulo=None):
    """Cadena completa: remuestreo, filtro y ventanas.

    t_modulo son los instantes de rpm_modulo, si no coinciden con t (por ejemplo, cuando se
    descartaron muestras de fase_total).
    """
    if t[-1] < VENTANA_S:
        raise ValueError(f"La captura dura {t[-1]:.1f} s; hacen falta al menos {VENTANA_S:.0f} s")
    t_uniforme, x_uniforme = remuestrear(t, x)
    filtrada = filtrar(x_uniforme, disenar_filtro())
    ventanas = analizar_ventanas(t_uniforme, filtrada, t if t_modulo is None else t_modulo, rpm_modulo)
    return t_uniforme, x_uniforme, filtrada, ventanas


# --- Salidas -------------------------------------------------------------------------------

def estadisticos(valores):
    validos = valores[~np.isnan(valores)]
    if len(validos) == 0:
        return float("nan"), float("nan")
    return float(np.median(validos)), float(np.std(validos))


def texto_resumen(nombre, senal, t, rpm_modulo, ventanas, conteo):
    lineas = [
        f"Captura: {nombre}",
        f"Señal: {senal}",
        f"Duración: {t[-1]:.1f} s, {len(t)} muestras ({(len(t) - 1) / t[-1]:.2f} por segundo)",
        f"Parámetros: remuestreo {FS_HZ:g} Hz, pasabanda {BANDA_HZ[0]:g}-{BANDA_HZ[1]:g} Hz "
        f"(Butterworth orden {ORDEN_FILTRO}, ida y vuelta), rango {RANGO_RPM[0]:g}-{RANGO_RPM[1]:g} rpm, "
        f"ventana {VENTANA_S:g} s, paso {PASO_S:g} s, FFT de {N_FFT} puntos",
        f"Ventanas analizadas: {len(ventanas['t_fin'])}",
        "",
        f"{'Estimador':<28}{'mediana':>10}{'desv.':>10}" + (f"{'error vs conteo':>18}" if conteo else ""),
    ]
    filas = [
        ("Módulo (muestras crudas)", rpm_modulo),
        ("Módulo (mediana por ventana)", ventanas["modulo_rpm"]),
        ("FFT", ventanas["fft_rpm"]),
        ("Autocorrelación", ventanas["autocorrelacion_rpm"]),
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
    lineas.append(f"RMS de la señal filtrada por ventana: mediana {mediana_rms:.5f}, desv. {desv_rms:.5f}")
    lineas.append("Unidades: rpm (respiraciones por minuto); RMS en las unidades de fase del radar.")
    return "\n".join(lineas) + "\n"


def graficar(ruta_png, titulo, t_uniforme, x_uniforme, filtrada, t, rpm_modulo, ventanas, conteo):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figura, (ax_senal, ax_rpm, ax_rms) = plt.subplots(3, 1, figsize=(11, 9), sharex=True)
    figura.suptitle(titulo)

    ax_senal.plot(t_uniforme, x_uniforme - np.mean(x_uniforme), color="0.6", linewidth=0.8,
                  label="remuestreada (sin media)")
    ax_senal.plot(t_uniforme, filtrada, color="C0", linewidth=1.2, label="filtrada (pasabanda)")
    ax_senal.set_ylabel("fase")
    ax_senal.legend(loc="upper right")

    ax_rpm.plot(t, rpm_modulo, color="0.5", linewidth=0.8, label="módulo (resp_rpm)")
    ax_rpm.plot(ventanas["t_fin"], ventanas["fft_rpm"], "o-", color="C1", markersize=3, label="FFT")
    ax_rpm.plot(ventanas["t_fin"], ventanas["autocorrelacion_rpm"], "^-", color="C5", markersize=3,
                label="autocorrelación")
    ax_rpm.plot(ventanas["t_fin"], ventanas["cruces_rpm"], "s-", color="C2", markersize=3, label="cruces por cero")
    if conteo:
        ax_rpm.axhline(conteo, color="C3", linestyle="--", label=f"conteo manual ({conteo:g})")
    ax_rpm.set_ylabel("rpm")
    ax_rpm.set_ylim(0, RANGO_RPM[1] + 5)
    ax_rpm.legend(loc="upper right", fontsize=8)

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
    parser.add_argument("--senal", choices=SENALES, default="fase_resp")
    args = parser.parse_args()

    datos = cargar_csv(args.csv)
    t, x = senal_respiracion(datos, args.senal)
    t_uniforme, x_uniforme, filtrada, ventanas = procesar(t, x, datos["resp_rpm"], datos["t"])

    nombre = args.csv.stem
    carpeta = CARPETA_RESULTADOS / (nombre if args.senal == "fase_resp" else f"{nombre}_{args.senal}")
    carpeta.mkdir(parents=True, exist_ok=True)

    resumen = texto_resumen(nombre, args.senal, datos["t"], datos["resp_rpm"], ventanas, args.conteo)
    (carpeta / "resumen.txt").write_text(resumen, encoding="utf-8", newline="\n")
    graficar(carpeta / "grafica.png", f"{nombre} ({args.senal})", t_uniforme, x_uniforme, filtrada,
             datos["t"], datos["resp_rpm"], ventanas, args.conteo)

    print(resumen)
    print(f"Resultados en {carpeta}")


if __name__ == "__main__":
    main()
