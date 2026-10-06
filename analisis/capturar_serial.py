"""Captura el CSV que imprime la base por el puerto serie y lo guarda en pruebas/crudos/.

Uso:
    python capturar_serial.py persona_sentada_50cm
    python capturar_serial.py sin_nadie --puerto COM3 --duracion 60

El archivo se llama AAAA-MM-DD_HHMMSS_descripcion.csv. Se detiene con Ctrl+C (o al cumplir
--duracion). Cada línea se escribe y se vacía a disco en cuanto llega, así que al cortar no se
pierde nada de lo ya recibido.

Solo se guardan las líneas de datos con el número correcto de columnas. Las líneas que empiezan
con '#' (comentarios del firmware) se muestran en pantalla pero no van al archivo.
"""

import argparse
import re
import sys
import time
from datetime import datetime
from pathlib import Path

import serial
from serial.tools import list_ports

ENCABEZADO = "ms,valida,presencia,distancia_cm,resp_rpm,latido_bpm,fase_total,fase_resp,fase_latido"
COLUMNAS = len(ENCABEZADO.split(","))
CARPETA_CRUDOS = Path(__file__).resolve().parent.parent / "pruebas" / "crudos"


def elegir_puerto(puerto):
    if puerto:
        return puerto
    puertos = [p.device for p in list_ports.comports()]
    if len(puertos) == 1:
        return puertos[0]
    if not puertos:
        sys.exit("No se encontró ningún puerto serie. Conecta la base o usa --puerto.")
    sys.exit(f"Hay varios puertos ({', '.join(puertos)}). Indica uno con --puerto.")


def nombre_archivo(descripcion):
    limpia = re.sub(r"[^A-Za-z0-9_-]+", "_", descripcion.strip()).strip("_")
    if not limpia:
        sys.exit("La descripción no puede quedar vacía.")
    return f"{datetime.now():%Y-%m-%d_%H%M%S}_{limpia}.csv"


def abrir_puerto(puerto, baudios):
    # En el USB serie del ESP32-C6, los cambios de DTR/RTS reinician el chip. Con los valores por
    # defecto de pyserial la base se reinicia al cerrar el puerto (medido con el kit). Fijarlos
    # en False antes de abrir evita el reinicio; el encabezado se escribe aquí y no hace falta
    # el que imprime el firmware al arrancar.
    conexion = serial.Serial()
    conexion.port = puerto
    conexion.baudrate = baudios
    conexion.timeout = 0.5
    conexion.dtr = False
    conexion.rts = False
    conexion.open()
    return conexion


def es_dato(linea):
    partes = linea.split(",")
    if len(partes) != COLUMNAS:
        return False
    try:
        int(partes[0])
    except ValueError:
        return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("descripcion", help="texto corto que va en el nombre del archivo")
    parser.add_argument("--puerto", help="puerto serie (por defecto, el único conectado)")
    parser.add_argument("--baudios", type=int, default=115200)
    parser.add_argument("--duracion", type=float, help="segundos de captura (por defecto, hasta Ctrl+C)")
    args = parser.parse_args()

    puerto = elegir_puerto(args.puerto)
    CARPETA_CRUDOS.mkdir(parents=True, exist_ok=True)
    ruta = CARPETA_CRUDOS / nombre_archivo(args.descripcion)

    lineas = 0
    descartadas = 0
    inicio = time.monotonic()
    print(f"Capturando de {puerto} en {ruta}. Ctrl+C para terminar.")

    with abrir_puerto(puerto, args.baudios) as conexion, \
            open(ruta, "w", encoding="utf-8", newline="\n") as archivo:
        archivo.write(ENCABEZADO + "\n")
        archivo.flush()
        try:
            while args.duracion is None or time.monotonic() - inicio < args.duracion:
                crudo = conexion.readline()
                if not crudo:
                    continue
                linea = crudo.decode("utf-8", errors="replace").strip()
                if not linea or linea == ENCABEZADO:
                    continue
                if linea.startswith("#"):
                    print(linea)
                    continue
                if not es_dato(linea):
                    descartadas += 1
                    continue
                archivo.write(linea + "\n")
                archivo.flush()
                lineas += 1
                if lineas % 100 == 0:
                    print(f"  {lineas} lecturas", end="\r")
        except KeyboardInterrupt:
            pass

    duracion = time.monotonic() - inicio
    tasa = lineas / duracion if duracion > 0 else 0
    print(f"\nGuardadas {lineas} lecturas en {duracion:.1f} s ({tasa:.2f} por segundo) en {ruta}")
    if descartadas:
        print(f"Se descartaron {descartadas} líneas incompletas o con formato inesperado.")


if __name__ == "__main__":
    main()
