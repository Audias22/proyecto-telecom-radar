# Pruebas físicas del colgante

Dos partes:

- **Primera prueba en protoboard** (abajo): ESP32-C3 Super Mini por USB, sin batería, con el
  pulsador, el buzzer activo y el receptor de prueba `firmware/pruebas/receptor_espnow` en un
  ESP32 clásico haciendo de base.
- **Lista completa** (más abajo): para el colgante terminado, con batería, TP4056 y la base
  real. Conexiones finales armadas el 2026-10-27 (abajo).

Registrar cada sesión en `pruebas/` (fecha, placa, firmware/commit, lo medido, resultado). Marcar
una casilla solo con la medición hecha; si algo falla, anotar qué se observó y no marcarla.

## Primera prueba en protoboard

### Conexión

| Elemento | Pin del C3 Super Mini | Conexión |
|---|---|---|
| Pulsador 12x12 | GPIO3 (serigrafía `3`) | Pulsador entre GPIO3 y GND. Pull-up interno; no hace falta resistencia |
| Buzzer (módulo de 3 pines) | GPIO10 (serigrafía `10`) | `VCC` a `3V3`, `GND` a `G`, `I/O` a GPIO10. Activo en bajo |
| Alimentación | USB-C | Sin batería por ahora. El módulo del buzzer se alimenta del pin `3V3` |

```
  Pulsador                         Módulo de buzzer activo (3 pines)

  GPIO3 ───┐                       3V3 ──────── VCC
           │                       GPIO10 ───── I/O    (en bajo suena, en alto se calla)
         [ ] pulsador              G (GND) ──── GND
           │
  GND ─────┘
```

- **Pulsador de 4 patas:** las patas van unidas por pares por dentro. Usar dos patas en
  diagonal: así siempre se conecta a través del interruptor y no por el par que ya está unido.
- **Módulo del buzzer:** trae el buzzer activo, un transistor PNP S8550 (SMD marcado `2TY`) y
  1 kΩ en la base. El GPIO solo maneja la base del transistor, así que no hace falta otro
  transistor ni resistencias externas. Probado en banco: `I/O` a GND suena, `I/O` a 3V3 se calla.
  Por eso el firmware tiene `BUZZER_ACTIVO_EN_BAJO 1` en `config.h`.
- **`VCC` a `3V3`, no a 5 V:** el PNP se apaga cuando su base queda al mismo voltaje que su
  emisor (`VCC`). Con `VCC` a 5 V, los 3.3 V del GPIO en alto siguen 1.7 V por debajo y el
  transistor no se apaga: el buzzer sonaría siempre.
- **Sin resistencia de GPIO10 a GND:** con este módulo, una resistencia a GND tira `I/O` hacia
  bajo y lo haría sonar todo el tiempo. Para que el pin no flote en deep sleep, el firmware lo
  deja en alto (apagado) y lo retiene con `gpio_hold_en()` y `gpio_deep_sleep_hold_en()`.
- **Quitar la etiqueta "REMOVE SEAL AFTER WASHING"** que trae encima el buzzer: tapa el orificio
  y baja mucho el volumen.
- No usar GPIO2, GPIO8 ni GPIO9 (pines de arranque; GPIO8 es además el LED de la placa y GPIO9 el
  botón BOOT).

### Preparación

1. Cargar el receptor en el ESP32 clásico y abrir su monitor (ver
   `firmware/pruebas/receptor_espnow/README.md`). Copiar la línea `#define BASE_MAC ...` que
   imprime al arrancar.
2. Crear `firmware/colgante/include/secrets.h` desde `secrets.example.h` con esa `BASE_MAC` y
   `COLGANTE_ID 1`.
3. En `firmware/colgante/include/config.h`, poner `HEARTBEAT_PRUEBA_S` en `30U` para la prueba 7.
   Dejar `REGISTROS_SERIAL_HABILITADOS` en `1`.
4. Compilar y cargar el colgante:
   `pio run -d firmware/colgante -t upload`. Si el puerto no aparece, mantener BOOT presionado al
   conectar el USB.
5. Cada placa en su puerto USB, con su propio monitor. El del colgante se pierde cuando entra en
   deep sleep (el USB se desconecta), así que **lo que cuenta es el monitor del receptor**.
6. Al terminar, volver a poner `HEARTBEAT_PRUEBA_S` en `0U`.

En los resultados de abajo, `seq` es la secuencia y `R` el valor aleatorio con el que empieza
tras el arranque en frío.

### Lista de pruebas, en orden

**1. Arranque**

- Hacer: presionar RST del colgante (o desconectar y conectar el USB).
- Receptor: una línea
  `HEARTBEAT id=1 seq=R flags=ARRANQUE_FRIO bat=3900 mV ... NUEVO ack=OK en ... us`.
- Buzzer: silencio (un heartbeat nunca suena).
- Repetir 2 o 3 veces: `seq` cambia cada vez (es aleatoria) y siempre llega con
  `ARRANQUE_FRIO` y `NUEVO`.
- [x] Pasa. Anotar el tiempo `ack=... us` y el RSSI. (2026-10-12: 3 resets, `ack` ~280 us; RSSI
      bajo, ver `pruebas/resultados/2026-10-12_colgante_banco.md`)

**2. ALERTA con ACK**

- Hacer: con el colgante ya dormido (un segundo después del arranque), presionar el pulsador
  una vez.
- Receptor: `ALERTA id=1 seq=R+1 flags=- ... NUEVO ack=OK`. Ya no lleva `ARRANQUE_FRIO`, porque
  el heartbeat del arranque recibió su ACK. Una sola línea por pulsación.
- [x] Pasa en 5 pulsaciones seguidas; `seq` sube de 1 en 1. (2026-10-12: 6 pulsaciones, `ack`
      279-310 us)

**3. Beep de confirmación**

- Hacer: lo mismo que en la prueba 2, fijándose en el sonido.
- Buzzer: un beep corto (~120 ms) poco después de soltar el botón.
- [x] Un solo beep corto por alerta, audible.
- [x] Mantener el botón presionado más de 5 s: una sola línea `ALERTA` en el receptor y un solo
      beep; al soltar no sale otra alerta. (2026-10-12: 8 s)

**4. Colgante sin receptor (patrón de error)**

- Hacer: desconectar el USB del receptor y presionar el pulsador.
- Receptor: nada (está apagado).
- Buzzer: tres beeps largos (~0.5 s, con pausas de ~0.25 s) unos 2 s después de presionar
  (1.6 s de reintentos y barrido más el arranque de WiFi).
- Volver a conectar el receptor y presionar de nuevo: `ALERTA ... flags=- NUEVO ack=OK` y beep
  corto. La `seq` salta 2: la alerta sin respuesta también gastó un número.
- [x] Pasa. Anotar el tiempo aproximado hasta el patrón de error. (2026-10-12: pasa; el tiempo
      no se anotó. La `seq` saltó 4 y no 2: además de la alerta, 3 heartbeats sin ACK durante la
      desconexión)

**5. Barrido cuando el receptor cambia de canal**

- Hacer: en el monitor del receptor escribir `6` y Enter. Presionar el pulsador.
- Receptor: `ALERTA ... flags=REINTENTO|BARRIDO ... canal=6 NUEVO ack=OK`.
- Buzzer: beep corto, algo más tarde que en la prueba 2 (unos 0.8 s).
- Presionar otra vez: `ALERTA ... flags=- ... NUEVO`. El colgante guardó el canal y ya no barre.
- Volver al canal `1` y repetir: de nuevo una línea con `REINTENTO|BARRIDO` y luego normal.
- [x] Pasa en los canales 6, 13 y 1. (2026-10-12: el receptor recibe el mismo mensaje varias
      veces desde canales vecinos durante el barrido, 3 copias en el 6 y 5 en el 13; solo la
      primera es `NUEVO`)
- Nota: los canales de 2.4 GHz se solapan. El colgante podría encontrar al receptor desde un
  canal vecino (5 o 7) con RSSI más bajo. Si después funciona sin barrer, no es un fallo; anotar
  el RSSI.

**6. Duplicados**

- 6a. Escribir `p` en el receptor y presionar el pulsador. Receptor, dos líneas con la misma
  `seq`:
  `ALERTA ... flags=- NUEVO ack=OMITIDO (comando p)` y luego
  `ALERTA ... flags=REINTENTO DUPLICADO ack=OK+DUPLICADO`. Buzzer: un beep corto (no el de error).
- 6b. Primer mensaje tras el arranque con el ACK perdido: escribir `p` y presionar RST del
  colgante. Receptor:
  `HEARTBEAT ... flags=ARRANQUE_FRIO NUEVO ack=OMITIDO` y luego
  `HEARTBEAT ... flags=REINTENTO|ARRANQUE_FRIO DUPLICADO ack=OK+DUPLICADO`, misma `seq`. Sin
  sonido. Es el caso corregido en la regla de duplicados.
- 6c. Escribir `o` (la base "se reinicia") y presionar: `ALERTA ... NUEVO`, se acepta aunque la
  base no tenga historia.
- [x] En 6a y 6b hay un solo `NUEVO` por `seq`. (2026-10-12: 6c también pasa)

**7. HEARTBEAT por timer (intervalo corto de prueba)**

- Hacer: con `HEARTBEAT_PRUEBA_S = 30U`, no tocar nada durante 2 minutos.
- Receptor: una línea `HEARTBEAT ... flags=- ... NUEVO ack=OK` cada ~30 s (los `ms` de líneas
  seguidas difieren unos 30 000 más el tiempo despierto); `seq` sube de 1 en 1.
- Buzzer: silencio.
- [x] Al menos 3 heartbeats seguidos. Anotar el intervalo real. (2026-10-12: ~30.1 s)

**8. Despertar desde deep sleep**

- Hacer: entre dos heartbeats, comprobar que el colgante duerme: su puerto COM desaparece en
  Windows y el receptor no muestra nada. Presionar el pulsador en ese momento.
- Receptor: `ALERTA ... NUEVO ack=OK` en menos de un segundo. Buzzer: beep corto.
- Después de la alerta, los heartbeats siguen llegando cada ~30 s.
- [ ] Pasa 5 veces en distintos momentos del intervalo. (2026-10-12: las 6 alertas de la prueba 2
      despertaron al colgante desde deep sleep, pero no se registró en qué momento del intervalo)
- [x] Ninguna línea `ALERTA` aparece sin haber presionado (dejarlo 10 minutos sin tocar: solo
      heartbeats). (2026-10-12: 20 heartbeats, ninguna alerta)

**9. Potencia de transmisión**

En la primera prueba el RSSI fue de -84 a -92 dBm a solo 20 cm
(`pruebas/resultados/2026-10-12_colgante_banco.md`). Se sospechaba que bajar la potencia mejoraría
la señal de la antena del Super Mini. Esta prueba elige `COLGANTE_TX_POTENCIA_QDBM` (`config.h`,
unidades de 0.25 dBm, rango 8-84).

- Para cada potencia de la tabla:
  1. Poner el valor en `COLGANTE_TX_POTENCIA_QDBM`, compilar y cargar (con
     `HEARTBEAT_PRUEBA_S = 30U`).
  2. Colgante a ~3 m del receptor, con línea de vista, siempre en la misma posición y orientación,
     sin tocarlo. Receptor elevado, en el canal 1, sin moverlo entre mediciones.
  3. Anotar el RSSI de cada heartbeat (al menos 10), el promedio, el mínimo, el máximo y cuántos
     llegaron con `REINTENTO`.
- Elegir la potencia con mejor RSSI promedio. Si dos quedan parecidas (menos de 3 dB de
  diferencia), preferir la más baja: consume menos.
- [x] Tabla llena y valor elegido en `config.h`. (2026-10-12: `80`. La potencia alta dio ~6 dB más
      que la baja; en esta placa bajar a 8.5 dBm no mejora la señal)

| Potencia (qdBm) | dBm | Distancia | Heartbeats | RSSI promedio (dBm) | Mínimo | Máximo | Con `REINTENTO` |
|---|---|---|---|---|---|---|---|
| 34 | 8.5 | 3 m | 20 | -91.1 | -94 | -89 | 0 |
| 80 | 20 | 3 m | 14 | -85.4 | -87 | -85 | 0 |

Aparte: la altura y la orientación cambian mucho el RSSI. Con el receptor más alto y lejos de la
mesa, a 0.6-1 m, el RSSI pasó de ~-90 a ~-74/-80 dBm.

## Lista completa (colgante terminado)

Con la batería, el TP4056 modificado, la carcasa y la base real. La batería, el TP4056 y el
circuito se armaron el 2026-10-27; la carcasa todavía no.

### Conexión final (2026-10-27)

| Elemento | Conexión |
|---|---|
| Batería Li-ion 240 mAh | Soldada a `B+` / `B-` del TP4056 HW-373 V1.2.1 (con protección). Medía 3.86 V antes de cargar |
| Alimentación | `OUT+` → pin `5V` del C3, `OUT-` → `G` |
| Divisor de batería | `OUT+` — 100 kΩ — GPIO1 — 100 kΩ — `G`, sin capacitor. 2.1 V en GPIO1 con 4.2 V |
| Pulsador | GPIO3 ↔ `G` |
| Buzzer azul (PNP S8550) | `VCC` a `3V3`, `I/O` a GPIO10 (activo en bajo), `GND` a `G` |
| Corriente de carga | R3 (1.202 kΩ, 1 A) quitada; 4.7 kΩ THT de la patita 2 (`PROG`) del TP4056 al pad `OUT-`, porque un pad de R3 quedó dañado |

**Regla: nunca conectar el USB del C3 con la batería conectada.** En el C3 Super Mini el pin
`5V` y el VBUS del USB están unidos, sin selector de fuente: el USB metería 5 V en la batería.
La batería se carga solo por el USB-C del TP4056. Para programar o ver el monitor, desconectar
primero `OUT+` del pin `5V` (irá con un conector header/dupont). Por USB, sin `OUT+`, la batería
se lee cerca de 0 mV con `FLAG_BATERIA_BAJA`; es lo esperado.

Por eso el monitor Serial no sirve con la batería conectada: las pruebas con batería se ven en el
monitor de la base o del receptor de prueba.

### 0. Preparación y seguridad

- [x] R3 del TP4056 cambiada por 4.7 kΩ. (2026-10-27: 0.1 Ω entre la patita 2 y el pad de R3
      del lado `PROG`, 4.65 kΩ entre la patita 2 y `OUT-`. Cargando, V(`PROG`) = 0.918 V, es decir
      `0.918 / 4650 × 1200 ≈ 237 mA`. No se midió en serie: fusible de mA quemado)
- [ ] Corriente de ~237 mA aceptada por la hoja de datos de la celda.
- [ ] Polaridad de `BAT+` / `BAT-` verificada antes de soldar. Celda sin calentamiento al cargar.
- [x] Identificadas en el módulo real las salidas protegidas `OUT+` / `OUT-`. (2026-10-27)
- [x] Definido el pin de alimentación de la Super Mini: `OUT+` al pin `5V`. (2026-10-27)
- [ ] La placa arranca y transmite con la batería entre 3.5 y 4.2 V (a 3.5 V el regulador de la
      placa puede quedarse corto de 3.3 V; anotar desde qué voltaje deja de funcionar).
- [ ] Conector header/dupont en `OUT+` para desconectarlo al programar.
- [ ] GPIO1 nunca supera 2.4 V (`BATERIA_ADC_LIMITE_MV`) con la batería a 4.2 V: con el divisor
      100 kΩ / 100 kΩ debe dar ~2.1 V. Medido con multímetro.
- [ ] `secrets.h` con la MAC STA real de la base y `COLGANTE_ID`; nunca subirlo al repositorio.

### 1. Botón (GPIO3)

- [ ] Con `REGISTROS_SERIAL_HABILITADOS = 1`, una pulsación con el equipo dormido produce una
      sola ALERTA.
- [ ] Pulsación breve (soltar de inmediato): igual produce ALERTA ("ya fue liberado; alerta
      aceptada").
- [ ] Mantener presionado más de 5 s: una sola alerta; el registro indica revisión por timer y no
      hay alertas repetidas mientras se mantiene.
- [ ] Al soltar tras un botón atascado, el siguiente wake por timer de 5 s no transmite ni suena,
      y luego vuelve a aceptar pulsaciones.
- [ ] Sin tocar el botón durante al menos 1 hora: ningún wake GPIO falso. Si aparecen, probar
      pull-up externo.
- [ ] Rebote real: ninguna alerta doble en 20 pulsaciones seguidas con pausas.

### 2. Buzzer (GPIO10)

- [ ] Módulo del buzzer pasado a la placa final con `VCC` a `3V3` (no 5 V) y sin resistencia de
      GPIO10 a GND; etiqueta "REMOVE SEAL AFTER WASHING" quitada.
- [ ] Ningún sonido durante el deep sleep ni al despertar o encender (retención de GPIO10 en
      alto).
- [ ] Alerta con ACK: un beep corto (~120 ms).
- [ ] Alerta sin base: tres beeps largos (~500 ms con pausas de ~250 ms).
- [ ] Heartbeat, con o sin ACK: ningún sonido.
- [ ] Volumen audible para el usuario con la carcasa cerrada.

### 3. Deep sleep y arranque

- [ ] Primer encendido o reset: un HEARTBEAT con `FLAG_ARRANQUE_FRIO`, sin sonido, y vuelve a
      dormir.
- [ ] Wake por timer cada 15 min: HEARTBEAT sin sonido (verificar al menos 3 ciclos seguidos).
- [ ] Secuencia y canal se conservan entre wakes (la secuencia crece de 1 en 1 en la base).
- [ ] Tras quitar y volver a conectar la batería, el primer mensaje llega con `FLAG_ARRANQUE_FRIO`
      y la base lo acepta.

### 4. Batería y ADC

- [ ] `BATERIA_MODO_SIMULADO = 0` cargado. (En `config.h` desde el 2026-10-27; falta cargarlo
      y ver los valores en la base)
- [ ] Comparar el valor reportado (`bat=` en la base) con el multímetro en `OUT+` en al menos
      4.2, 3.9, 3.7, 3.5 y 3.3 V (o lo que permita la descarga), con y sin radio activo. Anotar el
      error. El divisor no tiene capacitor y la fuente es de 50 kΩ: un error fijo de unas decenas
      de mV o lecturas que varían más de ~35 mV entre heartbeats seguidos indican que hace falta
      un capacitor de 10-100 nF de GPIO1 a GND (ver `README.md`, "Lectura real").
- [ ] Error de las resistencias reales del divisor medido (las dos de 100 kΩ, sueltas).
- [ ] Curva de descarga registrada; voltaje de corte de la protección identificado.
- [ ] `BATERIA_BAJA_MV` confirmado o ajustado con esa curva (hoy 3500 mV, hipótesis).
- [ ] Si se puede provocar de forma segura una lectura de GPIO1 por encima de 2400 mV, el paquete
      lleva `0 mV` y `FLAG_BATERIA_BAJA`. No forzar más de 3.3 V en el pin.

### 5. Enlace ESP-NOW con la base

- [ ] ALERTA recibida por la base con `version = 1`, `tipo = 1`, el `COLGANTE_ID` y la batería.
- [ ] ACK recibido dentro de 100 ms en el canal guardado; registrar la latencia real.
- [ ] Con la base apagada: reintentos, barrido 1-13 y patrón de error en ~1.6 s más el arranque
      de WiFi; medir el tiempo total.
- [ ] Cambiar el canal del router: el colgante lo encuentra por barrido, guarda el canal y el
      siguiente mensaje sale directo en él.
- [ ] Router en un canal fuera de 1-13 o solo en 5 GHz: documentar que el colgante no conecta.
- [ ] Una base o equipo con otra MAC no puede confirmar la alerta.

### 6. Duplicados y retransmisiones

- [ ] Forzar la pérdida del ACK (por ejemplo, alejando el colgante en el límite de alcance): la
      base recibe reintentos con `FLAG_REINTENTO` y no repite la alerta ni el aviso.
- [ ] La base responde `FLAG_ACK_DUPLICADO` a los duplicados y el colgante da beep de
      confirmación.
- [ ] Primer mensaje tras arranque en frío con ACK perdido: los reintentos con
      `FLAG_ARRANQUE_FRIO` no generan alertas repetidas en la base.

### 7. Alcance y uso real

- [ ] Alcance en la casa: alerta confirmada desde las habitaciones y el baño; anotar distancias y
      paredes.
- [ ] Base reconectando WiFi: comportamiento del colgante mientras la base cambia de canal.

### 8. Consumo y autonomía

- [ ] Corriente en deep sleep de la placa completa (LED, regulador, divisor de 21 µA, pull-up).
      (2026-10-27: no se pudo medir, el fusible de la escala de mA del multímetro está quemado)
- [ ] Carga por alerta y por heartbeat (corriente y duración con radio activo).
- [ ] Autonomía estimada con la celda de 240 mAh y un heartbeat cada 15 min.
- [ ] Con `REGISTROS_SERIAL_HABILITADOS = 0`, comparar el tiempo despierto con el modo de
      registros.

### 9. Carcasa y cordón

- [ ] Carcasa impresa: botón accesible y fácil de presionar, buzzer audible, puerto USB-C
      accesible para cargar.
- [ ] Cordón resistente y cómodo; la carcasa no presiona el botón por accidente.
- [ ] Repetir los puntos 1, 2 y 5 con el colgante cerrado.
