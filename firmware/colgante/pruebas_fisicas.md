# Pruebas físicas del colgante

Dos partes:

- **Primera prueba en protoboard** (abajo): ESP32-C3 Super Mini por USB, sin batería, con el
  pulsador, el buzzer activo y el receptor de prueba `firmware/pruebas/receptor_espnow` en un
  ESP32 clásico haciendo de base.
- **Lista completa** (más abajo): para el colgante terminado, con batería, TP4056 y la base
  real.

Registrar cada sesión en `pruebas/` (fecha, placa, firmware/commit, lo medido, resultado). Marcar
una casilla solo con la medición hecha; si algo falla, anotar qué se observó y no marcarla.

## Primera prueba en protoboard

### Conexión

| Elemento | Pin del C3 Super Mini | Conexión |
|---|---|---|
| Pulsador 12x12 | GPIO3 (serigrafía `3`) | Pulsador entre GPIO3 y GND. Pull-up interno; no hace falta resistencia |
| Buzzer activo | GPIO10 (serigrafía `10`) | Mediante transistor NPN (abajo), con 10 kΩ de GPIO10 a GND |
| Alimentación | USB-C | Sin batería por ahora. El buzzer se alimenta del pin `3V3` |

```
  Pulsador                         Buzzer activo con transistor NPN (2N2222, S8050 o BC547)

  GPIO3 ───┐                       3V3 ─────────────┬──────────────┐
           │                                        │ (+)          │
         [ ] pulsador                            [buzzer]       [1N4148]  solo si el buzzer
           │                                        │ (−)          │       es magnético; el
  GND ─────┘                                        ├──────────────┘       cátodo (raya) a 3V3
                                                    │
                                                    C
  GPIO10 ──────┬────[ 1 kΩ ]──────────────────── B  NPN
               │                                    E
            [10 kΩ]                                 │
               │                                    │
  GND ─────────┴────────────────────────────────────┘
```

- **Pulsador de 4 patas:** las patas van unidas por pares por dentro. Usar dos patas en
  diagonal: así siempre se conecta a través del interruptor y no por el par que ya está unido.
- **¿Buzzer directo al pin o con transistor?** Con transistor. Un buzzer activo de 12 mm consume
  unos 25-30 mA a 5 V y unos 15-25 mA a 3.3 V (cambia según el modelo; no tenemos su hoja de
  datos). El GPIO del ESP32-C3 entrega unos 20 mA con la fuerza de salida por defecto. Estaríamos
  en el límite: el pin cae de voltaje, el buzzer suena más débil y el pin trabaja forzado. El NPN
  toma la corriente desde `3V3` y el GPIO solo da ~2.6 mA a la base a través de 1 kΩ.
  - Directo solo si se mide antes: conectar el buzzer entre `3V3` y GND con el multímetro en
    serie (en mA). Si marca 12 mA o menos, puede ir directo de GPIO10 (+) a GND (−).
  - El firmware no cambia: GPIO10 en alto enciende el transistor y suena; en bajo, silencio.
- **10 kΩ de GPIO10 a GND:** en deep sleep el pin queda flotando y el transistor podría
  encenderse a medias (zumbido leve o consumo). La resistencia lo mantiene en bajo. Con el
  buzzer directo al pin también va, entre GPIO10 y GND.
- **Diodo 1N4148:** solo para un buzzer magnético (bobina; suele tener una etiqueta "remove
  seal after washing" y no se ve el disco piezo). Protege al transistor del pico al apagar la
  bobina. Un buzzer piezoeléctrico no lo necesita.
- **Polaridad del buzzer:** la pata larga o la marca `+` va a `3V3`.
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
- [ ] Pasa. Anotar el tiempo `ack=... us` y el RSSI.

**2. ALERTA con ACK**

- Hacer: con el colgante ya dormido (un segundo después del arranque), presionar el pulsador
  una vez.
- Receptor: `ALERTA id=1 seq=R+1 flags=- ... NUEVO ack=OK`. Ya no lleva `ARRANQUE_FRIO`, porque
  el heartbeat del arranque recibió su ACK. Una sola línea por pulsación.
- [ ] Pasa en 5 pulsaciones seguidas; `seq` sube de 1 en 1.

**3. Beep de confirmación**

- Hacer: lo mismo que en la prueba 2, fijándose en el sonido.
- Buzzer: un beep corto (~120 ms) poco después de soltar el botón.
- [ ] Un solo beep corto por alerta, audible.
- [ ] Mantener el botón presionado más de 5 s: una sola línea `ALERTA` en el receptor y un solo
      beep; al soltar no sale otra alerta.

**4. Colgante sin receptor (patrón de error)**

- Hacer: desconectar el USB del receptor y presionar el pulsador.
- Receptor: nada (está apagado).
- Buzzer: tres beeps largos (~0.5 s, con pausas de ~0.25 s) unos 2 s después de presionar
  (1.6 s de reintentos y barrido más el arranque de WiFi).
- Volver a conectar el receptor y presionar de nuevo: `ALERTA ... flags=- NUEVO ack=OK` y beep
  corto. La `seq` salta 2: la alerta sin respuesta también gastó un número.
- [ ] Pasa. Anotar el tiempo aproximado hasta el patrón de error.

**5. Barrido cuando el receptor cambia de canal**

- Hacer: en el monitor del receptor escribir `6` y Enter. Presionar el pulsador.
- Receptor: `ALERTA ... flags=REINTENTO|BARRIDO ... canal=6 NUEVO ack=OK`.
- Buzzer: beep corto, algo más tarde que en la prueba 2 (unos 0.8 s).
- Presionar otra vez: `ALERTA ... flags=- ... NUEVO`. El colgante guardó el canal y ya no barre.
- Volver al canal `1` y repetir: de nuevo una línea con `REINTENTO|BARRIDO` y luego normal.
- [ ] Pasa en los canales 6, 13 y 1.
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
- [ ] En 6a y 6b hay un solo `NUEVO` por `seq`.

**7. HEARTBEAT por timer (intervalo corto de prueba)**

- Hacer: con `HEARTBEAT_PRUEBA_S = 30U`, no tocar nada durante 2 minutos.
- Receptor: una línea `HEARTBEAT ... flags=- ... NUEVO ack=OK` cada ~30 s (los `ms` de líneas
  seguidas difieren unos 30 000 más el tiempo despierto); `seq` sube de 1 en 1.
- Buzzer: silencio.
- [ ] Al menos 3 heartbeats seguidos. Anotar el intervalo real.

**8. Despertar desde deep sleep**

- Hacer: entre dos heartbeats, comprobar que el colgante duerme: su puerto COM desaparece en
  Windows y el receptor no muestra nada. Presionar el pulsador en ese momento.
- Receptor: `ALERTA ... NUEVO ack=OK` en menos de un segundo. Buzzer: beep corto.
- Después de la alerta, los heartbeats siguen llegando cada ~30 s.
- [ ] Pasa 5 veces en distintos momentos del intervalo.
- [ ] Ninguna línea `ALERTA` aparece sin haber presionado (dejarlo 10 minutos sin tocar: solo
      heartbeats).

## Lista completa (colgante terminado)

Para cuando estén disponibles la batería, el TP4056 modificado, la carcasa y la base con
`espnow_rx` implementado. Ninguna se ha ejecutado todavía.

### 0. Preparación y seguridad

- [ ] R3 del TP4056 cambiada por 4.7 kΩ. Medir la corriente de carga con multímetro en serie:
      cerca de 255 mA y aceptada por la hoja de datos de la celda.
- [ ] Polaridad de `BAT+` / `BAT-` verificada antes de soldar. Celda sin calentamiento al cargar.
- [ ] Identificadas en el módulo real las salidas protegidas `OUT+` / `OUT-`.
- [ ] Definido y probado el pin de alimentación de la Super Mini desde `OUT+` (5V o 3V3); la
      placa arranca con la batería entre 3.5 y 4.2 V.
- [ ] GPIO1 nunca supera 1.5 V con la batería a 4.2 V (medido con multímetro antes de conectar al
      ESP32-C3).
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

- [ ] Circuito del buzzer de la prueba en protoboard (transistor o directo según la corriente
      medida) pasado a la placa final, con la resistencia de 10 kΩ de GPIO10 a GND.
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

- [ ] `BATERIA_MODO_SIMULADO = 0` cargado.
- [ ] Comparar el valor reportado con el multímetro en al menos 4.2, 3.9, 3.7, 3.5 y 3.3 V
      (o lo que permita la descarga), con y sin radio activo. Anotar el error.
- [ ] Error de las resistencias reales del divisor medido.
- [ ] Curva de descarga registrada; voltaje de corte de la protección identificado.
- [ ] `BATERIA_BAJA_MV` confirmado o ajustado con esa curva (hoy 3500 mV, hipótesis).
- [ ] Si se puede provocar de forma segura una lectura de GPIO1 por encima de 1500 mV, el paquete
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

- [ ] Corriente en deep sleep de la placa completa (LED, regulador, divisor, pull-up).
- [ ] Carga por alerta y por heartbeat (corriente y duración con radio activo).
- [ ] Autonomía estimada con la celda de 240 mAh y un heartbeat cada 15 min.
- [ ] Con `REGISTROS_SERIAL_HABILITADOS = 0`, comparar el tiempo despierto con el modo de
      registros.

### 9. Carcasa y cordón

- [ ] Carcasa impresa: botón accesible y fácil de presionar, buzzer audible, puerto USB-C
      accesible para cargar.
- [ ] Cordón resistente y cómodo; la carcasa no presiona el botón por accidente.
- [ ] Repetir los puntos 1, 2 y 5 con el colgante cerrado.
