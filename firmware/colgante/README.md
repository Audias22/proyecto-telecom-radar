# firmware/colgante

Firmware del ESP32-C3 Super Mini del colgante.

Ciclo: deep sleep -> despierta por botón o por timer -> envía ALERTA o HEARTBEAT por ESP-NOW ->
espera ACK (reintentos y barrido de canales) -> emite sonido solo para la alerta -> deep sleep.
Detalle en `docs/protocolo.md`.

## Módulos (`src/`)

| Módulo | Responsabilidad |
|---|---|
| `boton` | Botón de auxilio, antirrebote y causa del despertar (implementado) |
| `espnow_tx` | Envío, espera de ACK, reintentos, barrido, canal y secuencia en RTC (implementado) |
| `energia` | Deep sleep, fuentes de despertar, beeps de confirmación y error (implementado) |
| `bateria` | Voltaje medido por ADC con el divisor de GPIO1, o simulado para desarrollo (implementado; medición real activa) |
| `despertar` | Decisión pura ALERTA / HEARTBEAT / ninguna a partir de reset, causa de wake y recuperación (solo header) |

`main.cpp` integra el ciclo completo de los cuatro módulos. Lee el motivo de reset, la causa del
despertar, el bit de GPIO3 y la recuperación del botón atascado, y delega la decisión en
`despertar::decidir()`, que no depende de ESP-IDF y se prueba en host (`pruebas_host/`).

## Ciclo principal integrado

- Antes de interpretar el despertar se consulta `energia::recuperandoBotonAtascado()`. En esa
  ruta no se lee la batería, no se enciende el radio y no se emiten sonidos; solamente se vuelve
  al procedimiento de suspensión, que revisa de forma limitada si GPIO3 ya fue liberado.
- Un despertar desde deep sleep (`ESP_RST_DEEPSLEEP`) puede originar ALERTA o HEARTBEAT; el timer
  genera `MSG_HEARTBEAT` cada 15 minutos. Al encender (`ESP_RST_POWERON`, botón RST, reinicio por
  USB o por software) se envía un `MSG_HEARTBEAT` con `FLAG_ARRANQUE_FRIO`, para que la base
  registre el colgante de inmediato; no suena. Una pulsación hecha durante ese arranque no se
  interpreta como alerta. Un reinicio por fallo (brownout, watchdog, panic) vuelve a dormir sin
  transmitir: con la batería al límite, el pico del radio podría causar otro brownout y un ciclo
  de reinicios.
- Un despertar por GPIO genera una sola `MSG_ALERTA` cuando el estado retenido de wake identifica
  GPIO3. Si el botón continúa bajo, `presionado()` confirma que el nivel sea estable; si ya fue
  liberado durante el arranque, se acepta igualmente el evento retenido para no perder una alerta
  legítima breve. No es posible medir después del arranque cuánto duró ese pulso ni distinguirlo de
  ruido eléctrico. Las pruebas físicas deben confirmar que el pull-up y el cableado no causen wakes
  falsos; si los causan, se necesitará mejorar el circuito o añadir filtrado hardware. Antes de
  rearmar el wake, `energia::dormir()` exige una liberación estable con el antirrebote existente.
- Antes de cada mensaje se mide el voltaje de la batería (`BATERIA_MODO_SIMULADO=0`). Con `1` se
  transmiten `3900 mV` solo para desarrollar el flujo y el registro advierte que no es una
  medición física.
- `espnow_tx::enviar()` determina la entrega exclusivamente por ACK de aplicación. En una alerta,
  `ENVIO_OK` o `ENVIO_OK_BARRIDO` produce un beep corto; `ENVIO_SIN_ACK`, incluida una MAC nula o
  una inicialización fallida, produce tres beeps de error. Un heartbeat nunca produce sonidos,
  tenga o no ACK.
- El radio se detiene antes de suspender. Si `energia::dormir()` retorna por un error, `loop()` no
  reconstruye ni retransmite el evento y no vuelve a sonar: espera y reintenta únicamente la
  suspensión. Esto evita duplicados y bucles rápidos, aunque un fallo persistente mantendrá el
  dispositivo despierto y elevará el consumo hasta que la configuración pueda completarse o el
  equipo se reinicie.
- `REGISTROS_SERIAL_HABILITADOS` controla la inicialización y los mensajes de integración por USB
  CDC. Debe ponerse en `0` para la operación normal de bajo consumo. Los registros no muestran la
  MAC de la base, el identificador privado ni otras credenciales.

## Transmisión ESP-NOW

- `iniciar()` activa WiFi únicamente en modo STA, sin asociarlo a un router, desactiva el ahorro
  de energía durante la transmisión, selecciona el último canal válido conservado en RTC e
  inicializa ESP-NOW con la MAC unicast de `include/secrets.h`.
- La MAC se copia a memoria propia al compilar y se valida antes de encender el enlace. Una MAC
  cero o multicast deja el transmisor inactivo; `enviar()` devuelve `ENVIO_SIN_ACK`. El archivo
  `secrets.h` continúa ignorado por Git y debe crearse desde `secrets.example.h`.
- Cada mensaje nuevo incrementa una sola vez la secuencia RTC. Todos sus reintentos conservan esa
  secuencia; a partir del segundo intento llevan `FLAG_REINTENTO`. Los envíos del barrido llevan
  además `FLAG_BARRIDO` y recorren los canales 1 a 13 con los intentos y tiempos de
  `protocolo.h`.
- Después de un arranque que no sea desde deep sleep, los paquetes llevan `FLAG_ARRANQUE_FRIO`
  hasta que llega un ACK de aplicación válido. Aceptar una trama en `esp_now_send()` no basta para
  limpiar el indicador: así una pérdida total de ACK no deja a la base comparando la secuencia
  reiniciada contra un valor antiguo. El canal y la secuencia sobreviven al deep sleep. Tras un
  arranque en frío la secuencia empieza en un valor aleatorio (`esp_random()`, sorteado con WiFi
  ya encendido para que el generador por hardware sea realmente aleatorio).
- El callback de recepción solo copia a una cola estática paquetes de exactamente 8 bytes que
  provienen de la MAC configurada. El flujo principal acepta el ACK únicamente si coinciden
  versión, tipo `MSG_ACK`, id del colgante, secuencia y canal. ACK ajenos o tardíos no prolongan
  el tiempo límite ni pueden guardar un canal incorrecto.
- El resultado del callback de entrega 802.11 no se usa como confirmación. La entrega se considera
  exitosa exclusivamente al recibir el ACK de aplicación. `detener()` desregistra el callback y
  el peer, detiene ESP-NOW y apaga WiFi incluso después de una inicialización parcial.
- La construcción de paquetes, validación de ACK, validación de canal y composición de flags están
  separadas como funciones puras en `espnow_tx::logica`, sin dependencias de WiFi. Esto permite
  pruebas unitarias automatizadas en un entorno host con paquetes sintetizados. La compilación del
  firmware verifica además varios casos de lógica y las firmas reales de Arduino ESP32 3.3.7;
  esas verificaciones no sustituyen pruebas de comunicación por radio.

Queda pendiente probar con una base real la MAC, recepción del ACK, pérdida de tramas, cambio de
canal del router, recorrido completo 1-13, persistencia RTC tras deep sleep, alcance y consumo del
radio. También se debe medir si los tiempos iniciales de `protocolo.h` son suficientes en el
entorno de instalación.

ESP-NOW se usa sin PMK/LMK, según la primera versión del protocolo. Por tanto, validar la MAC de
origen detecta tráfico ajeno normal, pero no autentica criptográficamente a la base: un atacante
con radio cercano podría falsificar la MAC y un ACK. Si el modelo de amenaza exige impedirlo,
habrá que añadir cifrado ESP-NOW de forma coordinada con la base y gestionar las claves fuera del
repositorio.

## Botón de auxilio

- Se conecta entre GPIO3 y GND y se configura como `INPUT_PULLUP`; por eso está activo en nivel
  bajo.
- Una pulsación o liberación debe permanecer estable durante `BOTON_ANTIRREBOTE_MS` (30 ms por
  defecto) para aceptarse.
- `despertoPorBoton()` distingue `ESP_SLEEP_WAKEUP_GPIO` del despertar periódico por temporizador
  y verifica que GPIO3 esté presente en el estado de wake.
- `presionado()` devuelve el estado estable, no un evento nuevo en cada llamada. En el ciclo del
  firmware se debe generar una sola alerta por despertar, aunque el botón continúe presionado.
- Antes de dormir, `esperarSoltar()` permite esperar una liberación estable. La espera está
  limitada por `BOTON_ESPERA_LIBERACION_MS` (5 s por defecto): devuelve `true` si se liberó y
  `false` si agotó el tiempo, sin quedar bloqueada.
- `energia::dormir()` habilita GPIO3 en nivel bajo solamente cuando `esperarSoltar()` devuelve
  `true`. Si el botón sigue presionado, omite temporalmente esa fuente y programa una revisión por
  timer para evitar despertares inmediatos y alertas repetidas.

## Energía y deep sleep

- En operación normal se habilitan simultáneamente GPIO3 activo en bajo y el timer de
  `HEARTBEAT_INTERVALO_S` (15 minutos). Ambas API pueden coexistir en el ESP32-C3.
- Si el botón no se libera en 5 segundos, el equipo duerme sin wake por GPIO y despierta después
  de `BOTON_REVISION_ATASCADO_S` (5 segundos por defecto) para comprobarlo de nuevo. En los wakes
  siguientes solo dedica `BOTON_REVISION_LECTURA_MS` (100 ms) a confirmar el nivel, en lugar de
  repetir la espera activa de 5 segundos.
- El estado se conserva en RTC y `recuperandoBotonAtascado()` permite que el flujo principal
  omita radio, heartbeat y sonidos durante ese wake de recuperación. El indicador se acepta solo
  cuando la causa de wake es el timer; un reinicio por otra causa lo limpia como obsoleto.
- Cuando se detecta la liberación, el wake activo en bajo se restaura automáticamente; no queda
  deshabilitado permanentemente. Durante los 5 segundos de recuperación no se puede distinguir una
  liberación seguida de una nueva pulsación: esa es la ventana máxima temporal sin detección. El
  valor deberá ajustarse con mediciones de consumo y pruebas de uso reales.
- Cualquier error al limpiar o habilitar fuentes se informa por Serial, aplica una pausa de 1 s y
  evita entrar en deep sleep con una configuración incompleta. Cuando `dormir()` regresa, `loop()`
  reintenta solo la suspensión, sin retransmitir ni volver a sonar (ver Ciclo principal integrado).
- El buzzer es un módulo de 3 pines con transistor PNP: suena con GPIO10 en nivel bajo
  (`BUZZER_ACTIVO_EN_BAJO 1` en `config.h`; en `0` sirve para un buzzer activo en alto).
  `energia::iniciarBuzzer()`, lo primero de `setup()`, escribe el nivel de apagado antes de
  habilitar la salida y luego suelta la retención del deep sleep, así no hay pitido al encender
  ni al despertar. Antes de `esp_deep_sleep_start()`, `dormir()` deja el pin apagado y lo retiene
  con `gpio_hold_en()` más `gpio_deep_sleep_hold_en()` (en el ESP32-C3 hacen falta las dos). La confirmación dura 120 ms y
  el error son tres sonidos de 500 ms separados por 250 ms. El módulo no invoca estos sonidos al
  dormir ni durante un heartbeat; `main.cpp` los llama únicamente al terminar una alerta.
- Arduino ESP32 3.3.7 habilita por defecto resistencias internas adecuadas al modo de wake dentro
  de `esp_deep_sleep_start()`. Aun así, Espressif recomienda un pull-up externo para wake en nivel
  bajo; su valor y el consumo adicional deben validarse con el circuito real.

Sin hardware no se han validado todavía el rebote real del pulsador, el despertar desde deep
sleep, el consumo de la resistencia interna, la retención del buzzer apagado en deep sleep ni
el comportamiento eléctrico ante una pulsación prolongada. La polaridad del módulo del buzzer
(activo en bajo) sí se comprobó en banco. Si el pull-up interno no mantiene un nivel estable en
deep sleep, habrá que añadir una resistencia externa sin cambiar la lógica activa en bajo.

## Batería y ADC

### Modo simulado

- `BATERIA_MODO_SIMULADO` está en `0` desde el 2026-10-27 (batería y divisor armados). Con `1`,
  `leerMilivoltios()` devuelve
  exactamente `3900 mV`, escribe por Serial que el dato es simulado y no configura ni consulta el
  ADC. `modoSimulado()` permite que `main.cpp` registre que ese valor no es una medición física.
  El paquete no tiene un campo que indique simulación: la base recibe `3900 mV` como cualquier
  otro valor.
- Los `3900 mV` sirven únicamente para ejercitar el formato del paquete y la lógica. No demuestran
  que haya una batería conectada, no describen su carga y no deben almacenarse como telemetría
  física.
- El umbral compartido es estricto: `bateria_mv < BATERIA_BAJA_MV`. Por ello `3499 mV` se considera
  bajo, `3500 mV` no, y el valor simulado de `3900 mV` no activa `FLAG_BATERIA_BAJA`. Esto valida
  lógica de desarrollo, no el estado de una celda real.

### Divisor armado (2026-10-27)

No conectar la Li-ion directamente a GPIO1. Se armó con lo que había: dos resistencias de
100 kΩ, sin capacitor (no había 1 MΩ, 470 kΩ ni 10 nF).

```text
OUT+ del TP4056 --- 100 kΩ ---+--- GPIO1 / ADC1_CH1
                              |
                              +--- 100 kΩ --- G / OUT-
```

- Factor de reconstrucción: `(100 k + 100 k) / 100 k = 2`. Con la batería llena (4.2 V), GPIO1
  recibe 2.1 V; con 3.5 V (`BATERIA_BAJA_MV`), 1.75 V.
- Corriente continua del divisor: `4.2 V / 200 kΩ = 21 µA`, también en deep sleep. Son unos
  0.5 mAh por día, el 0.2 % de la celda de 240 mAh. Es bastante más que los ~2.9 µA del divisor
  de 1 MΩ / 470 kΩ que se había propuesto, y puede pesar en la autonomía si el resto de la placa
  consume poco en deep sleep. Por confirmar con el módulo cuando se pueda medir corriente.
- Impedancia de la fuente vista por el ADC: `100 k || 100 k = 50 kΩ`, sin capacitor.

### Lectura real

`BATERIA_MODO_SIMULADO = 0`. El módulo configura 12 bits y la atenuación `ADC_11db`, descarta la
primera conversión y promedia 16 resultados de `analogReadMilliVolts()`, que usa la calibración
del ESP32-C3 para dar milivoltios del pin. Después multiplica por 2 con redondeo.

Rango del ADC, verificado y no supuesto:

- En arduino-esp32 3.3.7, `ADC_11db` es la `ADC_ATTEN_DB_12` de ESP-IDF (`hal/adc_types.h` del
  framework instalado: `ADC_ATTEN_DB_11` está marcada como obsoleta y vale lo mismo que
  `ADC_ATTEN_DB_12`).
- Hoja de datos del ESP32-C3 (v2.4, tabla 5-6), rango efectivo después de la calibración:
  0-750 mV (ATTEN0), 0-1050 mV (ATTEN1), 0-1300 mV (ATTEN2) y 0-2500 mV (ATTEN3), con un error
  total de ±10 mV en las tres primeras y de ±35 mV en ATTEN3. Los 2.1 V solo caben en ATTEN3.
- `BATERIA_ADC_LIMITE_MV = 2400`. Peor caso esperado: 4.24 V (4.2 V +1 % de la regulación del
  TP4056) con resistencias de 5 % da 2227 mV, más 35 mV de error del ADC, 2262 mV. 2400 queda
  arriba de eso y abajo del final del rango (2500 mV); un `static_assert` lo comprueba.

Sin capacitor y con 50 kΩ: Espressif caracteriza el ADC con un capacitor externo de 100 nF
(tabla 5-5), así que este montaje no está cubierto por la hoja de datos. Se considera aceptable
porque el voltaje de la batería cambia en horas, no en milisegundos: descartar la primera
conversión y promediar 16 reduce el ruido, como recomienda la propia hoja de datos. Riesgos que
quedan, **por confirmar con el módulo**:

- Un error fijo: el capacitor de muestreo del ADC se carga a través de 50 kΩ y puede no llegar al
  voltaje del nodo en cada conversión. El promedio no corrige un error fijo; si aparece, se ve
  como una diferencia constante contra el multímetro.
- Ruido del radio: la lectura se hace antes de transmitir, pero conviene comparar lecturas con y
  sin radio activo.
- Si hace falta, un capacitor de 10-100 nF entre GPIO1 y GND resuelve los dos puntos; entonces
  habría que volver a esperar a que cargue antes de medir (con 50 kΩ y 100 nF, τ = 5 ms).

Al quitar el capacitor se eliminó también la espera de 20 ms antes de medir, que solo servía para
cargarlo.

Una lectura del pin que supere el límite o una conversión imposible devuelve el centinela
`BATERIA_LECTURA_INVALIDA_MV = 0`. Debido a que `PaqueteEspNow.bateria_mv` no tiene un estado de
validez separado, `main.cpp` lo transmite tal cual y lo registra por Serial; la base debe
interpretarlo como lectura no disponible o fallo ADC, no como una medición física de `0 V`. El
paquete lleva además `FLAG_BATERIA_BAJA` por seguridad. `lecturaValida()` permite distinguir el
centinela antes de transmitir.

La calibración interna no corrige tolerancias del divisor ni el error por la impedancia. Hay que
comparar GPIO1 y la batería con un multímetro en varios puntos entre carga completa y descarga
(`pruebas_fisicas.md`, sección 4). Solo esa curva permitirá confirmar o ajustar
`BATERIA_BAJA_MV = 3500 mV`; hasta entonces el umbral sigue siendo una hipótesis de diseño.

## Instalación y carga

Requisitos e instalación de PlatformIO y pioarduino: README principal, sección "Cómo compilar el
firmware". Desde la raíz del repositorio:

```
copy firmware\colgante\include\secrets.example.h firmware\colgante\include\secrets.h
pio run -d firmware/colgante
pio run -d firmware/colgante -t upload
pio device monitor -d firmware/colgante
```

Antes de conectar el USB del C3, desconectar `OUT+` del pin `5V` (ver "Regla: nunca conectar el
USB del C3 con la batería conectada").

Mientras el colgante duerme, el puerto USB desaparece. Para cargar firmware, mantener BOOT
presionado al conectar si el puerto no aparece.

Después de cargar, el colgante envía un HEARTBEAT con `FLAG_ARRANQUE_FRIO` y se duerme. La
primera ALERTA sale al presionar el botón con el equipo ya dormido, y el siguiente HEARTBEAT a los
15 minutos.

## Configuración

`include/secrets.h` (ignorado por Git; copiar de `include/secrets.example.h`):

| Valor | Uso |
|---|---|
| `BASE_MAC` | MAC de la base en modo STA, la que imprime la base por Serial al arrancar. Con la MAC en ceros el colgante no transmite y da el patrón de error en cada alerta. |
| `COLGANTE_ID` | Id del colgante, 1-255, único si hay más de uno. La base identifica al colgante y filtra duplicados por este valor. |

`include/config.h` (versionado). Valores que se deben revisar antes de usar el hardware real:

| Valor | Actual | Para el dispositivo real |
|---|---|---|
| `BATERIA_MODO_SIMULADO` | `0` (mide con el divisor de GPIO1) | `0`. `1` solo para desarrollar sin batería (envía 3900 mV fijos) |
| `REGISTROS_SERIAL_HABILITADOS` | `1` (espera hasta 2 s el USB solo en el arranque en frío) | `0` en operación normal, para reducir tiempo despierto y consumo |
| `COLGANTE_TX_POTENCIA_QDBM` | `80` (20 dBm; unidades de 0.25 dBm) | `80`. Elegido con la prueba 9 de `pruebas_fisicas.md`: a 3 m dio ~6 dB más de RSSI que con `34` (8.5 dBm) |
| `HEARTBEAT_PRUEBA_S` | `0` (heartbeat cada 15 min, de `protocolo.h`) | `0`. Para pruebas en banco, un valor corto como `30` |
| `PIN_BOTON`, `PIN_BATERIA_ADC`, `PIN_BUZZER` | GPIO3, GPIO1, GPIO10 | Los del circuito armado el 2026-10-27 |

Las constantes del enlace (canal, tiempos, reintentos, heartbeat, umbral de batería) están en
`firmware/comun/protocolo/protocolo.h` y se comparten con la base.

## Conexiones del colgante (armado el 2026-10-27)

```text
                 TP4056 HW-373 V1.2.1 (con protección; su USB-C es para cargar)

 Li-ion 240 mAh ── B+          OUT+ ──┬──────────────────── 5V    ESP32-C3 Super Mini
 (soldada)      ── B-          OUT- ──│──────┬───────────── G
                                      │      │
                                   100 kΩ    │
                                      ├──────│──────────── GPIO1 (ADC)
                                   100 kΩ    │
                                      └──────┘
                 PROG (patita 2) ── 4.7 kΩ ── OUT-   (en lugar de R3)

 Pulsador:              GPIO3 ── [ ] ── G
 Buzzer azul (3 pines): VCC ── 3V3,  I/O ── GPIO10,  GND ── G
```

| Elemento | Conexión | Notas |
|---|---|---|
| Batería Li-ion 240 mAh | Soldada a `B+` / `B-` del TP4056 | Medía 3.86 V antes de la primera carga |
| TP4056 HW-373 V1.2.1 (con protección) | Carga por su USB-C. `OUT+` / `OUT-` son la salida protegida | R3 (1 A) reemplazada por 4.7 kΩ (abajo) |
| Alimentación del ESP32-C3 | `OUT+` → pin `5V` del C3, `OUT-` → `G` | El regulador de la placa baja a 3.3 V. Ver la regla del USB abajo |
| Divisor de batería | `OUT+` — 100 kΩ — GPIO1 — 100 kΩ — `G`, sin capacitor | Ver "Batería y ADC". Nunca la batería directa a GPIO1 |
| Pulsador | GPIO3 ↔ GND | Activo en bajo con `INPUT_PULLUP`. GPIO0-GPIO5 son los únicos que despiertan de deep sleep; se evita GPIO2 (strapping). Puede necesitar pull-up externo |
| Módulo de buzzer activo azul (3 pines, PNP S8550 y 1 kΩ en la base) | `VCC` a 3V3, `GND` a G, `I/O` a GPIO10 | Activo en bajo (`BUZZER_ACTIVO_EN_BAJO 1`). `VCC` a 3V3 y no a 5 V: con 5 V el PNP no se apaga con 3.3 V en `I/O`. Sin resistencia de GPIO10 a GND (lo haría sonar siempre); el firmware retiene el pin en alto durante el deep sleep. Quitar la etiqueta "REMOVE SEAL AFTER WASHING". Probado en banco |

### Regla: nunca conectar el USB del C3 con la batería conectada

En el ESP32-C3 Super Mini el pin `5V` y el VBUS del USB-C están unidos y no hay diodo ni
selector de fuente. Con la batería conectada, el USB del C3 metería 5 V en `OUT+` del TP4056, es
decir, en la batería y en el divisor. Por eso:

- Con `OUT+` conectado al pin `5V`, el USB-C del C3 **no se conecta nunca**. La batería se carga
  solo por el USB-C del TP4056.
- Para programar o ver el monitor: desconectar primero `OUT+` del pin `5V` (irá con un conector
  header/dupont) y después conectar el USB del C3. Al terminar, desconectar el USB y volver a
  conectar `OUT+`.
- Con `OUT+` desconectado, el divisor no recibe nada: por USB la lectura de batería da casi 0 V y
  el paquete lleva `FLAG_BATERIA_BAJA`. Es lo esperado, no un fallo.

### Corriente de carga: R3 por 4.7 kΩ

El módulo venía programado a 1 A (R3 medía 1.202 kΩ; `I = 1200 / R_PROG`), demasiado para una
celda de 240 mAh. Se quitó R3; uno de sus pads quedó dañado, así que la resistencia nueva de
4.7 kΩ THT va soldada directo de la patita 2 (`PROG`) del TP4056 al pad `OUT-`.

- Comprobado con el multímetro: 0.1 Ω entre la patita 2 y el pad de R3 del lado `PROG`, y
  4.65 kΩ entre la patita 2 y `OUT-`.
- Cargando, V(`PROG`) = 0.918 V. Con la fórmula de la hoja de datos del TP4056,
  `I = V_PROG / R_PROG × 1200 = 0.918 / 4650 × 1200 ≈ 237 mA`, cerca de 1C para la celda. No se
  midió con el multímetro en serie (fusible de mA quemado).

## Hardware

- Botón en GPIO0-GPIO5 (únicos pines que despiertan al ESP32-C3 de deep sleep).
- Batería Li-ion 240 mAh con cargador TP4056 HW-373 V1.2.1 (con protección, USB-C), cargando a
  ≈237 mA (R3 cambiada por 4.7 kΩ, ver "Conexiones del colgante").
- Por confirmar con el módulo el voltaje de corte por descarga de la protección, para fijar
  `BATERIA_BAJA_MV` con margen sobre ese corte.
- Consumo en deep sleep de la placa completa (LED de encendido, regulador, divisor de 21 µA,
  pull-up): no se pudo medir, el fusible de la escala de mA del multímetro está quemado. Define la
  duración de la batería; por confirmar con el módulo.

## Estado del desarrollo

Rama `colgante/firmware`. Firmware completo en software; nada validado todavía en hardware.

| Commit | Contenido |
|---|---|
| `b5bb5f9` | Botón de emergencia: pull-up, antirrebote, causa del despertar, espera limitada de liberación |
| `8fdf77e` | Energía: deep sleep con wake por GPIO3 y timer, recuperación de botón atascado, buzzer |
| `afa76e9` | ESP-NOW: envío unicast, ACK de aplicación, reintentos, barrido 1-13, canal y secuencia en RTC |
| `44054d4` | Batería: modo simulado y lectura ADC preparada con divisor |
| `5d15ab4` | Flujo principal: ALERTA / HEARTBEAT / ninguna, sonidos solo en alerta, reintento seguro de la suspensión |
| `a4102da` | Pruebas host y decisión de despertar aislada en `src/despertar.h` |

## Pruebas

Las pruebas automatizadas están en `pruebas_host/` (casos, limitaciones y comandos en su README).
Prueban la lógica pura en un PC, sin ESP32-C3 ni base.

| Verificación | Resultado registrado (9 de octubre de 2026) |
|---|---|
| Pruebas host en Windows (zig c++ 0.13.0) | 35 casos y 538 verificaciones aprobados |
| Pruebas host en Linux (g++ 11.4) | 35 casos y 538 verificaciones aprobados |
| `pio run -e colgante` (PlatformIO 6.1.19, arduino-esp32 3.3.7) | Compila sin advertencias del proyecto. RAM 10.8 %, Flash 71.7 % |
| Revisión del 12 de octubre de 2026 (Audias): pruebas host en Windows (g++ MinGW) | 37 casos y 562 verificaciones aprobados (se agregaron los 2 casos de duplicados) |
| Revisión del 12 de octubre de 2026: `pio run -e colgante` | Compila sin advertencias. RAM 10.8 %, Flash 71.7 % |
| Preparación de la prueba en hardware (12 de octubre de 2026): pruebas host | 39 casos y 599 verificaciones aprobados (HEARTBEAT al encender, secuencia inicial aleatoria) |
| Preparación de la prueba en hardware: `pio run -e colgante` | Compila sin advertencias, también con `HEARTBEAT_PRUEBA_S = 30`. RAM 10.8 %, Flash 71.7 % |
| Batería real (27 de octubre de 2026): pruebas host | 43 casos y 653 verificaciones aprobados (divisor 100 kΩ / 100 kΩ, límite ADC 2400 mV) |
| Batería real: `pio run -e colgante` con `BATERIA_MODO_SIMULADO = 0` | Compila. RAM 10.8 % (35 464 B), Flash 72.6 % (951 079 B) |

Que compile y pase las pruebas host no demuestra que el colgante funcione. Radio, botón, buzzer y
deep sleep se probaron en banco por USB (`pruebas_fisicas.md`, primera parte); falta la lectura
del ADC con la batería, el alcance en la casa, la base real y el consumo. La lista de
verificación para el hardware está en `pruebas_fisicas.md`.

## Integración con la base

El colgante depende de que el firmware de la base (`firmware/base-radar`, módulo `espnow_rx`)
cumpla lo siguiente. `espnow_rx` ya está implementado en la base (fase local: canal fijo 1, sin
WiFi); guía de prueba en `firmware/base-radar/README.md`.

- **MAC STA.** La base debe imprimir al arrancar la MAC de su interfaz STA, que es la que va en
  `BASE_MAC`. El colgante descarta toda trama cuya MAC de origen no sea exactamente esa, así que
  el ACK debe salir por la interfaz STA de la base.
- **Canal.** La base queda en el canal del router y el colgante lo encuentra con el barrido 1-13.
  El router debe estar en 2.4 GHz y en un canal de 1 a 13. La base debe usar
  `WiFi.setSleep(false)`. El colgante solo acepta un ACK recibido en el mismo canal en el que
  transmitió, y guarda en RTC el canal que anuncia la base en el ACK (si es 1-13); si el ACK
  trae `0` o un valor fuera de rango, guarda el canal en que lo recibió. Así no se queda en un
  canal vecino en el que oyó el ACK por solape (`docs/protocolo.md`, sección 6).
- **Formato del ACK.** 8 bytes según `protocolo.h`: `version = 1`, `tipo = MSG_ACK (3)`,
  `id_dispositivo` y `secuencia` iguales a los del mensaje recibido, en `bateria_mv` el canal de
  la radio de la base (`ackConCanal()` de `protocolo.h`; `0` = sin dato), y
  `FLAG_ACK_DUPLICADO` si la secuencia ya se había procesado. Enviado en unicast a la MAC de origen
  del colgante, que la base debe registrar como peer de ESP-NOW antes de responder.
- **Tiempo de respuesta.** El colgante espera el ACK 100 ms en su canal guardado y 50 ms por canal
  en el barrido. Si la base responde después de una operación bloqueante (HTTPS a Firebase o
  Telegram), el colgante reintentará, barrerá y dará el patrón de error aunque la alerta haya
  llegado. El ACK debe enviarse antes de cualquier trabajo de red.
- **Identificación.** La base guarda la última secuencia por `id_dispositivo`. Cada colgante
  necesita un `COLGANTE_ID` distinto.
- **Retransmisiones.** Todos los reintentos y el barrido repiten la misma secuencia con
  `FLAG_REINTENTO` (y `FLAG_BARRIDO` en el barrido). La base debe responder ACK también a los
  duplicados, con `FLAG_ACK_DUPLICADO`, y no repetir la alerta.
- **Arranque en frío.** El colgante mantiene `FLAG_ARRANQUE_FRIO` en todos los intentos (también
  en los reintentos) hasta recibir un ACK válido, y empieza la secuencia en un valor aleatorio. La
  base revisa el duplicado antes de aplicar `FLAG_ARRANQUE_FRIO` (`docs/protocolo.md`, sección 5;
  código en `firmware/comun/protocolo/duplicados.h`).
- **Batería.** `bateria_mv = 0` significa lectura no disponible, no 0 V; llega con
  `FLAG_BATERIA_BAJA`. Con `BATERIA_MODO_SIMULADO = 0` el valor es la medición del divisor. Con
  `1`, todos los mensajes llevan 3900 mV y el paquete no indica que sean simulados: la base y el
  panel no deben tratarlos como telemetría real. Por USB (con `OUT+` desconectado) llega cerca de
  0 mV con `FLAG_BATERIA_BAJA`.
- **Ausencia.** Heartbeat cada 15 min; la base marca ausente tras `COLGANTE_AUSENTE_S` (45 min).
- **Sin cifrado.** ESP-NOW sin PMK/LMK en ambos lados (ver "Transmisión ESP-NOW").

## Pendientes físicos

Hecho el 2026-10-27: batería soldada, R3 cambiada (≈237 mA), alimentación desde `OUT+` al pin
`5V`, botón, buzzer y divisor de batería armados. Falta:

- Conector header/dupont en `OUT+` para desconectarlo al programar.
- Medir el consumo en deep sleep: no se pudo, el fusible de la escala de mA del multímetro está
  quemado. Sin ese dato no se puede estimar la autonomía.
- Confirmar en la hoja de datos de la celda que acepta ~237 mA de carga.
- Comparar la lectura de batería con el multímetro (`pruebas_fisicas.md`, sección 4).
- Diseñar e imprimir la carcasa y el cordón.
- Ejecutar el resto de `pruebas_fisicas.md` y ajustar con mediciones: `BATERIA_BAJA_MV`, tiempos
  de ACK, `CANAL_MAX` según el router, y necesidad de pull-up externo para el botón.
