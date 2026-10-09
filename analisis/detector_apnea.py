"""Detector de apnea por caída de una medida respecto a la respiración normal reciente.

Módulo de funciones (sin programa principal); lo usa evaluar_detector_apnea.py. Está escrito para
portarse al ESP32: todas las medidas se calculan una vez por segundo con ventanas fijas, filtros
IIR causales y buffers de tamaño acotado.

Medidas por segundo (MEDIDAS), sobre los últimos `ventana_s` segundos:
    actividad   mediana de |fase_total| en las tramas con dato (fase_total se comporta como el
                incremento de fase entre tramas: qué tan rápido se mueve el pecho)
    rms_resp    RMS de fase_resp filtrada con el pasabanda de respiración (causal)
    banda       proporción de energía de la fase total integrada que cae en la banda de
                respiración (0.1-0.6 Hz) frente a la banda ancha (0.05-3 Hz)
Apoyo opcional:
    modulo      mediana de resp_rpm del módulo en la ventana

Máquina de estados del detector, una vez por segundo:
    - Historia: valores de la medida en los segundos "normales" (máximo REFERENCIA_S).
    - Armado: al menos ARMADO_HISTORIA_S de historia, mediana de la actividad en esa historia por
      encima del mínimo de una persona (ACTIVIDAD_MIN_PERSONA) y presencia. Es el estado
      "respirando": el detector solo puede avisar de una apnea si antes vio respirar. Se usa la
      actividad para todas las medidas porque es la que separa personas de objetos quietos
      (la proporción de banda, por ejemplo, da valores parecidos con una pared).
    - Bajo: armado y medida < umbral × referencia (y, si se pide, módulo <= MODULO_MAX_RPM).
    - Alarma: bajo durante DURACION_MIN_S seguidos.
    - Mientras está bajo, la referencia se congela; si lleva DESCONGELAR_S bajo, se descarta la
      historia y se vuelve a aprender (evita quedar en alarma indefinidamente por una referencia
      vieja).
"""

from collections import deque

import numpy as np
from scipy import signal

import estimar_respiracion as er

FS_HZ = er.FS_HZ
DURACION_MIN_S = 20.0       # requisito: las apneas de prueba duran 20-30 s
REFERENCIA_S = 60.0         # historia de segundos normales para la referencia
ARMADO_HISTORIA_S = 30.0    # historia mínima de respiración antes de armarse
DESCONGELAR_S = 60.0        # tras este tiempo bajo, se reaprende la referencia
MODULO_MAX_RPM = 8.0        # condición de apoyo: el módulo también ve respiración baja

# Actividad mínima (mediana de la historia) para considerar que hay una persona respirando.
# Objetos quietos: 0.004 (cama vacía) y 0.016-0.017 (pared); personas: >= 0.037 (sección 11).
ACTIVIDAD_MIN_PERSONA = 0.03
MEDIDAS = ("actividad", "rms_resp", "banda")

BANDA_RESP_HZ = (0.1, 0.6)
BANDA_ANCHA_HZ = (0.05, 3.0)


# --- Medidas por segundo -------------------------------------------------------------------

def _ventanas(t_muestras, valores, instantes, ventana_s, funcion):
    """Aplica `funcion` a las muestras de (t - ventana_s, t] para cada instante t."""
    resultado = np.full(len(instantes), np.nan)
    inicio = 0
    fin = 0
    for i, t in enumerate(instantes):
        while fin < len(t_muestras) and t_muestras[fin] <= t:
            fin += 1
        while inicio < fin and t_muestras[inicio] <= t - ventana_s:
            inicio += 1
        if fin > inicio:
            resultado[i] = funcion(valores[inicio:fin])
    return resultado


def _filtro_causal(x, banda):
    sos = signal.butter(2, banda, btype="bandpass", fs=FS_HZ, output="sos")
    return signal.sosfilt(sos, x - x[0])


def calcular_medidas(datos, ventana_s):
    """Medidas una vez por segundo. Devuelve dict con 't', cada medida, 'modulo' y 'presencia'."""
    t = datos["t"]
    instantes = np.arange(np.ceil(ventana_s), t[-1] + 1e-9, 1.0)

    validas = datos["fase_total"] != 0.0
    actividad = _ventanas(t[validas], np.abs(datos["fase_total"][validas]), instantes, ventana_s, np.median)

    t_u, resp_u = er.remuestrear(t, datos["fase_resp"])
    resp_filtrada = _filtro_causal(resp_u, er.BANDA_HZ)
    rms_resp = _ventanas(t_u, resp_filtrada, instantes, ventana_s, lambda v: np.sqrt(np.mean(v ** 2)))

    t_int, x_int = er.senal_respiracion(datos, "fase_total_integrada")
    t_iu, x_iu = er.remuestrear(t_int, x_int)
    en_banda = _filtro_causal(x_iu, BANDA_RESP_HZ)
    ancha = _filtro_causal(x_iu, BANDA_ANCHA_HZ)
    energia_banda = _ventanas(t_iu, en_banda ** 2, instantes, ventana_s, np.mean)
    energia_ancha = _ventanas(t_iu, ancha ** 2, instantes, ventana_s, np.mean)
    banda = energia_banda / np.maximum(energia_ancha, 1e-12)

    modulo = _ventanas(t, datos["resp_rpm"], instantes, ventana_s, np.median)
    presencia = np.interp(instantes, t, datos["presencia"]) >= 0.5
    return {"t": instantes, "actividad": actividad, "rms_resp": rms_resp, "banda": banda,
            "modulo": modulo, "presencia": presencia}


# --- Detector ------------------------------------------------------------------------------

class DetectorApnea:
    """Máquina de estados; recibe una medida por segundo (ver la documentación del módulo)."""

    def __init__(self, medida, umbral, usar_modulo=False):
        self.medida = medida
        self.umbral = umbral
        self.usar_modulo = usar_modulo
        self.historia = deque()            # medida en los segundos normales
        self.historia_actividad = deque()  # actividad en esos mismos segundos (para el armado)
        self.bajo_desde = None
        self.en_alarma = False

    def evaluar(self, t, valor, actividad, modulo_rpm, presencia):
        """Devuelve (alarma nueva o None, en_alarma, referencia). Alarma = (instante, inicio)."""
        if np.isnan(valor) or np.isnan(actividad):
            return None, self.en_alarma, float("nan")
        armado = len(self.historia) >= ARMADO_HISTORIA_S
        referencia = float(np.median(self.historia)) if self.historia else float("nan")
        respirando = bool(self.historia_actividad) and np.median(self.historia_actividad) >= ACTIVIDAD_MIN_PERSONA
        armado = armado and respirando and presencia
        bajo = armado and valor < self.umbral * referencia
        if bajo and self.usar_modulo:
            bajo = not np.isnan(modulo_rpm) and modulo_rpm <= MODULO_MAX_RPM

        alarma = None
        if bajo:
            if self.bajo_desde is None:
                self.bajo_desde = t
            duracion = t - self.bajo_desde
            if not self.en_alarma and duracion >= DURACION_MIN_S:
                self.en_alarma = True
                alarma = (t, self.bajo_desde)
            if duracion >= DESCONGELAR_S:
                self.historia.clear()
                self.historia_actividad.clear()
                self.bajo_desde = None
                self.en_alarma = False
        else:
            self.bajo_desde = None
            self.en_alarma = False
            self.historia.append(valor)
            self.historia_actividad.append(actividad)
            while len(self.historia) > REFERENCIA_S:
                self.historia.popleft()
                self.historia_actividad.popleft()
        return alarma, self.en_alarma, referencia


def correr(medidas, medida, umbral, usar_modulo=False):
    """Corre el detector sobre las medidas por segundo de una captura.

    Devuelve (alarmas, segundos_en_alarma, referencias por segundo).
    """
    detector = DetectorApnea(medida, umbral, usar_modulo)
    alarmas, en_alarma, referencias = [], 0, []
    for i, t in enumerate(medidas["t"]):
        alarma, activa, referencia = detector.evaluar(t, medidas[medida][i], medidas["actividad"][i],
                                                      medidas["modulo"][i], medidas["presencia"][i])
        referencias.append(referencia)
        if alarma:
            alarmas.append(alarma)
        en_alarma += int(activa)
    return alarmas, en_alarma, np.array(referencias)
