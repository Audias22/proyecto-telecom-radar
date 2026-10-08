"""Detector de apnea por caída de amplitud de la fase de respiración.

Uso:
    python detectar_apnea.py

No estima la frecuencia: compara el RMS de los últimos RMS_CORTO_S segundos con una referencia
(mediana del RMS corto en los últimos REFERENCIA_S segundos de respiración normal). Si con
presencia el RMS corto queda por debajo de UMBRAL_RELATIVO × referencia durante DURACION_MIN_S
segundos seguidos, da la alarma.

- Funciona en tiempo real y con filtro causal (como correría en el ESP32), así que el retraso de
  detección medido incluye el retraso del filtro.
- La referencia se congela mientras el RMS está bajo, para que la apnea no la contamine.
- Solo se arma si la referencia supera RMS_MIN_PERSONA: un objeto quieto (pared) nunca "respiró",
  así que no puede dejar de hacerlo.

Evalúa la captura de apnea y las demás capturas (para contar falsas alarmas), prueba una rejilla
de umbrales y duraciones, y escribe en pruebas/resultados/deteccion_apnea/:
    apnea.png       señales de la captura de apnea con el intervalo marcado y la detección
    relacion.png    RMS corto / referencia en el tiempo para todas las capturas
    deteccion.md    resultados con los parámetros elegidos y la rejilla de sensibilidad
"""

from pathlib import Path

import numpy as np
from scipy import signal

import estimar_respiracion as er

# --- Parámetros (fijados antes de ver los resultados; ver docs/radar.md) -------------------

SENAL = "fase_resp"
RMS_CORTO_S = 5.0          # ventana del RMS corto
REFERENCIA_S = 45.0        # historia de RMS corto con la que se calcula la referencia
REFERENCIA_MIN_S = 30.0    # historia mínima antes de armar el detector
UMBRAL_RELATIVO = 0.5      # RMS corto / referencia por debajo de esto = respiración ausente
DURACION_MIN_S = 10.0      # tiempo seguido por debajo del umbral para dar la alarma
RMS_MIN_PERSONA = 0.01     # referencia mínima para armarse (pared: 0.003; personas: >= 0.035)
PASO_S = 1.0               # el detector se evalúa una vez por segundo

CARPETA_CRUDOS = Path(__file__).resolve().parent.parent / "pruebas" / "crudos"
CARPETA_SALIDA = er.CARPETA_RESULTADOS / "deteccion_apnea"

CAPTURA_APNEA = "2026-10-07_224314_apnea_84cm"
INTERVALO_APNEA_S = (45.0, 65.0)
# Capturas sin apnea, para contar falsas alarmas.
CAPTURAS_SIN_APNEA = [
    "2026-10-06_172200_persona_sentada_84cm",
    "2026-10-07_222846_normal_84cm",
    "2026-10-07_223440_lenta_84cm",
    "2026-10-07_223850_rapida_84cm",
    "2026-10-06_163155_sin_nadie_apuntando_pared",
    "2026-10-06_161758_persona_sentada_80cm",
    "2026-10-06_162052_persona_frente_computadora_50cm",
]

REJILLA_UMBRAL = (0.3, 0.4, 0.5, 0.6)
REJILLA_DURACION_S = (5.0, 10.0, 15.0)


# --- Detector ------------------------------------------------------------------------------

class DetectorApnea:
    """Detector que recibe una muestra filtrada por vez, como en el firmware.

    Usa un buffer circular de RMS_CORTO_S segundos de muestras y otro de REFERENCIA_S valores
    de RMS corto (uno por segundo).
    """

    def __init__(self, fs=er.FS_HZ, umbral=UMBRAL_RELATIVO, duracion_min_s=DURACION_MIN_S):
        self.fs = fs
        self.umbral = umbral
        self.duracion_min_s = duracion_min_s
        self.muestras_rms = int(round(RMS_CORTO_S * fs))
        self.muestras_paso = int(round(PASO_S * fs))
        self.buffer = np.zeros(self.muestras_rms)
        self.historia = []          # RMS corto de los segundos "normales" (máx. REFERENCIA_S)
        self.contador = 0
        self.bajo_desde = None      # instante en que el RMS cayó bajo el umbral
        self.en_alarma = False

    def procesar(self, t, x, presencia):
        """Agrega una muestra. Devuelve un dict con el estado cada PASO_S, o None."""
        self.buffer[self.contador % self.muestras_rms] = x
        self.contador += 1
        if self.contador < self.muestras_rms or self.contador % self.muestras_paso:
            return None

        rms_corto = float(np.sqrt(np.mean(self.buffer ** 2)))
        armado = len(self.historia) * PASO_S >= REFERENCIA_MIN_S
        referencia = float(np.median(self.historia)) if armado else float("nan")
        armado = armado and referencia >= RMS_MIN_PERSONA and presencia
        bajo = armado and rms_corto < self.umbral * referencia

        alarma_nueva = False
        if bajo:
            if self.bajo_desde is None:
                self.bajo_desde = t
            if not self.en_alarma and t - self.bajo_desde >= self.duracion_min_s:
                self.en_alarma = True
                alarma_nueva = True
        else:
            self.bajo_desde = None
            self.en_alarma = False
            self.historia.append(rms_corto)
            if len(self.historia) * PASO_S > REFERENCIA_S:
                self.historia.pop(0)

        return {"t": t, "rms": rms_corto, "referencia": referencia, "bajo": bajo, "alarma": alarma_nueva}


def senal_tiempo_real(datos, senal=SENAL):
    """Remuestrea y filtra con el pasabanda causal (sosfilt), como se haría muestra a muestra."""
    t, x = er.senal_respiracion(datos, senal)
    t_uniforme, x_uniforme = er.remuestrear(t, x)
    filtrada = signal.sosfilt(er.disenar_filtro(), x_uniforme - x_uniforme[0])
    presencia = np.interp(t_uniforme, datos["t"], datos["presencia"]) >= 0.5
    return t_uniforme, filtrada, presencia


def correr_detector(datos, umbral=UMBRAL_RELATIVO, duracion_min_s=DURACION_MIN_S):
    """Devuelve (estados por segundo, instantes de alarma)."""
    t_uniforme, filtrada, presencia = senal_tiempo_real(datos)
    detector = DetectorApnea(umbral=umbral, duracion_min_s=duracion_min_s)
    estados = []
    for t, x, p in zip(t_uniforme, filtrada, presencia):
        estado = detector.procesar(t, x, p)
        if estado is not None:
            estados.append(estado)
    alarmas = [e["t"] for e in estados if e["alarma"]]
    return estados, alarmas


def clasificar_alarmas_apnea(alarmas):
    """Separa las alarmas de la captura de apnea en (primera verdadera, falsas).

    Una alarma es verdadera si ocurre entre el inicio de la apnea y 15 s después de su fin
    (margen para el retraso de detección).
    """
    inicio, fin = INTERVALO_APNEA_S
    verdaderas = [a for a in alarmas if inicio <= a <= fin + 15.0]
    falsas = [a for a in alarmas if not inicio <= a <= fin + 15.0]
    return (verdaderas[0] if verdaderas else None), falsas


# --- Salidas -------------------------------------------------------------------------------

def graficar_apnea(ruta_png, datos, estados, alarmas):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    t_resp, filtrada_resp, _ = senal_tiempo_real(datos, "fase_resp")
    t_total, filtrada_total, _ = senal_tiempo_real(datos, "fase_total_integrada")
    t_e = np.array([e["t"] for e in estados])
    rms = np.array([e["rms"] for e in estados])
    referencia = np.array([e["referencia"] for e in estados])

    figura, ejes = plt.subplots(4, 1, figsize=(12, 11), sharex=True)
    ejes[0].plot(datos["t"], datos["fase_resp"], color="0.6", linewidth=0.7, label="cruda")
    ejes[0].plot(t_resp, filtrada_resp, color="C0", linewidth=1.0, label="filtrada (causal)")
    ejes[0].set_ylabel("fase_resp")
    ejes[1].plot(t_total, filtrada_total, color="C1", linewidth=1.0)
    ejes[1].set_ylabel("fase_total integrada\nfiltrada (causal)")
    ejes[2].plot(t_e, rms, "o-", color="C4", markersize=3, label=f"RMS {RMS_CORTO_S:g} s de fase_resp")
    ejes[2].plot(t_e, referencia, color="C2", label=f"referencia (mediana {REFERENCIA_S:g} s)")
    ejes[2].plot(t_e, UMBRAL_RELATIVO * referencia, color="C3", linestyle="--",
                 label=f"umbral ({UMBRAL_RELATIVO:g} × referencia)")
    ejes[2].set_ylabel("RMS")
    ejes[3].plot(datos["t"], datos["resp_rpm"], color="0.4")
    ejes[3].set_ylabel("resp_rpm módulo")
    ejes[3].set_xlabel("tiempo desde el inicio de la captura (s)")

    for eje in ejes:
        eje.axvspan(*INTERVALO_APNEA_S, color="C3", alpha=0.12)
        for alarma in alarmas:
            eje.axvline(alarma, color="C3", linewidth=1.5)
        eje.grid(True, alpha=0.3)
    ejes[0].legend(loc="upper right", fontsize=8)
    ejes[2].legend(loc="upper right", fontsize=8)
    texto = ", ".join(f"{a:.0f} s" for a in alarmas) if alarmas else "ninguna"
    figura.suptitle(f"{CAPTURA_APNEA}: apnea {INTERVALO_APNEA_S[0]:g}-{INTERVALO_APNEA_S[1]:g} s "
                    f"(franja roja); alarmas (líneas rojas): {texto}")
    figura.tight_layout()
    figura.savefig(ruta_png, dpi=100)
    plt.close(figura)


def graficar_relacion(ruta_png, resultados):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figura, ejes = plt.subplots(len(resultados), 1, figsize=(12, 2.0 * len(resultados)), sharex=True)
    for eje, (nombre, estados, alarmas) in zip(ejes, resultados):
        t = np.array([e["t"] for e in estados])
        relacion = np.array([e["rms"] / e["referencia"] if e["referencia"] > 0 else np.nan for e in estados])
        eje.plot(t, relacion, color="C0", linewidth=1.0)
        eje.axhline(UMBRAL_RELATIVO, color="C3", linestyle="--", linewidth=1.0)
        for alarma in alarmas:
            eje.axvline(alarma, color="C3", linewidth=1.5)
        if nombre == CAPTURA_APNEA:
            eje.axvspan(*INTERVALO_APNEA_S, color="C3", alpha=0.12)
        eje.set_ylabel(nombre.split("_", 2)[2], fontsize=8)
        eje.set_ylim(0, 3)
        eje.grid(True, alpha=0.3)
    ejes[-1].set_xlabel("tiempo (s)")
    figura.suptitle("RMS corto / referencia (sin valor hasta tener 30 s de referencia); "
                    "línea discontinua: umbral; líneas rojas: alarmas")
    figura.tight_layout()
    figura.savefig(ruta_png, dpi=100)
    plt.close(figura)


def main():
    CARPETA_SALIDA.mkdir(parents=True, exist_ok=True)
    capturas = {nombre: er.cargar_csv(CARPETA_CRUDOS / f"{nombre}.csv")
                for nombre in [CAPTURA_APNEA] + CAPTURAS_SIN_APNEA}

    # Parámetros elegidos.
    resultados = []
    for nombre, datos in capturas.items():
        estados, alarmas = correr_detector(datos)
        resultados.append((nombre, estados, alarmas))
    _, estados_apnea, alarmas_apnea = resultados[0]
    deteccion, falsas_apnea = clasificar_alarmas_apnea(alarmas_apnea)

    lineas = [
        "# Detección de apnea por caída de amplitud",
        "",
        f"Señal {SENAL}, pasabanda causal {er.BANDA_HZ[0]:g}-{er.BANDA_HZ[1]:g} Hz, RMS de {RMS_CORTO_S:g} s, "
        f"referencia = mediana del RMS de los últimos {REFERENCIA_S:g} s normales, umbral "
        f"{UMBRAL_RELATIVO:g} × referencia durante {DURACION_MIN_S:g} s, referencia mínima {RMS_MIN_PERSONA:g}, "
        "presencia obligatoria.",
        "",
        "## Resultado con los parámetros elegidos",
        "",
        "| Captura | Alarmas (s) | Resultado |",
        "|---|---|---|",
    ]
    for nombre, _, alarmas in resultados:
        lista = ", ".join(f"{a:.0f}" for a in alarmas) or "ninguna"
        if nombre == CAPTURA_APNEA:
            if deteccion is None:
                resultado = "apnea NO detectada"
            else:
                resultado = (f"apnea detectada a los {deteccion:.0f} s, retraso "
                             f"{deteccion - INTERVALO_APNEA_S[0]:.0f} s desde el inicio")
            if falsas_apnea:
                resultado += f"; {len(falsas_apnea)} falsa(s) fuera del intervalo"
        else:
            resultado = f"{len(alarmas)} falsa(s) alarma(s)" if alarmas else "sin falsas alarmas"
        lineas.append(f"| {nombre.split('_', 2)[2]} | {lista} | {resultado} |")

    lineas += [
        "",
        "## Sensibilidad a umbral y duración mínima",
        "",
        "Detección en la captura de apnea (retraso desde el segundo "
        f"{INTERVALO_APNEA_S[0]:g}) / falsas alarmas sumando todas las capturas "
        "(incluidas las de la captura de apnea fuera del intervalo).",
        "",
        "| Umbral | " + " | ".join(f"{d:g} s" for d in REJILLA_DURACION_S) + " |",
        "|---|" + "---|" * len(REJILLA_DURACION_S),
    ]
    for umbral in REJILLA_UMBRAL:
        celdas = []
        for duracion in REJILLA_DURACION_S:
            falsas_total = 0
            det = None
            for nombre, datos in capturas.items():
                _, alarmas = correr_detector(datos, umbral, duracion)
                if nombre == CAPTURA_APNEA:
                    det, falsas = clasificar_alarmas_apnea(alarmas)
                    falsas_total += len(falsas)
                else:
                    falsas_total += len(alarmas)
            texto_det = f"{det:.0f} s (+{det - INTERVALO_APNEA_S[0]:.0f})" if det is not None else "no detecta"
            celdas.append(f"{texto_det} / {falsas_total}")
        lineas.append(f"| {umbral:g} | " + " | ".join(celdas) + " |")

    contenido = "\n".join(lineas) + "\n"
    (CARPETA_SALIDA / "deteccion.md").write_text(contenido, encoding="utf-8", newline="\n")
    graficar_apnea(CARPETA_SALIDA / "apnea.png", capturas[CAPTURA_APNEA], estados_apnea, alarmas_apnea)
    graficar_relacion(CARPETA_SALIDA / "relacion.png", resultados)
    print(contenido)
    print(f"Resultados en {CARPETA_SALIDA}")


if __name__ == "__main__":
    main()
