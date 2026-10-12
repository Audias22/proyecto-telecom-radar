"""Evalúa el detector de detector_apnea.py contra un criterio de éxito fijado de antemano.

Uso:
    python evaluar_detector_apnea.py

Criterio de éxito (fijado antes de ver resultados):
    - detectar la apnea a lo sumo MAX_RETRASO_S segundos después de su inicio, y
    - como máximo MAX_FALSAS_POR_10MIN falsas alarmas cada 10 minutos de respiración normal.

Validación cruzada dejando una apnea fuera: con las otras dos se elige la configuración
(medida, ventana, umbral, apoyo del módulo) que las detecta a tiempo sin falsas alarmas en esas
mismas capturas y con el menor retraso; luego se prueba en la apnea que quedó fuera y en TODAS
las capturas sin apnea (que no se usan para elegir). Las falsas alarmas cuentan en todas, incluidas
pared y cama vacía; el tiempo de referencia son los minutos con persona después de los 30 s.

También se reporta la mejor configuración viendo las tres apneas a la vez (cota optimista: si ni
así se cumple el criterio, con estos datos no se puede).

Salidas en pruebas/resultados/deteccion_apnea_v2/: evaluacion.md y relacion.png.
"""

import itertools
from pathlib import Path

import numpy as np

import detector_apnea as da
import estimar_respiracion as er

MAX_RETRASO_S = 30.0
MAX_FALSAS_POR_10MIN = 1.0
TOLERANCIA_INICIO_S = 2.0   # la caída que dispara la alarma debe empezar dentro de la apnea
INICIO_NORMAL_S = 30.0      # los primeros 30 s de cada captura son de acomodo

REJILLA = {
    "medida": da.MEDIDAS,
    "ventana_s": (5.0, 10.0),
    "umbral": (0.3, 0.4, 0.5, 0.6, 0.7, 0.8),
    "usar_modulo": (False, True),
}

CARPETA_CRUDOS = Path(__file__).resolve().parent.parent / "pruebas" / "crudos"
CARPETA_SALIDA = er.CARPETA_RESULTADOS / "deteccion_apnea_v2"

APNEAS = {
    "acostado1": ("2026-10-09_001540_acostado_apnea1_50cm", (45.0, 75.0)),
    "acostado2": ("2026-10-09_001918_acostado_apnea2_50cm", (45.0, 75.0)),
    "sentado": ("2026-10-07_224314_apnea_84cm", (45.0, 65.0)),
}
SIN_APNEA = [
    ("acostado", "2026-10-08_235538_acostado_8rpm_50cm"),
    ("acostado", "2026-10-08_235002_acostado_15rpm_50cm"),
    ("acostado", "2026-10-09_001141_acostado_30rpm_50cm"),
    ("acostado", "2026-10-09_000001_acostado_variable_rapido_a_lento_50cm"),
    ("sentado", "2026-10-06_172200_persona_sentada_84cm"),
    ("sentado", "2026-10-07_222846_normal_84cm"),
    ("sentado", "2026-10-07_223440_lenta_84cm"),
    ("sentado", "2026-10-07_223850_rapida_84cm"),
    ("sentado", "2026-10-06_161758_persona_sentada_80cm"),
    ("sentado", "2026-10-06_162052_persona_frente_computadora_50cm"),
    ("sin persona", "2026-10-09_002233_cama_vacia_50cm"),
    ("sin persona", "2026-10-06_163155_sin_nadie_apuntando_pared"),
]


def configuraciones():
    claves = list(REJILLA)
    for valores in itertools.product(*(REJILLA[c] for c in claves)):
        yield dict(zip(claves, valores))


def nombre_config(c):
    apoyo = " + módulo ≤ 8 rpm" if c["usar_modulo"] else ""
    return f"{c['medida']}, ventana {c['ventana_s']:g} s, umbral {c['umbral']:g}{apoyo}"


class Evaluador:
    """Guarda las medidas por captura y ventana para no recalcularlas en cada configuración."""

    def __init__(self):
        self.datos = {}
        for clave, (archivo, _) in APNEAS.items():
            self.datos[clave] = er.cargar_csv(CARPETA_CRUDOS / f"{archivo}.csv")
        for _, archivo in SIN_APNEA:
            self.datos[archivo] = er.cargar_csv(CARPETA_CRUDOS / f"{archivo}.csv")
        self.medidas = {}

    def medidas_de(self, captura, ventana_s):
        clave = (captura, ventana_s)
        if clave not in self.medidas:
            self.medidas[clave] = da.calcular_medidas(self.datos[captura], ventana_s)
        return self.medidas[clave]

    def correr(self, captura, config):
        medidas = self.medidas_de(captura, config["ventana_s"])
        return da.correr(medidas, config["medida"], config["umbral"], config["usar_modulo"],
                         config.get("duracion_min_s", da.DURACION_MIN_S))

    def apnea(self, clave, config):
        """(retraso de la detección o None, instantes de falsas alarmas) en una captura con apnea."""
        alarmas, _, _ = self.correr(clave, config)
        inicio, fin = APNEAS[clave][1]
        retraso = None
        falsas = []
        for instante, desde in alarmas:
            verdadera = desde >= inicio - TOLERANCIA_INICIO_S and instante <= fin + 15.0
            if verdadera and retraso is None:
                retraso = instante - inicio
            elif not verdadera:
                falsas.append(instante)
        return retraso, falsas

    def sin_apnea(self, config):
        """Por captura: (instantes de falsas alarmas, segundos en alarma)."""
        resultado = {}
        for _, archivo in SIN_APNEA:
            alarmas, segundos, _ = self.correr(archivo, config)
            resultado[archivo] = ([a[0] for a in alarmas], segundos)
        return resultado

    def minutos_persona(self):
        return sum(self.datos[a]["t"][-1] - INICIO_NORMAL_S for g, a in SIN_APNEA if g != "sin persona") / 60.0


def a_tiempo(retraso):
    return retraso is not None and retraso <= MAX_RETRASO_S


def elegir(evaluador, entrenamiento):
    """Configuración que detecta a tiempo las apneas de entrenamiento sin falsas alarmas en esas
    capturas, con el menor retraso total; en empate, la de umbral más bajo (más conservadora)."""
    candidatas = []
    for config in configuraciones():
        resultados = [evaluador.apnea(clave, config) for clave in entrenamiento]
        if all(a_tiempo(r) and not f for r, f in resultados):
            candidatas.append((sum(r for r, _ in resultados), config["umbral"], nombre_config(config), config))
    if not candidatas:
        return None
    return min(candidatas, key=lambda c: (c[0], c[1], c[2]))[3]


def evaluar_config(evaluador, config, claves_apnea):
    """Resultados de una configuración: apneas pedidas y falsas alarmas sin apnea."""
    apneas = {clave: evaluador.apnea(clave, config) for clave in claves_apnea}
    sin = evaluador.sin_apnea(config)
    falsas = sum(len(f) for f, _ in sin.values()) + sum(len(f) for _, f in apneas.values())
    segundos = sum(s for _, s in sin.values())
    tasa = falsas / evaluador.minutos_persona() * 10.0
    return apneas, sin, falsas, segundos, tasa


def texto_apnea(retraso, falsas):
    texto = "**no detecta**" if retraso is None else f"detecta a los {retraso:+.0f} s del inicio"
    if retraso is not None and retraso > MAX_RETRASO_S:
        texto += " (tarde)"
    if falsas:
        texto += f"; {len(falsas)} falsa(s) en esa captura"
    return texto


def graficar(ruta, evaluador, ventana_s):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figura, ejes = plt.subplots(len(APNEAS), 1, figsize=(12, 3.0 * len(APNEAS)), sharex=True)
    for eje, (clave, (archivo, intervalo)) in zip(ejes, APNEAS.items()):
        medidas = evaluador.medidas_de(clave, ventana_s)
        for medida, color in zip(da.MEDIDAS, ("C2", "C4", "C1")):
            _, _, referencias = da.correr(medidas, medida, 0.5)
            eje.plot(medidas["t"], medidas[medida] / referencias, color=color, lw=1.0, label=medida)
        eje.axvspan(*intervalo, color="C3", alpha=0.12)
        eje.axhline(0.5, color="0.4", ls="--", lw=0.8)
        eje.set_ylim(0, 2.5)
        eje.set_ylabel(f"{clave}\nmedida / referencia")
        eje.grid(True, alpha=0.3)
        eje.legend(loc="upper right", fontsize=7)
    ejes[-1].set_xlabel("tiempo (s)")
    figura.suptitle(f"Medida / referencia (ventana {ventana_s:g} s); franja: apnea; línea: 0.5")
    figura.tight_layout(rect=(0, 0, 1, 0.97))
    figura.savefig(ruta, dpi=100)
    plt.close(figura)


def main():
    CARPETA_SALIDA.mkdir(parents=True, exist_ok=True)
    evaluador = Evaluador()
    minutos = evaluador.minutos_persona()
    lineas = [
        "# Detector de apnea v2: evaluación contra el criterio de éxito",
        "",
        f"Criterio (fijado de antemano): detectar en ≤ {MAX_RETRASO_S:g} s desde el inicio de la apnea y "
        f"≤ {MAX_FALSAS_POR_10MIN:g} falsa alarma cada 10 min de respiración normal. Duración mínima de la "
        f"caída {da.DURACION_MIN_S:g} s; referencia = mediana de los últimos {da.REFERENCIA_S:g} s normales; "
        f"se arma tras {da.ARMADO_HISTORIA_S:g} s de respiración (actividad ≥ {da.ACTIVIDAD_MIN_PERSONA:g}); "
        f"la referencia se reaprende tras {da.DESCONGELAR_S:g} s bajo.",
        "",
        f"Falsas alarmas en {len(SIN_APNEA)} capturas sin apnea ({minutos:.1f} min con persona después de los "
        "30 s; pared y cama vacía cuentan alarmas pero no minutos) más las de las capturas de apnea fuera "
        "del intervalo.",
        "",
        "## Validación cruzada (una apnea fuera cada vez)",
        "",
        "| Apnea de prueba | Configuración elegida con las otras dos | Apnea de prueba | Falsas alarmas | Por 10 min | Segundos en alarma | ¿Cumple? |",
        "|---|---|---|---|---|---|---|",
    ]
    cumplen = 0
    for prueba in APNEAS:
        entrenamiento = [c for c in APNEAS if c != prueba]
        config = elegir(evaluador, entrenamiento)
        if config is None:
            lineas.append(f"| {prueba} | ninguna configuración detecta a tiempo las dos de entrenamiento sin falsas alarmas | – | – | – | – | **no** |")
            continue
        apneas, _, falsas, segundos, tasa = evaluar_config(evaluador, config, list(APNEAS))
        retraso, falsas_prueba = apneas[prueba]
        cumple = a_tiempo(retraso) and tasa <= MAX_FALSAS_POR_10MIN
        cumplen += int(cumple)
        lineas.append(f"| {prueba} | {nombre_config(config)} | {texto_apnea(retraso, falsas_prueba)} | {falsas} | "
                      f"{tasa:.1f} | {segundos:.0f} | {'**sí**' if cumple else '**no**'} |")
    lineas += ["", f"Cumple el criterio en {cumplen} de {len(APNEAS)} validaciones.", ""]

    # Cota optimista: configuraciones que detectan las tres a tiempo, la de menos falsas alarmas.
    lineas += ["## Cota optimista (eligiendo viendo las tres apneas)", ""]
    candidatas = []
    for config in configuraciones():
        apneas, _, falsas, segundos, tasa = evaluar_config(evaluador, config, list(APNEAS))
        detectadas = sum(a_tiempo(r) for r, _ in apneas.values())
        candidatas.append((-detectadas, falsas, segundos, nombre_config(config), config, apneas, tasa))
    candidatas.sort(key=lambda c: c[:4])
    lineas += ["Las 8 mejores configuraciones (más apneas detectadas a tiempo, luego menos falsas alarmas):", "",
               "| Configuración | Apneas a tiempo | acostado1 | acostado2 | sentado | Falsas alarmas | Por 10 min |",
               "|---|---|---|---|---|---|---|"]
    for menos_det, falsas, segundos, nombre, _, apneas, tasa in candidatas[:8]:
        celdas = [("–" if r is None else f"{r:+.0f} s") for r, _ in apneas.values()]
        lineas.append(f"| {nombre} | {-menos_det}/3 | " + " | ".join(celdas) + f" | {falsas} | {tasa:.1f} |")
    mejor_tres = [c for c in candidatas if c[0] == -3]
    if mejor_tres:
        b = mejor_tres[0]
        lineas += ["", f"Mejor configuración que detecta las tres a tiempo: {b[3]}, con {b[1]} falsas alarmas "
                       f"({b[6]:.1f} cada 10 min)."]
    else:
        lineas += ["", "Ninguna configuración de la rejilla detecta las tres apneas a tiempo."]

    contenido = "\n".join(lineas) + "\n"
    (CARPETA_SALIDA / "evaluacion.md").write_text(contenido, encoding="utf-8", newline="\n")
    graficar(CARPETA_SALIDA / "relacion.png", evaluador, 10.0)
    print(contenido)
    print(f"Resultados en {CARPETA_SALIDA}")


if __name__ == "__main__":
    main()
