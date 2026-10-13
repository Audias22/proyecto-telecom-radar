# Colgante: primera prueba en banco (2026-10-12)

Pruebas 1 a 9 de la primera parte de `firmware/colgante/pruebas_fisicas.md`.

## Montaje

| | |
|---|---|
| Fecha | 2026-10-12 |
| Hizo la prueba | Audias |
| Colgante | ESP32-C3 Super Mini por USB, sin batería. Firmware de `main` en `6df78f4` con `HEARTBEAT_PRUEBA_S = 30U` (local) y batería simulada (3900 mV) |
| Botón | Pulsador 12x12 entre GPIO3 y GND |
| Buzzer | Módulo de buzzer activo de 3 pines, activo en bajo: `VCC` a 3V3, `I/O` a GPIO10 |
| Receptor | ESP32 DevKitC clásico (WROOM-32) con `firmware/pruebas/receptor_espnow`, canal 1 |
| Distancia | ~20 cm entre colgante y receptor, sin obstáculos |

## Resultados

| # | Prueba | Resultado | Observado |
|---|---|---|---|
| 1 | Arranque | Pasa | 3 resets. Cada uno, un `HEARTBEAT` con `ARRANQUE_FRIO` y secuencia aleatoria distinta (46562, 16499, 37401). `ack=OK` en ~280 us. Sin sonido |
| 2 | ALERTA con ACK | Pasa | 6 pulsaciones seguidas: una línea `ALERTA NUEVO` por pulsación, secuencia de 1 en 1, ACK en 279-310 us |
| 3 | Beep de confirmación | Pasa | Un beep corto por alerta. Botón sostenido 8 s: una sola alerta y un solo beep |
| 4 | Sin receptor | Pasa | Tres beeps largos. Al reconectar, la alerta llegó `NUEVO ack=OK`. La secuencia saltó 4: la alerta perdida y 3 heartbeats sin ACK mientras el receptor estaba desconectado. El tiempo hasta el patrón de error no se midió |
| 5 | Barrido | Pasa | Canales 6, 13 y 1. El primer mensaje tras el cambio llega con `REINTENTO\|BARRIDO`; el siguiente, con `flags=-` |
| 6a | ACK perdido | Pasa | Un solo `NUEVO` por secuencia; el reintento, `DUPLICADO` con `ack=OK+DUPLICADO`. Beep corto |
| 6b | ACK perdido tras arranque | Pasa | Un solo `NUEVO` por secuencia con `ARRANQUE_FRIO`. Sin sonido |
| 6c | Base reiniciada (`o`) | Pasa | La alerta se acepta como `NUEVO` |
| 7 | HEARTBEAT por timer | Pasa | Intervalo real ~30.1 s, secuencia de 1 en 1, sin sonido |
| 8 | Sin falsas alertas | Pasa | 10 minutos sin tocar: 20 heartbeats y ninguna `ALERTA` |

El buzzer no hizo ningún sonido indebido: nada al encender, al despertar ni durante el deep sleep.

Queda sin hacer la primera casilla de la prueba 8: despertar con el botón 5 veces en distintos
momentos del intervalo del heartbeat. Las 6 alertas de la prueba 2 sí despertaron al colgante
desde deep sleep, pero no se anotó en qué momento.

## Observaciones

**Copias del barrido en canales vecinos.** Durante el barrido el receptor recibió el mismo
mensaje varias veces: 3 copias con el receptor en el canal 6 y 5 en el 13. Los canales de 2.4 GHz
se solapan, así que el receptor oye las transmisiones del colgante en canales cercanos. Solo la
primera copia fue `NUEVO`; las demás, `DUPLICADO`. La regla de duplicados funciona como se espera.

**RSSI muy bajo (problema).** A 20 cm el RSSI estuvo normalmente entre -84 y -92 dBm (rango
completo de -69 a -95 dBm). A esa distancia se esperaría algo cercano a -40 dBm. Un heartbeat
necesitó `REINTENTO` con -93 dBm. A esta distancia funciona, pero queda muy poco margen para el
alcance real dentro de la casa.

Causa sospechada: la antena de chip del ESP32-C3 Super Mini, mal adaptada en estas placas. Se
probó bajar la potencia máxima (prueba 9, abajo) y no mejoró la señal.

## Prueba 9: potencia de transmisión

Receptor ESP32 clásico elevado, colgante a ~3 m con línea de vista, canal 1, misma posición en las
dos mediciones.

| Potencia (qdBm) | dBm | Heartbeats | RSSI promedio (dBm) | Mínimo | Máximo | Con `REINTENTO` |
|---|---|---|---|---|---|---|
| 34 | 8.5 | 20 | -91.1 | -94 | -89 | 0 |
| 80 | 20 | 14 | -85.4 | -87 | -85 | 0 |

Conclusión: en esta placa la potencia alta da ~6 dB más. Bajar a 8.5 dBm no aplica;
`COLGANTE_TX_POTENCIA_QDBM` queda en 80.

La altura y la orientación cambian mucho el RSSI: con el receptor más alto y lejos de la mesa, a
0.6-1 m, pasó de ~-90 a ~-74/-80 dBm. Conviene cuidar la
ubicación de la base.

## Siguiente paso

- Hecho: `COLGANTE_TX_POTENCIA_QDBM` agregado en `firmware/colgante/include/config.h` y elegido
  con la prueba 9 (80, 20 dBm).
- Pendiente: la primera casilla de la prueba 8 (despertar en distintos momentos del intervalo).
- Medir el alcance real dentro de la casa con la base en su ubicación definitiva.
