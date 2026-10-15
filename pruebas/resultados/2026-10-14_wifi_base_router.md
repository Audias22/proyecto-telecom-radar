# Base con WiFi y colgante: primera prueba con el router (2026-10-14)

Guía de prueba de la fase WiFi de `firmware/base-radar/README.md`, pasos 1 a 3.

## Montaje

| | |
|---|---|
| Fecha | 2026-10-14 |
| Base | XIAO ESP32-C6 del kit MR60BHA2, `BASE_WIFI_HABILITADO 1`, conectada al router de la casa |
| Colgante | ESP32-C3 Super Mini, firmware anterior a la corrección del canal en el ACK |
| Distancia | ~1 m entre colgante y base |

## Resultados

| # | Prueba | Resultado | Observado |
|---|---|---|---|
| 1 | Conectar al router | Pasa | Conectó al primer intento, canal 7, RSSI -73 dBm |
| 3 | El colgante encuentra la base por barrido | Pasa | El colgante, con el canal 1 guardado, barrió hasta el canal 7 y recibió el ACK |
| - | Duplicados | Pasa | Las copias del mismo mensaje (reintentos y barrido) se marcaron `DUPLICADO`; la alerta se procesó una sola vez |

## Problema: el colgante guarda un canal vecino

El colgante a veces recibía el ACK estando en el canal 6 (vecino del 7) por el solape de los
canales de 2.4 GHz, y guardaba ese canal en RTC. En el mensaje siguiente empezaba en el 6: la
base lo oía, pero el ACK no le llegaba al colgante, que agotaba los reintentos y volvía a barrer.

Registro en la base:

1. `ALERTA seq=15658` llegó `NUEVO` con RSSI -46 dBm, transmitida en el canal guardado (6).
2. Dos líneas `REINTENTO` de la misma secuencia: el colgante no recibió el ACK.
3. Líneas `REINTENTO|BARRIDO` desde el canal 1 hasta que la base volvió a recibir con -46 dBm y el
   colgante recibió el ACK.

Causa: el colgante guardaba el canal en el que estaba al recibir el ACK, no el canal real de la
base.

## Corrección

El ACK lleva ahora el canal de la radio de la base en el campo `bateria_mv` (0 = sin dato) y el
colgante guarda ese canal si está entre 1 y 13; si no, el canal en que recibió el ACK. Si difieren
lo registra por serial. Detalle en `docs/protocolo.md`, sección 6. Sin cambio de
`PROTOCOLO_VERSION` ni del formato del paquete.

Verificado: pruebas host del colgante (43 casos, 649 verificaciones, todas pasan) y compilación de
base, diagnóstico, colgante y receptor de prueba.

## Siguiente paso

- Repetir el paso 3 con la corrección cargada en base y colgante: tras el barrido, el colgante
  debe guardar el canal 7 aunque oiga el ACK en el 6, y los mensajes siguientes deben llegar con
  `flags=-` sin reintentos.
- Pasos 4 a 6 de la guía de la fase WiFi (apagar y encender el router, botón BOOT).
