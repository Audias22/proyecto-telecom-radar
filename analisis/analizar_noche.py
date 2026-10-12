"""Analiza una noche completa de sueño con el radar (docs/radar.md, sección 13).

Uso:
    python analizar_noche.py ../pruebas/crudos/2026-10-12_024855_noche_cama.csv

Qué hace (nada de esto ajusta parámetros con la noche; todos vienen de las secciones 11-12):
    1. Vista por minuto: tasa de tramas, presencia, distancia, actividad, resp_rpm y latido_bpm,
       con los eventos anotados y los huecos de la captura. Busca el cambio de nivel de distancia
       más grande (posible movimiento del sensor).
    2. Segmentos: cuarto vacío, persona de pie, en cama despierta y dormida (ver segmentar()).
    3. Criterio de cama vacía: actividad de 30 s >= ACTIVIDAD_MIN_PERSONA (0.03) => persona.
    4. Detector de apnea v2 sin cambios (actividad, ventana 10 s, umbral 0.7) con duración
       mínima de 20 s y, como variantes fijadas de antemano, 25 y 30 s. Falsas alarmas por hora
       de sueño y su contexto; detección de las 3 apneas de las capturas anteriores.
    5. Respiración durante el sueño: resp_rpm del módulo y estimación propia (FFT y cruces de
       fase_resp en ventanas de 30 s).

Salidas en pruebas/resultados/noche_<captura>/: noche.png, alarmas.png, respiracion.png y
resumen.md.
"""

import argparse
import csv
from pathlib import Path

import numpy as np

import detector_apnea as da
import estimar_respiracion as er
import evaluar_detector_apnea as eva

# --- Parámetros (fijados antes de ver los resultados) ---------------------------------------

# Eventos anotados por número de lectura (fila de datos, desde 0). Aproximados.
EVENTOS = (
    (3100, "sale"),
    (5800, "vuelve (de pie a un costado)"),
    (7800, "sale"),
    (9600, "vuelve"),
    (9900, "se acuesta"),
)
HUECO_CAPTURA_S = 5.0        # sin filas durante más de esto: hueco de la captura (no del radar)
MARGEN_TRANSICION_S = 30.0   # alrededor de cada evento no se evalúa (eventos aproximados + ventana)

# Inicio del sueño: primer tramo de MINUTOS_QUIETO minutos seguidos, después de acostarse, sin
# ningún minuto con actividad mediana > ACTIVIDAD_DESPIERTO (los minutos sin objetivo no cuentan
# como movimiento). 0.1 está por encima de la respiración tranquila acostado (0.04-0.07,
# sección 11) y por debajo del movimiento al acomodarse (0.2-0.3).
ACTIVIDAD_DESPIERTO = 0.1
MINUTOS_QUIETO = 10
FINAL_DESPIERTO_S = 60.0     # el último minuto (despierta y detiene la captura) no es sueño

VENTANA_PERSONA_S = 30.0     # ventana de la actividad para el criterio de cama vacía

# Detector v2 (sección 12.4), sin cambios; solo varía la duración mínima.
DETECTOR = {"medida": "actividad", "ventana_s": 10.0, "umbral": 0.7, "usar_modulo": False}
DURACIONES_S = (20.0, 25.0, 30.0)

# Contexto de cada falsa alarma, mirando desde CONTEXTO_S antes del inicio de la caída:
#   sin objetivo  hubo segundos sin dato de fase (el radar perdió el objetivo)
#   movimiento    la actividad de 10 s pasó de FACTOR_MOVIMIENTO × la referencia
#   quieto        nada de lo anterior
CONTEXTO_S = 60.0
FACTOR_MOVIMIENTO = 2.0

SEGMENTOS = ("vacío", "de pie", "en cama despierta", "dormida")
COLOR_SEGMENTO = {"vacío": "0.85", "de pie": "C1", "en cama despierta": "C3", "dormida": "C0"}


# --- Lectura ---------------------------------------------------------------------------------

def leer_ms_filas(ruta):
    """ms de cada fila de datos (para pasar los eventos de número de lectura a tiempo)."""
    with open(ruta, encoding="utf-8") as archivo:
        filas = (linea for linea in archivo if not linea.startswith("#"))
        return np.array([int(fila["ms"]) for fila in csv.DictReader(filas)], dtype=np.int64)


def huecos(t):
    """Intervalos (inicio, fin) sin filas durante más de HUECO_CAPTURA_S."""
    dt = np.diff(t)
    return [(t[i], t[i + 1]) for i in np.flatnonzero(dt > HUECO_CAPTURA_S)]


def bloques(datos):
    """Parte la captura en tramos continuos (sin huecos); cada uno con t desde 0 y su desfase."""
    cortes = np.flatnonzero(np.diff(datos["t"]) > HUECO_CAPTURA_S) + 1
    inicios = np.concatenate(([0], cortes))
    fines = np.concatenate((cortes, [len(datos["t"])]))
    for i, f in zip(inicios, fines):
        bloque = {clave: valores[i:f] for clave, valores in datos.items()}
        desfase = bloque["t"][0]
        bloque["t"] = bloque["t"] - desfase
        yield bloque, desfase


def medidas_por_segundo(datos, ventana_s):
    """calcular_medidas por bloque continuo, con los tiempos de la captura completa."""
    partes = []
    for bloque, desfase in bloques(datos):
        if bloque["t"][-1] < ventana_s + 1:
            continue
        m = da.calcular_medidas(bloque, ventana_s)
        m["t"] = m["t"] + desfase
        partes.append(m)
    return partes


def unir(partes, clave):
    return np.concatenate([p[clave] for p in partes])


# --- 1. Vista por minuto -----------------------------------------------------------------------

def por_minuto(datos):
    """Resumen por minuto. Minutos sin filas (huecos) quedan en NaN."""
    t = datos["t"]
    minuto = (t // 60).astype(int)
    n = minuto[-1] + 1
    r = {c: np.full(n, np.nan) for c in ("tasa", "presencia", "distancia", "actividad", "resp", "latido")}
    r["t"] = np.arange(n) * 60.0 + 30.0
    orden = np.searchsorted(minuto, np.arange(n + 1))
    for k in range(n):
        s = slice(orden[k], orden[k + 1])
        if orden[k + 1] == orden[k]:
            continue
        tt = t[s]
        r["tasa"][k] = (len(tt) - 1) / (tt[-1] - tt[0]) if len(tt) > 1 and tt[-1] > tt[0] else np.nan
        r["presencia"][k] = np.mean(datos["presencia"][s])
        con = datos["presencia"][s] > 0.5
        dist = datos["distancia_cm"][s][con]
        dist = dist[~np.isnan(dist)]
        r["distancia"][k] = np.median(dist) if len(dist) else np.nan
        fase = datos["fase_total"][s]
        fase = fase[fase != 0.0]
        r["actividad"][k] = np.median(np.abs(fase)) if len(fase) else np.nan
        r["resp"][k] = np.median(datos["resp_rpm"][s][con]) if con.any() else np.nan
        r["latido"][k] = np.median(datos["latido_bpm"][s][con]) if con.any() else np.nan
    return r


def cambio_distancia(minutos, lado_min=30):
    """Minuto con el mayor salto entre la mediana de distancia de los lado_min minutos antes y
    después (solo minutos con distancia). Devuelve (t, antes, después)."""
    d = minutos["distancia"]
    mejor = (np.nan, np.nan, np.nan, 0.0)
    for k in range(lado_min, len(d) - lado_min):
        antes = d[k - lado_min:k]
        despues = d[k:k + lado_min]
        antes, despues = antes[~np.isnan(antes)], despues[~np.isnan(despues)]
        if len(antes) < lado_min // 3 or len(despues) < lado_min // 3:
            continue
        salto = abs(np.median(despues) - np.median(antes))
        if salto > mejor[3]:
            mejor = (k * 60.0, np.median(antes), np.median(despues), salto)
    return mejor[:3]


# --- 2. Segmentos ------------------------------------------------------------------------------

def segmentar(t_eventos, minutos, t_fin):
    """Lista de (inicio, fin, segmento, detalle).

    Antes del primer evento la persona está en el cuarto preparando la captura (de pie). Entre
    "vuelve" y "se acuesta" (19 s) cuenta como de pie. En cama despierta va de "se acuesta" al
    inicio del sueño (criterio en los parámetros); dormida, hasta FINAL_DESPIERTO_S antes del fin.
    """
    sale1, vuelve1, sale2, vuelve2, acuesta = t_eventos
    act = minutos["actividad"]
    k = int(np.ceil(acuesta / 60.0))
    inicio_sueno = None
    while k + MINUTOS_QUIETO <= len(act):
        tramo = act[k:k + MINUTOS_QUIETO]
        if not np.any(tramo[~np.isnan(tramo)] > ACTIVIDAD_DESPIERTO):
            inicio_sueno = k * 60.0
            break
        k += 1
    fin_sueno = t_fin - FINAL_DESPIERTO_S
    return [
        (0.0, sale1, "de pie", "preparación de la captura"),
        (sale1, vuelve1, "vacío", "sale del cuarto"),
        (vuelve1, sale2, "de pie", "de pie a ~1.5 m, a un costado"),
        (sale2, vuelve2, "vacío", "sale otra vez"),
        (vuelve2, acuesta, "de pie", "vuelve"),
        (acuesta, inicio_sueno, "en cama despierta", "acostada, con el celular"),
        (inicio_sueno, fin_sueno, "dormida", "boca abajo casi toda la noche"),
        (fin_sueno, t_fin, "en cama despierta", "despierta y detiene la captura"),
    ]


def etiqueta_por_segundo(t, segmentos, t_eventos, lista_huecos):
    """Segmento de cada instante; None en los márgenes de transición y en los huecos."""
    etiquetas = np.empty(len(t), dtype=object)
    for inicio, fin, segmento, _ in segmentos:
        etiquetas[(t >= inicio) & (t < fin)] = segmento
    cambios = list(t_eventos) + [s[0] for s in segmentos[5:]]
    for c in cambios:
        etiquetas[np.abs(t - c) < MARGEN_TRANSICION_S] = None
    for a, b in lista_huecos:
        etiquetas[(t >= a) & (t <= b + VENTANA_PERSONA_S)] = None
    return etiquetas


# --- 3. Cama vacía ------------------------------------------------------------------------------

def hay_persona(actividad_30s):
    """Criterio de la sección 11: actividad >= 0.03. Sin tramas con dato (radar sin objetivo),
    no hay persona."""
    return np.nan_to_num(actividad_30s, nan=0.0) >= da.ACTIVIDAD_MIN_PERSONA


# --- 4. Detector de apnea ------------------------------------------------------------------------

def correr_detector(partes, duracion_s):
    """Corre el detector en cada bloque continuo. Devuelve (alarmas, referencias por segundo)."""
    alarmas, referencias = [], []
    for m in partes:
        a, _, ref = da.correr(m, DETECTOR["medida"], DETECTOR["umbral"], DETECTOR["usar_modulo"], duracion_s)
        alarmas += a
        referencias.append(ref)
    return alarmas, np.concatenate(referencias)


def contexto_alarma(instante, desde, t, actividad, referencias):
    antes = (t >= desde - CONTEXTO_S) & (t <= instante)
    if np.any(np.isnan(actividad[antes])):
        return "sin objetivo"
    previo = (t >= desde - CONTEXTO_S) & (t <= desde)
    ref = referencias[np.searchsorted(t, desde)]
    if np.any(actividad[previo] > FACTOR_MOVIMIENTO * ref):
        return "movimiento"
    return "quieto"


def apneas_anteriores(duracion_s):
    """Retraso de detección en las 3 apneas y falsas alarmas en las 12 capturas cortas."""
    evaluador = eva.Evaluador()
    config = dict(DETECTOR, duracion_min_s=duracion_s)
    apneas, _, falsas, _, tasa = eva.evaluar_config(evaluador, config, list(eva.APNEAS))
    return {clave: r for clave, (r, _) in apneas.items()}, falsas, tasa


# --- 5. Respiración ------------------------------------------------------------------------------

def respiracion_en_ventanas(datos, en_sueno):
    """Ventanas de 30 s (paso 30 s) completas: sin huecos y con objetivo en todas las tramas.

    Devuelve dict con t_fin, modulo (mediana de resp_rpm), fft y cruces (rpm de fase_resp).
    """
    r = {"t_fin": [], "modulo": [], "fft": [], "cruces": []}
    sos = er.disenar_filtro()
    for bloque, desfase in bloques(datos):
        t = bloque["t"]
        if t[-1] < er.VENTANA_S:
            continue
        t_u, x_u = er.remuestrear(t, bloque["fase_resp"])
        presencia_u = np.interp(t_u, t, bloque["presencia"])
        for inicio in range(0, len(t_u) - er.MUESTRAS_VENTANA + 1, er.MUESTRAS_VENTANA):
            fin = inicio + er.MUESTRAS_VENTANA
            t_fin = t_u[fin - 1] + desfase
            if not en_sueno(t_fin - er.VENTANA_S) or not en_sueno(t_fin) or np.any(presencia_u[inicio:fin] < 1.0):
                continue
            ventana = er.filtrar(x_u[inicio:fin], sos)
            crudas = (t >= t_u[inicio]) & (t <= t_u[fin - 1])
            r["t_fin"].append(t_fin)
            r["modulo"].append(float(np.median(bloque["resp_rpm"][crudas])))
            r["fft"].append(60.0 * er.estimar_fft(ventana))
            r["cruces"].append(60.0 * er.estimar_cruces(ventana))
    return {c: np.array(v) for c, v in r.items()}


def describir(valores):
    v = valores[~np.isnan(valores)]
    p = np.percentile(v, (10, 25, 50, 75, 90))
    en_rango = np.mean((v >= 10) & (v <= 20)) * 100
    return (f"{len(v)} | {p[0]:.1f} | {p[1]:.1f} | **{p[2]:.1f}** | {p[3]:.1f} | {p[4]:.1f} | "
            f"{np.mean(v == 0) * 100:.0f} % | {np.mean((v > 0) & (v < 6)) * 100:.0f} % | {en_rango:.0f} %")


# --- Gráficas ------------------------------------------------------------------------------------

def _marcar(eje, segmentos, t_eventos, lista_huecos, escala):
    for inicio, fin, segmento, _ in segmentos:
        eje.axvspan(inicio / escala, fin / escala, color=COLOR_SEGMENTO[segmento], alpha=0.10, lw=0)
    for c in t_eventos:
        eje.axvline(c / escala, color="k", lw=0.6, ls=":")
    for a, b in lista_huecos:
        eje.axvspan(a / escala, b / escala, color="k", alpha=0.35, lw=0)


def graficar_noche(ruta, minutos, segmentos, t_eventos, lista_huecos, cambio):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch

    h = minutos["t"] / 3600.0
    paneles = (
        ("tasa", "tramas/s", None),
        ("presencia", "presencia\n(fracción)", (0, 1.05)),
        ("distancia", "distancia (cm)", None),
        ("actividad", "actividad\n(mediana |fase_total|)", None),
        ("resp", "resp_rpm módulo", (0, 30)),
        ("latido", "latido_bpm módulo", (0, 130)),
    )
    figura, ejes = plt.subplots(len(paneles), 1, figsize=(15, 14), sharex=True)
    for eje, (clave, etiqueta, limites) in zip(ejes, paneles):
        eje.plot(h, minutos[clave], ".-", ms=2.5, lw=0.7, color="C0")
        _marcar(eje, segmentos, t_eventos, lista_huecos, 3600.0)
        eje.set_ylabel(etiqueta)
        eje.grid(True, alpha=0.3)
        if limites:
            eje.set_ylim(*limites)
    ejes[3].set_yscale("log")
    ejes[3].axhline(da.ACTIVIDAD_MIN_PERSONA, color="C3", ls="--", lw=0.8)
    if not np.isnan(cambio[0]):
        ejes[2].axvline(cambio[0] / 3600.0, color="C3", lw=1.2)
    leyenda = [Patch(color=COLOR_SEGMENTO[s], alpha=0.3, label=s) for s in SEGMENTOS]
    leyenda.append(Patch(color="k", alpha=0.35, label="hueco de la captura"))
    ejes[0].legend(handles=leyenda, loc="upper right", ncol=5, fontsize=8)
    ejes[-1].set_xlabel("horas desde el inicio de la captura (líneas punteadas: eventos anotados)")
    figura.suptitle("Noche completa, por minuto (actividad en escala log; línea roja: umbral 0.03)")
    figura.tight_layout(rect=(0, 0, 1, 0.98))
    figura.savefig(ruta, dpi=90)
    plt.close(figura)


def graficar_alarmas(ruta, t, actividad, persona, alarmas_por_duracion, segmentos, t_eventos, lista_huecos):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figura, (ax_act, ax_al) = plt.subplots(2, 1, figsize=(15, 6), sharex=True,
                                           gridspec_kw={"height_ratios": (3, 1)})
    h = t / 3600.0
    ax_act.plot(h, actividad, lw=0.4, color="C0", label="actividad 10 s")
    ax_act.set_yscale("log")
    ax_act.set_ylim(1e-3, 3)
    ax_act.fill_between(h, 1e-3, 1.3e-3, where=~persona, color="C3", step="mid", label="criterio: sin persona")
    ax_act.legend(loc="upper right", fontsize=8)
    ax_act.set_ylabel("actividad")
    for k, (duracion, alarmas) in enumerate(alarmas_por_duracion.items()):
        x = [a[0] / 3600.0 for a in alarmas]
        ax_al.plot(x, np.full(len(x), k), "|", ms=12, mew=1.5, color=f"C{k + 1}")
    ax_al.set_yticks(range(len(alarmas_por_duracion)))
    ax_al.set_yticklabels([f"{d:g} s" for d in alarmas_por_duracion])
    ax_al.set_ylim(-0.7, len(alarmas_por_duracion) - 0.3)
    ax_al.set_ylabel("alarmas\n(duración mín.)")
    for eje in (ax_act, ax_al):
        _marcar(eje, segmentos, t_eventos, lista_huecos, 3600.0)
        eje.grid(True, alpha=0.3)
    ax_al.set_xlabel("horas desde el inicio de la captura")
    figura.suptitle("Detector v2 (actividad, 10 s, umbral 0.7) durante la noche")
    figura.tight_layout(rect=(0, 0, 1, 0.97))
    figura.savefig(ruta, dpi=90)
    plt.close(figura)


def graficar_respiracion(ruta, ventanas):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figura, (ax_t, ax_h) = plt.subplots(1, 2, figsize=(15, 4.5), gridspec_kw={"width_ratios": (3, 1)})
    h = ventanas["t_fin"] / 3600.0
    for clave, color in (("modulo", "0.4"), ("fft", "C1"), ("cruces", "C2")):
        ax_t.plot(h, ventanas[clave], ".", ms=3, color=color, label=clave)
        validos = ventanas[clave][~np.isnan(ventanas[clave])]
        ax_h.hist(validos, bins=np.arange(0, 37, 1), histtype="step", color=color, label=clave)
    for eje in (ax_t,):
        eje.axhspan(10, 20, color="C0", alpha=0.08)
        eje.set_ylim(-1, 36)
        eje.set_xlabel("horas desde el inicio de la captura")
        eje.set_ylabel("rpm")
    ax_h.axvspan(10, 20, color="C0", alpha=0.08)
    ax_h.set_xlabel("rpm")
    ax_h.set_ylabel("ventanas de 30 s")
    for eje in (ax_t, ax_h):
        eje.grid(True, alpha=0.3)
        eje.legend(fontsize=8)
    figura.suptitle("Respiración durante el sueño, ventanas de 30 s con objetivo (franja: 10-20 rpm)")
    figura.tight_layout(rect=(0, 0, 1, 0.95))
    figura.savefig(ruta, dpi=90)
    plt.close(figura)


# --- Programa principal ------------------------------------------------------------------------

def hms(segundos):
    s = int(round(segundos))
    return f"{s // 3600}:{s % 3600 // 60:02d}:{s % 60:02d}"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, help="captura de la noche (pruebas/crudos/)")
    args = parser.parse_args()

    ms = leer_ms_filas(args.csv)
    datos = er.cargar_csv(args.csv)
    t_eventos = [(ms[fila] - ms[0]) / 1000.0 for fila, _ in EVENTOS]
    t_fin = datos["t"][-1]
    lista_huecos = huecos(datos["t"])
    minutos = por_minuto(datos)
    cambio = cambio_distancia(minutos)
    segmentos = segmentar(t_eventos, minutos, t_fin)

    carpeta = er.CARPETA_RESULTADOS / f"noche_{args.csv.stem}"
    carpeta.mkdir(parents=True, exist_ok=True)

    L = [f"# Noche completa: {args.csv.stem}", "",
         f"Duración {hms(t_fin)} ({t_fin:.0f} s), {len(ms)} filas. Huecos de la captura (sin filas > "
         f"{HUECO_CAPTURA_S:g} s): {len(lista_huecos)}, {sum(b - a for a, b in lista_huecos):.0f} s en total.", ""]
    L += ["| Hueco | Desde | Duración (s) |", "|---|---|---|"]
    L += [f"| {i + 1} | {hms(a)} | {b - a:.0f} |" for i, (a, b) in enumerate(lista_huecos)]
    L += ["", "## Eventos", "", "| Lectura | Evento | Tiempo |", "|---|---|---|"]
    L += [f"| {fila} | {texto} | {hms(tt)} ({tt:.0f} s) |" for (fila, texto), tt in zip(EVENTOS, t_eventos)]
    L += ["", f"Mayor cambio de nivel de la distancia (mediana de 30 min antes y después): a las {hms(cambio[0])}, "
              f"de {cambio[1]:.0f} a {cambio[2]:.0f} cm.", ""]

    # 2. Segmentos
    L += ["## Segmentos", "", "| Segmento | Desde | Hasta | Duración | Detalle |", "|---|---|---|---|---|"]
    L += [f"| {s} | {hms(a)} | {hms(b)} | {(b - a) / 60:.1f} min | {d} |" for a, b, s, d in segmentos]
    sueno = [(a, b) for a, b, s, _ in segmentos if s == "dormida"][0]

    # 3. Cama vacía
    partes30 = medidas_por_segundo(datos, VENTANA_PERSONA_S)
    t30 = unir(partes30, "t")
    act30 = unir(partes30, "actividad")
    persona = hay_persona(act30)
    etiquetas = etiqueta_por_segundo(t30, segmentos, t_eventos, lista_huecos)
    L += ["", "## Criterio de cama vacía (actividad 30 s ≥ 0.03)", "",
          f"Por segundo, sin los ±{MARGEN_TRANSICION_S:g} s alrededor de cada cambio de segmento ni los huecos.", "",
          "| Segmento | Segundos evaluados | Esperado | Aciertos | Persona sin objetivo del radar | Persona con objetivo y actividad < 0.03 |",
          "|---|---|---|---|---|---|"]
    for segmento in SEGMENTOS:
        sel = etiquetas == segmento
        n = int(sel.sum())
        esperado = segmento != "vacío"
        aciertos = np.mean(persona[sel] == esperado) * 100
        if esperado:
            sin_obj = np.mean(np.isnan(act30[sel])) * 100
            bajo = np.mean(~np.isnan(act30[sel]) & ~persona[sel]) * 100
            extra = f"{sin_obj:.1f} % | {bajo:.1f} %"
        else:
            extra = "– | –"
        L.append(f"| {segmento} | {n} | {'persona' if esperado else 'vacío'} | **{aciertos:.1f} %** | {extra} |")

    # 4. Detector
    partes10 = medidas_por_segundo(datos, DETECTOR["ventana_s"])
    t10 = unir(partes10, "t")
    act10 = unir(partes10, "actividad")
    en_sueno = (t10 >= sueno[0]) & (t10 < sueno[1])
    horas_sueno = en_sueno.sum() / 3600.0
    horas_objetivo = (en_sueno & ~np.isnan(act10)).sum() / 3600.0
    L += ["", "## Detector de apnea v2 durante el sueño", "",
          f"Actividad, ventana 10 s, umbral 0.7, referencia 60 s; corre en cada tramo continuo de la captura. "
          f"Sueño con datos: {horas_sueno:.2f} h; con objetivo del radar: {horas_objetivo:.2f} h.", "",
          "| Duración mínima | Falsas alarmas en el sueño | Por hora de sueño | Por hora con objetivo | Movimiento | Quieto | Sin objetivo | Fuera del sueño |",
          "|---|---|---|---|---|---|---|---|"]
    alarmas_por_duracion = {}
    detalle = []
    for duracion in DURACIONES_S:
        alarmas, refs = correr_detector(partes10, duracion)
        alarmas_por_duracion[duracion] = alarmas
        dentro = [a for a in alarmas if sueno[0] <= a[0] < sueno[1]]
        contextos = [contexto_alarma(i, d, t10, act10, refs) for i, d in dentro]
        cuenta = {c: contextos.count(c) for c in ("movimiento", "quieto", "sin objetivo")}
        L.append(f"| {duracion:g} s | {len(dentro)} | **{len(dentro) / horas_sueno:.2f}** | "
                 f"{len(dentro) / horas_objetivo:.2f} | {cuenta['movimiento']} | {cuenta['quieto']} | "
                 f"{cuenta['sin objetivo']} | {len(alarmas) - len(dentro)} |")
        detalle.append((duracion, dentro, contextos))
    for duracion, dentro, contextos in detalle:
        L += ["", f"Alarmas con {duracion:g} s (instante; inicio de la caída; contexto): " +
              "; ".join(f"{hms(i)} ({hms(d)}, {c})" for (i, d), c in zip(dentro, contextos))]

    L += ["", "### Las 3 apneas de las capturas anteriores", "",
          "| Duración mínima | acostado1 | acostado2 | sentado | Falsas alarmas en las 12 capturas cortas | Por 10 min |",
          "|---|---|---|---|---|---|"]
    for duracion in DURACIONES_S:
        retrasos, falsas, tasa = apneas_anteriores(duracion)
        celdas = " | ".join("no detecta" if r is None else f"+{r:.0f} s" for r in retrasos.values())
        L.append(f"| {duracion:g} s | {celdas} | {falsas} | {tasa:.1f} |")

    # 5. Respiración
    ventanas = respiracion_en_ventanas(datos, lambda tt: sueno[0] <= tt < sueno[1])
    total_ventanas = int(horas_sueno * 3600 // er.VENTANA_S)
    L += ["", "## Respiración durante el sueño", "",
          f"Ventanas de 30 s (sin solaparse) con objetivo en todas las tramas: {len(ventanas['t_fin'])} de ~{total_ventanas} "
          f"({len(ventanas['t_fin']) / total_ventanas * 100:.0f} % del sueño con datos).", "",
          "| Estimación | Ventanas | P10 | P25 | Mediana | P75 | P90 | = 0 | 0-6 rpm | 10-20 rpm |",
          "|---|---|---|---|---|---|---|---|---|---|"]
    for clave, nombre in (("modulo", "Módulo (mediana de resp_rpm)"), ("fft", "Propia: FFT de fase_resp"),
                          ("cruces", "Propia: cruces de fase_resp")):
        L.append(f"| {nombre} | {describir(ventanas[clave])} |")
    con = (datos["t"] >= sueno[0]) & (datos["t"] < sueno[1]) & (datos["presencia"] > 0.5)
    crudo = datos["resp_rpm"][con]
    L += ["", f"resp_rpm crudo con presencia en el sueño: {len(crudo)} tramas, {np.mean(crudo == 0) * 100:.0f} % en 0, "
              f"mediana de los valores > 0: {np.median(crudo[crudo > 0]):.0f} rpm."]
    # Por hora: la mediana del módulo y la proporción de ventanas en 0
    L += ["", "| Hora | Ventanas con objetivo | Módulo: mediana | Módulo: ventanas en 0 | FFT: mediana | Cruces: mediana |",
          "|---|---|---|---|---|---|"]
    hora = (ventanas["t_fin"] // 3600).astype(int)
    for k in np.unique(hora):
        s = hora == k
        L.append(f"| {k}-{k + 1} h | {s.sum()} | {np.nanmedian(ventanas['modulo'][s]):.1f} | "
                 f"{np.mean(ventanas['modulo'][s] == 0) * 100:.0f} % | {np.nanmedian(ventanas['fft'][s]):.1f} | "
                 f"{np.nanmedian(ventanas['cruces'][s]):.1f} |")

    contenido = "\n".join(L) + "\n"
    (carpeta / "resumen.md").write_text(contenido, encoding="utf-8", newline="\n")
    graficar_noche(carpeta / "noche.png", minutos, segmentos, t_eventos, lista_huecos, cambio)
    graficar_alarmas(carpeta / "alarmas.png", t30, np.interp(t30, t10, act10), persona, alarmas_por_duracion,
                     segmentos, t_eventos, lista_huecos)
    graficar_respiracion(carpeta / "respiracion.png", ventanas)
    print(contenido)
    print(f"Resultados en {carpeta}")


if __name__ == "__main__":
    main()
