# Pruebas físicas del colgante

Lista de verificación para cuando estén disponibles el ESP32-C3 Super Mini, la batería, el
TP4056 modificado, el botón, el buzzer y la base con `espnow_rx` implementado. Ninguna se ha
ejecutado todavía.

Registrar cada sesión en `pruebas/` (fecha, placa, firmware/commit, lo medido, resultado). Marcar
una casilla solo con la medición hecha; si algo falla, anotar qué se observó y no marcarla.

## 0. Preparación y seguridad

- [ ] R3 del TP4056 cambiada por 4.7 kΩ. Medir la corriente de carga con multímetro en serie:
      cerca de 255 mA y aceptada por la hoja de datos de la celda.
- [ ] Polaridad de `BAT+` / `BAT-` verificada antes de soldar. Celda sin calentamiento al cargar.
- [ ] Identificadas en el módulo real las salidas protegidas `OUT+` / `OUT-`.
- [ ] Definido y probado el pin de alimentación de la Super Mini desde `OUT+` (5V o 3V3); la
      placa arranca con la batería entre 3.5 y 4.2 V.
- [ ] GPIO1 nunca supera 1.5 V con la batería a 4.2 V (medido con multímetro antes de conectar al
      ESP32-C3).
- [ ] `secrets.h` con la MAC STA real de la base y `COLGANTE_ID`; nunca subirlo al repositorio.

## 1. Botón (GPIO3)

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

## 2. Buzzer (GPIO10)

- [ ] Corriente del buzzer medida y dentro de lo seguro para el GPIO; si no, transistor.
- [ ] Alerta con ACK: un beep corto (~120 ms).
- [ ] Alerta sin base: tres beeps largos (~500 ms con pausas de ~250 ms).
- [ ] Heartbeat, con o sin ACK: ningún sonido.
- [ ] Volumen audible para el usuario con la carcasa cerrada.

## 3. Deep sleep y arranque

- [ ] Primer encendido o reset: no transmite y vuelve a dormir.
- [ ] Wake por timer cada 15 min: HEARTBEAT sin sonido (verificar al menos 3 ciclos seguidos).
- [ ] Secuencia y canal se conservan entre wakes (la secuencia crece de 1 en 1 en la base).
- [ ] Tras quitar y volver a conectar la batería, el primer mensaje llega con `FLAG_ARRANQUE_FRIO`
      y la base lo acepta.

## 4. Batería y ADC

- [ ] `BATERIA_MODO_SIMULADO = 0` cargado.
- [ ] Comparar el valor reportado con el multímetro en al menos 4.2, 3.9, 3.7, 3.5 y 3.3 V
      (o lo que permita la descarga), con y sin radio activo. Anotar el error.
- [ ] Error de las resistencias reales del divisor medido.
- [ ] Curva de descarga registrada; voltaje de corte de la protección identificado.
- [ ] `BATERIA_BAJA_MV` confirmado o ajustado con esa curva (hoy 3500 mV, hipótesis).
- [ ] Si se puede provocar de forma segura una lectura de GPIO1 por encima de 1500 mV, el paquete
      lleva `0 mV` y `FLAG_BATERIA_BAJA`. No forzar más de 3.3 V en el pin.

## 5. Enlace ESP-NOW con la base

- [ ] ALERTA recibida por la base con `version = 1`, `tipo = 1`, el `COLGANTE_ID` y la batería.
- [ ] ACK recibido dentro de 100 ms en el canal guardado; registrar la latencia real.
- [ ] Con la base apagada: reintentos, barrido 1-13 y patrón de error en ~1.6 s más el arranque
      de WiFi; medir el tiempo total.
- [ ] Cambiar el canal del router: el colgante lo encuentra por barrido, guarda el canal y el
      siguiente mensaje sale directo en él.
- [ ] Router en un canal fuera de 1-13 o solo en 5 GHz: documentar que el colgante no conecta.
- [ ] Una base o equipo con otra MAC no puede confirmar la alerta.

## 6. Duplicados y retransmisiones

- [ ] Forzar la pérdida del ACK (por ejemplo, alejando el colgante en el límite de alcance): la
      base recibe reintentos con `FLAG_REINTENTO` y no repite la alerta ni el aviso.
- [ ] La base responde `FLAG_ACK_DUPLICADO` a los duplicados y el colgante da beep de
      confirmación.
- [ ] Primer mensaje tras arranque en frío con ACK perdido: los reintentos con
      `FLAG_ARRANQUE_FRIO` no generan alertas repetidas en la base.

## 7. Alcance y uso real

- [ ] Alcance en la casa: alerta confirmada desde las habitaciones y el baño; anotar distancias y
      paredes.
- [ ] Base reconectando WiFi: comportamiento del colgante mientras la base cambia de canal.

## 8. Consumo y autonomía

- [ ] Corriente en deep sleep de la placa completa (LED, regulador, divisor, pull-up).
- [ ] Carga por alerta y por heartbeat (corriente y duración con radio activo).
- [ ] Autonomía estimada con la celda de 240 mAh y un heartbeat cada 15 min.
- [ ] Con `REGISTROS_SERIAL_HABILITADOS = 0`, comparar el tiempo despierto con el modo de
      registros.

## 9. Carcasa y cordón

- [ ] Carcasa impresa: botón accesible y fácil de presionar, buzzer audible, puerto USB-C
      accesible para cargar.
- [ ] Cordón resistente y cómodo; la carcasa no presiona el botón por accidente.
- [ ] Repetir los puntos 1, 2 y 5 con el colgante cerrado.
