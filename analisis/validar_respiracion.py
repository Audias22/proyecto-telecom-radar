"""Valida los estimadores de respiración contra las capturas con conteo manual.

Uso:
    python validar_respiracion.py

Para cada captura de CAPTURAS y cada señal (fase_resp, fase_total desenvuelta, fase_total
integrada) estima la frecuencia con los tres estimadores de estimar_respiracion.py, usando la
mediana de las ventanas de 30 s que caen completas dentro del tramo del conteo manual, y la
compara con el conteo. Los parámetros son los mismos para todas las capturas.

También analiza CAPTURA_VARIABLE (ritmo que baja de rápido a lento, sin referencia exacta): grafica
la frecuencia estimada en el tiempo y calcula su pendiente, que debería ser negativa.

Salidas en pruebas/resultados/validacion_respiracion/:
    espectros.png  espectro de cada señal en el tramo del conteo, con el conteo marcado
    variable.png   frecuencia estimada en el tiempo en la captura de ritmo variable
    errores.md     tabla de estimaciones y errores por captura, error absoluto medio por
                   posición y pendientes de la captura de ritmo variable
"""

from pathlib import Path

import numpy as np
from scipy import signal

import estimar_respiracion as er

CARPETA_CRUDOS = Path(__file__).resolve().parent.parent / "pruebas" / "crudos"
CARPETA_SALIDA = er.CARPETA_RESULTADOS / "validacion_respiracion"

# (captura, posición, referencia en rpm, tramo en s desde el inicio; None = hasta el final).
# Sentado: conteo manual. Acostado: pacer de audio desde los 30 s.
CAPTURAS = [
    ("2026-10-06_172200_persona_sentada_84cm", "sentado", 15.5, (0.0, None)),
    ("2026-10-07_222846_normal_84cm", "sentado", 14.0, (30.0, 90.0)),
    ("2026-10-07_223440_lenta_84cm", "sentado", 8.0, (30.0, 90.0)),
    ("2026-10-07_223850_rapida_84cm", "sentado", 31.0, (30.0, 90.0)),
    ("2026-10-08_235538_acostado_8rpm_50cm", "acostado", 8.0, (30.0, None)),
    ("2026-10-08_235002_acostado_15rpm_50cm", "acostado", 15.0, (30.0, None)),
    ("2026-10-09_001141_acostado_30rpm_50cm", "acostado", 30.0, (30.0, None)),
]

# Sin pacer: 7 respiraciones en 0-30 s y 30 en los 2 minutos siguientes, de rápido a lento.
CAPTURA_VARIABLE = "2026-10-09_000001_acostado_variable_rapido_a_lento_50cm"
TRAMO_VARIABLE = (30.0, None)
PROMEDIO_VARIABLE_RPM = 15.0

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




def resolver_tramo(datos, tramo):
    """Reemplaza el final None por la duración de la captura."""
    return tramo[0], datos["t"][-1] if tramo[1] is None else tramo[1]


def nombre_corto(nombre):
    return nombre.split("_", 2)[2]


def graficar_espectros(ruta_png, cargadas):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    filas = len(cargadas)
    figura, ejes = plt.subplots(filas, len(er.SENALES), figsize=(15, 2.8 * filas), sharex=True)
    for i, (nombre, posicion, referencia, tramo, datos) in enumerate(cargadas):
        for j, senal in enumerate(er.SENALES):
            eje = ejes[i, j]
            rpm, magnitud = espectro_en_tramo(datos, senal, tramo)
            eje.plot(rpm, magnitud, color="C0", linewidth=1.0)
            eje.axvspan(*er.RANGO_RPM, color="0.92", zorder=0)
            eje.axvline(referencia, color="C3", linewidth=1.5, label="referencia (conteo o pacer)")
            eje.axvline(2 * referencia, color="C3", linewidth=1.0, linestyle=":", label="2 × referencia")
            eje.set_xlim(0, 65)
            eje.set_ylim(0, 1.15)
            if i == 0:
                eje.set_title(ETIQUETAS_SENAL[senal])
            if j == 0:
                eje.set_ylabel(f"{nombre_corto(nombre)}\n{posicion}, {referencia:g} rpm\n"
                               f"{tramo[0]:g}-{tramo[1]:.0f} s", fontsize=8)
            if i == filas - 1:
                eje.set_xlabel("rpm")
            if i == 0 and j == 0:
                eje.legend(loc="upper right", fontsize=7)
            eje.grid(True, alpha=0.3)
    figura.suptitle("Espectro en el tramo de referencia (zona gris: rango buscado 6-35 rpm; "
                    "magnitud normalizada al máximo del rango)")
    figura.tight_layout(rect=(0, 0, 1, 0.98))
    figura.savefig(ruta_png, dpi=90)
    plt.close(figura)


def tabla_errores(resultados):
    """Tabla markdown: una fila por captura y columnas por señal/estimador, más el error
    absoluto medio por posición y total."""
    columnas = [(s, e) for s in er.SENALES for e in er.ESTIMADORES]
    encabezado = (["Captura", "Posición", "Referencia", "Módulo"]
                  + [f"{ETIQUETAS_SENAL[s]} / {ETIQUETAS_ESTIMADOR[e]}" for s, e in columnas])
    lineas = ["| " + " | ".join(encabezado) + " |", "|" + "---|" * len(encabezado)]
    errores = []  # (posición, {columna: error absoluto})
    for nombre, posicion, referencia, modulo, por_senal in resultados:
        fila = {("modulo", ""): abs(modulo - referencia)}
        celdas = [nombre_corto(nombre), posicion, f"{referencia:g}", f"{modulo:.1f} ({modulo - referencia:+.1f})"]
        for s, e in columnas:
            estimado = por_senal[s][e]
            fila[(s, e)] = abs(estimado - referencia)
            celdas.append(f"{estimado:.1f} ({estimado - referencia:+.1f})")
        errores.append((posicion, fila))
        lineas.append("| " + " | ".join(celdas) + " |")

    for grupo in ("sentado", "acostado", None):
        filas = [f for p, f in errores if grupo is None or p == grupo]
        etiqueta = f"**Error absoluto medio, {grupo or 'todas'}**"
        celdas = [etiqueta, "", ""] + [f"**{np.mean([f[c] for f in filas]):.1f}**"
                                       for c in [("modulo", "")] + columnas]
        lineas.append("| " + " | ".join(celdas) + " |")
    return "\n".join(lineas) + "\n"


def analizar_variable(datos):
    """Frecuencia estimada en el tiempo y su pendiente (rpm/min) por señal y estimador."""
    tramo = resolver_tramo(datos, TRAMO_VARIABLE)
    series = {}
    for senal in er.SENALES:
        t, x = er.senal_respiracion(datos, senal)
        _, _, _, ventanas = er.procesar(t, x, datos["resp_rpm"], datos["t"])
        dentro = (ventanas["t_ini"] >= tramo[0] - 0.5) & (ventanas["t_fin"] <= tramo[1] + 0.5)
        for nombre in er.ESTIMADORES:
            t_fin = ventanas["t_fin"][dentro]
            rpm = ventanas[f"{nombre}_rpm"][dentro]
            validos = ~np.isnan(rpm)
            pendiente = 60.0 * np.polyfit(t_fin[validos], rpm[validos], 1)[0]
            series[(senal, nombre)] = (t_fin, rpm, pendiente)
        series[("modulo", "")] = (ventanas["t_fin"][dentro], ventanas["modulo_rpm"][dentro],
                                  60.0 * np.polyfit(ventanas["t_fin"][dentro], ventanas["modulo_rpm"][dentro], 1)[0])
    return series


def graficar_variable(ruta_png, series):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figura, ejes = plt.subplots(len(er.SENALES), 1, figsize=(11, 3.2 * len(er.SENALES)), sharex=True)
    for eje, senal in zip(ejes, er.SENALES):
        t_mod, rpm_mod, _ = series[("modulo", "")]
        eje.plot(t_mod, rpm_mod, color="0.5", linewidth=1.0, label="módulo (mediana por ventana)")
        for color, nombre in zip(("C1", "C5", "C2"), er.ESTIMADORES):
            t_fin, rpm, pendiente = series[(senal, nombre)]
            eje.plot(t_fin, rpm, "o-", color=color, markersize=2.5,
                     label=f"{ETIQUETAS_ESTIMADOR[nombre]} (pendiente {pendiente:+.1f} rpm/min)")
        eje.axhline(PROMEDIO_VARIABLE_RPM, color="C3", linestyle="--",
                    label=f"promedio contado ({PROMEDIO_VARIABLE_RPM:g} rpm, bajando de rápido a lento)")
        eje.set_title(ETIQUETAS_SENAL[senal])
        eje.set_ylabel("rpm")
        eje.set_ylim(0, er.RANGO_RPM[1] + 5)
        eje.legend(loc="upper right", fontsize=7)
        eje.grid(True, alpha=0.3)
    ejes[-1].set_xlabel("tiempo (s), final de cada ventana de 30 s")
    figura.suptitle(f"{nombre_corto(CAPTURA_VARIABLE)}: el ritmo real baja de rápido a lento desde los 30 s")
    figura.tight_layout()
    figura.savefig(ruta_png, dpi=100)
    plt.close(figura)


def main():
    CARPETA_SALIDA.mkdir(parents=True, exist_ok=True)
    cargadas = []
    for nombre, posicion, referencia, tramo in CAPTURAS:
        datos = er.cargar_csv(CARPETA_CRUDOS / f"{nombre}.csv")
        cargadas.append((nombre, posicion, referencia, resolver_tramo(datos, tramo), datos))

    resultados = []
    for nombre, posicion, referencia, tramo, datos in cargadas:
        en_tramo = (datos["t"] >= tramo[0]) & (datos["t"] <= tramo[1])
        modulo = float(np.median(datos["resp_rpm"][en_tramo]))
        por_senal = {senal: estimar_en_tramo(datos, senal, tramo) for senal in er.SENALES}
        resultados.append((nombre, posicion, referencia, modulo, por_senal))

    series = analizar_variable(er.cargar_csv(CARPETA_CRUDOS / f"{CAPTURA_VARIABLE}.csv"))

    texto = [
        "# Validación de la estimación de respiración",
        "",
        f"Mediana de las ventanas de {er.VENTANA_S:g} s dentro del tramo de referencia. Rango "
        f"{er.RANGO_RPM[0]:g}-{er.RANGO_RPM[1]:g} rpm, pasabanda {er.BANDA_HZ[0]:g}-{er.BANDA_HZ[1]:g} Hz, "
        f"umbral de subarmónico {er.UMBRAL_SUBARMONICO:g}, histéresis {er.FRACCION_HISTERESIS:g} × RMS. "
        "Referencia: conteo manual (sentado) o pacer (acostado). Entre paréntesis, el error (rpm).",
        "",
        tabla_errores(resultados),
        "RMS mediano de la señal filtrada en el tramo:",
        "",
    ]
    for nombre, posicion, _, _, por_senal in resultados:
        rms = ", ".join(f"{ETIQUETAS_SENAL[s]} {por_senal[s]['rms']:.3f}" for s in er.SENALES)
        texto.append(f"- {nombre_corto(nombre)} ({posicion}): {rms}")
    texto += [
        "",
        f"## Ritmo variable ({nombre_corto(CAPTURA_VARIABLE)})",
        "",
        "Pendiente de la frecuencia estimada entre los 30 s y el final (el ritmo real baja, así que "
        "debería ser claramente negativa):",
        "",
        "| Señal | Estimador | Pendiente (rpm/min) | Mediana (rpm) |",
        "|---|---|---|---|",
    ]
    for (senal, nombre), (_, rpm, pendiente) in series.items():
        etiqueta_senal = "módulo" if senal == "modulo" else ETIQUETAS_SENAL[senal]
        etiqueta_est = "" if senal == "modulo" else ETIQUETAS_ESTIMADOR[nombre]
        texto.append(f"| {etiqueta_senal} | {etiqueta_est} | {pendiente:+.1f} | {np.nanmedian(rpm):.1f} |")

    contenido = "\n".join(texto) + "\n"
    (CARPETA_SALIDA / "errores.md").write_text(contenido, encoding="utf-8", newline="\n")
    graficar_espectros(CARPETA_SALIDA / "espectros.png", cargadas)
    graficar_variable(CARPETA_SALIDA / "variable.png", series)

    print(contenido)
    print(f"Resultados en {CARPETA_SALIDA}")


if __name__ == "__main__":
    main()
