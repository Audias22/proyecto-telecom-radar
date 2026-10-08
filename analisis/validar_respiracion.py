"""Valida los estimadores de respiración contra las capturas con conteo manual.

Uso:
    python validar_respiracion.py

Para cada captura de CAPTURAS y cada señal (fase_resp, fase_total desenvuelta, fase_total
integrada) estima la frecuencia con los tres estimadores de estimar_respiracion.py, usando la
mediana de las ventanas de 30 s que caen completas dentro del tramo del conteo manual, y la
compara con el conteo. Los parámetros son los mismos para todas las capturas.

Salidas en pruebas/resultados/validacion_respiracion/:
    espectros.png  espectro de cada señal en el tramo del conteo, con el conteo marcado
    errores.md     tabla de estimaciones y errores por captura, y error absoluto medio
"""

from pathlib import Path

import numpy as np
from scipy import signal

import estimar_respiracion as er

CARPETA_CRUDOS = Path(__file__).resolve().parent.parent / "pruebas" / "crudos"
CARPETA_SALIDA = er.CARPETA_RESULTADOS / "validacion_respiracion"

# (captura, conteo manual en rpm, tramo del conteo en s desde el inicio de la captura)
CAPTURAS = [
    ("2026-10-06_172200_persona_sentada_84cm", 15.5, (0.0, 62.0)),
    ("2026-10-07_222846_normal_84cm", 14.0, (30.0, 90.0)),
    ("2026-10-07_223440_lenta_84cm", 8.0, (30.0, 90.0)),
    ("2026-10-07_223850_rapida_84cm", 31.0, (30.0, 90.0)),
]

ETIQUETAS_SENAL = {
    "fase_resp": "fase_resp",
    "fase_total_desenvuelta": "fase_total desenvuelta",
    "fase_total_integrada": "fase_total integrada",
}
ETIQUETAS_ESTIMADOR = {"fft": "FFT", "autocorrelacion": "Autocorrelación", "cruces": "Cruces"}

N_FFT_ESPECTRO = 8192


def estimar_en_tramo(datos, senal, tramo):
    """Mediana de cada estimador en las ventanas que caen completas dentro del tramo."""
    t, x = er.senal_respiracion(datos, senal)
    _, _, _, ventanas = er.procesar(t, x, datos["resp_rpm"], datos["t"])
    dentro = (ventanas["t_ini"] >= tramo[0] - 0.5) & (ventanas["t_fin"] <= tramo[1] + 0.5)
    if not np.any(dentro):
        raise ValueError(f"ninguna ventana de {er.VENTANA_S:g} s cabe en el tramo {tramo}")
    estimaciones = {nombre: float(np.nanmedian(ventanas[f"{nombre}_rpm"][dentro])) for nombre in er.ESTIMADORES}
    estimaciones["rms"] = float(np.median(ventanas["rms"][dentro]))
    return estimaciones


def espectro_en_tramo(datos, senal, tramo):
    """Espectro (rpm, magnitud normalizada) de la señal sin tendencia, en el tramo del conteo."""
    t, x = er.senal_respiracion(datos, senal)
    t_uniforme, x_uniforme = er.remuestrear(t, x)
    en_tramo = (t_uniforme >= tramo[0]) & (t_uniforme <= tramo[1])
    segmento = signal.detrend(x_uniforme[en_tramo]) * np.hanning(int(np.sum(en_tramo)))
    magnitud = np.abs(np.fft.rfft(segmento, n=N_FFT_ESPECTRO))
    rpm = 60.0 * np.fft.rfftfreq(N_FFT_ESPECTRO, 1.0 / er.FS_HZ)
    en_rango = (rpm >= er.RANGO_RPM[0]) & (rpm <= er.RANGO_RPM[1])
    return rpm, magnitud / np.max(magnitud[en_rango])


def graficar_espectros(ruta_png, cargadas):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    filas = len(cargadas)
    figura, ejes = plt.subplots(filas, len(er.SENALES), figsize=(15, 3.2 * filas), sharex=True)
    for i, (nombre, conteo, tramo, datos) in enumerate(cargadas):
        for j, senal in enumerate(er.SENALES):
            eje = ejes[i, j]
            rpm, magnitud = espectro_en_tramo(datos, senal, tramo)
            eje.plot(rpm, magnitud, color="C0", linewidth=1.0)
            eje.axvspan(*er.RANGO_RPM, color="0.92", zorder=0)
            eje.axvline(conteo, color="C3", linewidth=1.5, label=f"conteo ({conteo:g} rpm)")
            eje.axvline(2 * conteo, color="C3", linewidth=1.0, linestyle=":", label="2 × conteo")
            eje.set_xlim(0, 50)
            eje.set_ylim(0, 1.15)
            if i == 0:
                eje.set_title(ETIQUETAS_SENAL[senal])
            if j == 0:
                eje.set_ylabel(f"{nombre.split('_', 2)[2]}\n{tramo[0]:g}-{tramo[1]:g} s")
            if i == filas - 1:
                eje.set_xlabel("rpm")
            if i == 0 and j == 0:
                eje.legend(loc="upper right", fontsize=8)
            eje.grid(True, alpha=0.3)
    figura.suptitle("Espectro en el tramo del conteo manual (zona gris: rango buscado 6-35 rpm; "
                    "magnitud normalizada al máximo del rango)")
    figura.tight_layout()
    figura.savefig(ruta_png, dpi=100)
    plt.close(figura)


def tabla_errores(resultados):
    """Tabla markdown: una fila por captura y columnas por señal/estimador, más el MAE."""
    columnas = [(s, e) for s in er.SENALES for e in er.ESTIMADORES]
    encabezado = ["Captura", "Conteo", "Módulo"] + [f"{ETIQUETAS_SENAL[s]} / {ETIQUETAS_ESTIMADOR[e]}" for s, e in columnas]
    lineas = ["| " + " | ".join(encabezado) + " |", "|" + "---|" * len(encabezado)]
    errores = {columna: [] for columna in columnas + [("modulo", "")]}
    for nombre, conteo, modulo, por_senal in resultados:
        celdas = [nombre.split("_", 2)[2], f"{conteo:g}", f"{modulo:.1f} ({modulo - conteo:+.1f})"]
        errores[("modulo", "")].append(abs(modulo - conteo))
        for s, e in columnas:
            estimado = por_senal[s][e]
            errores[(s, e)].append(abs(estimado - conteo))
            celdas.append(f"{estimado:.1f} ({estimado - conteo:+.1f})")
        lineas.append("| " + " | ".join(celdas) + " |")
    mae = ["**Error absoluto medio**", "", f"**{np.mean(errores[('modulo', '')]):.1f}**"]
    mae += [f"**{np.mean(errores[c]):.1f}**" for c in columnas]
    lineas.append("| " + " | ".join(mae) + " |")
    return "\n".join(lineas) + "\n"


def main():
    CARPETA_SALIDA.mkdir(parents=True, exist_ok=True)
    cargadas = [(nombre, conteo, tramo, er.cargar_csv(CARPETA_CRUDOS / f"{nombre}.csv"))
                for nombre, conteo, tramo in CAPTURAS]

    resultados = []
    for nombre, conteo, tramo, datos in cargadas:
        en_tramo = (datos["t"] >= tramo[0]) & (datos["t"] <= tramo[1])
        modulo = float(np.median(datos["resp_rpm"][en_tramo]))
        por_senal = {senal: estimar_en_tramo(datos, senal, tramo) for senal in er.SENALES}
        resultados.append((nombre, conteo, modulo, por_senal))

    texto = [
        "# Validación de la estimación de respiración",
        "",
        f"Mediana de las ventanas de {er.VENTANA_S:g} s dentro del tramo del conteo. Rango "
        f"{er.RANGO_RPM[0]:g}-{er.RANGO_RPM[1]:g} rpm, pasabanda {er.BANDA_HZ[0]:g}-{er.BANDA_HZ[1]:g} Hz, "
        f"umbral de subarmónico {er.UMBRAL_SUBARMONICO:g}, histéresis {er.FRACCION_HISTERESIS:g} × RMS. "
        "Entre paréntesis, el error contra el conteo (rpm).",
        "",
        tabla_errores(resultados),
        "RMS mediano de la señal filtrada en el tramo:",
        "",
    ]
    for nombre, _, _, por_senal in resultados:
        rms = ", ".join(f"{ETIQUETAS_SENAL[s]} {por_senal[s]['rms']:.3f}" for s in er.SENALES)
        texto.append(f"- {nombre.split('_', 2)[2]}: {rms}")
    contenido = "\n".join(texto) + "\n"
    (CARPETA_SALIDA / "errores.md").write_text(contenido, encoding="utf-8", newline="\n")
    graficar_espectros(CARPETA_SALIDA / "espectros.png", cargadas[1:] + cargadas[:1])

    print(contenido)
    print(f"Resultados en {CARPETA_SALIDA}")


if __name__ == "__main__":
    main()
