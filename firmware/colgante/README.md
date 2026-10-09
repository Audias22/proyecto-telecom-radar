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
| `bateria` | Voltaje simulado o medido por ADC (implementado; simulación activa) |
| `despertar` | Decisión pura ALERTA / HEARTBEAT / ninguna a partir de reset, causa de wake y recuperación (solo header) |

`main.cpp` integra el ciclo completo de los cuatro módulos. Lee el motivo de reset, la causa del
despertar, el bit de GPIO3 y la recuperación del botón atascado, y delega la decisión en
`despertar::decidir()`, que no depende de ESP-IDF y se prueba en host (`pruebas_host/`).

## Ciclo principal integrado

- Antes de interpretar el despertar se consulta `energia::recuperandoBotonAtascado()`. En esa
  ruta no se lee la batería, no se enciende el radio y no se emiten sonidos; solamente se vuelve
  al procedimiento de suspensión, que revisa de forma limitada si GPIO3 ya fue liberado.
- Solo un reinicio cuyo motivo sea `ESP_RST_DEEPSLEEP` puede originar una transmisión. El timer
  genera `MSG_HEARTBEAT` cada 15 minutos. El encendido en frío, un reinicio inesperado y cualquier
  otra causa vuelven a dormir sin transmitir, evitando alertas falsas. Una pulsación realizada
  durante ese arranque inicial no se interpreta como alerta. El primer mensaje posterior conserva
  `FLAG_ARRANQUE_FRIO`, responsabilidad de `espnow_tx`.
- Un despertar por GPIO genera una sola `MSG_ALERTA` cuando el estado retenido de wake identifica
  GPIO3. Si el botón continúa bajo, `presionado()` confirma que el nivel sea estable; si ya fue
  liberado durante el arranque, se acepta igualmente el evento retenido para no perder una alerta
  legítima breve. No es posible medir después del arranque cuánto duró ese pulso ni distinguirlo de
  ruido eléctrico. Las pruebas físicas deben confirmar que el pull-up y el cableado no causen wakes
  falsos; si los causan, se necesitará mejorar el circuito o añadir filtrado hardware. Antes de
  rearmar el wake, `energia::dormir()` exige una liberación estable con el antirrebote existente.
- Antes de cada mensaje se obtiene el voltaje. Mientras `BATERIA_MODO_SIMULADO=1`, se transmiten
  `3900 mV` solo para desarrollar el flujo y el registro advierte que no es una medición física.
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
  reiniciada contra un valor antiguo. El canal y la secuencia sobreviven al deep sleep; un reinicio
  normal vuelve a iniciar la secuencia.
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
- El buzzer activo de GPIO10 se maneja directamente en nivel alto. La confirmación dura 120 ms y
  el error son tres sonidos de 500 ms separados por 250 ms. El módulo no invoca estos sonidos al
  dormir ni durante un heartbeat; `main.cpp` los llama únicamente al terminar una alerta.
- Arduino ESP32 3.3.7 habilita por defecto resistencias internas adecuadas al modo de wake dentro
  de `esp_deep_sleep_start()`. Aun así, Espressif recomienda un pull-up externo para wake en nivel
  bajo; su valor y el consumo adicional deben validarse con el circuito real.

Sin hardware no se han validado todavía el rebote real del pulsador, el despertar desde deep
sleep, el consumo de la resistencia interna, la polaridad real del buzzer ni el comportamiento
eléctrico ante una pulsación prolongada. Si el pull-up interno no mantiene un nivel estable en
deep sleep, habrá que añadir una resistencia externa sin cambiar la lógica activa en bajo.

## Batería y ADC

### Modo simulado

- `BATERIA_MODO_SIMULADO` está inicialmente en `1`. En este modo `leerMilivoltios()` devuelve
  exactamente `3900 mV`, escribe por Serial que el dato es simulado y no configura ni consulta el
  ADC. `modoSimulado()` permite que `main.cpp` registre que ese valor no es una medición física.
  El paquete no tiene un campo que indique simulación: la base recibe `3900 mV` como cualquier
  otro valor.
- Los `3900 mV` sirven únicamente para ejercitar el formato del paquete y la lógica. No demuestran
  que haya una batería conectada, no describen su carga y no deben almacenarse como telemetría
  física. Antes de probar el dispositivo real se debe cambiar `BATERIA_MODO_SIMULADO` a `0`.
- El umbral compartido es estricto: `bateria_mv < BATERIA_BAJA_MV`. Por ello `3499 mV` se considera
  bajo, `3500 mV` no, y el valor simulado de `3900 mV` no activa `FLAG_BATERIA_BAJA`. Esto valida
  lógica de desarrollo, no el estado de una celda real.

### Circuito propuesto

No conectar una Li-ion directamente a GPIO1. El pin admite la salida reducida de este divisor:

```text
OUT+ protegido de la batería --- 1 MΩ ---+--- GPIO1 / ADC1_CH1
                                         |
                                         +--- 470 kΩ --- GND / OUT-
                                         |
                                         +--- 10 nF ---- GND / OUT-
```

El capacitor de `10 nF` se conecta del nodo GPIO1 a GND, en paralelo con la resistencia de
`470 kΩ`. No se debe conectar el nodo a VBUS/USB de 5 V. Hay que confirmar en el módulo TP4056
real cuáles terminales son la salida protegida `OUT+` y `OUT-`.

- Resistencias propuestas: `R_superior = 1,000,000 Ω` y `R_inferior = 470,000 Ω`, idealmente de
  1 %. Factor de reconstrucción: `(R_superior + R_inferior) / R_inferior = 1470 / 470 ≈ 3.12766`.
- Con la Li-ion en su máximo esperado de `4.2 V`, GPIO1 recibe
  `4.2 × 470 / 1470 ≈ 1.343 V`.
- La corriente continua máxima del divisor es `4.2 V / 1.47 MΩ ≈ 2.86 µA`. Debe medirse junto
  con el consumo de la placa; LED, regulador y fugas pueden dominar ampliamente ese valor.
- La impedancia Thévenin es aproximadamente `320 kΩ`. Es alta para priorizar bajo consumo; el
  capacitor de `10 nF` actúa como reserva local de carga para el muestreo del ADC y forma una
  constante de tiempo de unos `3.2 ms`. La espera configurada de `20 ms` supera seis constantes
  de tiempo antes de descartar la primera conversión. Esto reduce el error por la alta impedancia,
  pero su efectividad todavía debe comprobarse con el circuito real.

### Lectura real preparada

Con la simulación desactivada, el módulo configura 12 bits y atenuación de 11 dB, descarta la
primera conversión y promedia 16 resultados de `analogReadMilliVolts()`. Esta API utiliza el
esquema de calibración disponible en el ESP32-C3 para convertir la lectura a milivoltios del pin;
después se aplica el factor racional `1470/470` usando aritmética de 64 bits y redondeo.

En el ESP32-C3, el rango documentado para 6 dB termina aproximadamente en `1300 mV`, de modo que
no cubre los `1343 mV` del divisor a carga completa. La atenuación de 11 dB amplía el rango hasta
aproximadamente `2500 mV`. El software adopta un límite más conservador de `1500 mV`, todavía por
encima del máximo esperado.

Una lectura del pin que supere ese límite o una conversión imposible devuelve el centinela
`BATERIA_LECTURA_INVALIDA_MV = 0`. Debido a que `PaqueteEspNow.bateria_mv` no tiene un estado de
validez separado, `main.cpp` lo transmite tal cual y lo registra por Serial; la base debe
interpretarlo como lectura no disponible o fallo ADC, no como una medición física de `0 V`. El
paquete lleva además `FLAG_BATERIA_BAJA` por seguridad. `lecturaValida()` permite distinguir el centinela antes de transmitir. Afirmar que
un dato es físico requiere simultáneamente `!modoSimulado()` y `lecturaValida(valor)`.

La calibración interna no corrige tolerancias del divisor, ruido de la placa, fugas por la alta
impedancia ni error residual del ADC.

Cuando llegue el hardware se debe comparar GPIO1 y la batería con un multímetro en varios puntos
entre carga completa y descarga, repetir lecturas con y sin radio activo, verificar el error de
las resistencias reales y medir consumo en deep sleep. Solo esa curva permitirá confirmar o
ajustar `BATERIA_BAJA_MV = 3500 mV`; hasta entonces el umbral sigue siendo una hipótesis de diseño.

## Instalación y carga

Requisitos e instalación de PlatformIO y pioarduino: README principal, sección "Cómo compilar el
firmware". Desde la raíz del repositorio:

```
copy firmware\colgante\include\secrets.example.h firmware\colgante\include\secrets.h
pio run -d firmware/colgante
pio run -d firmware/colgante -t upload
pio device monitor -d firmware/colgante
```

Mientras el colgante duerme, el puerto USB desaparece. Para cargar firmware, mantener BOOT
presionado al conectar si el puerto no aparece.

Después de cargar, el primer arranque no transmite (no viene de deep sleep): solo duerme. La
primera ALERTA sale al presionar el botón con el equipo ya dormido, y el primer HEARTBEAT a los
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
| `BATERIA_MODO_SIMULADO` | `1` (envía 3900 mV fijos) | `0`, después de armar y medir el divisor |
| `REGISTROS_SERIAL_HABILITADOS` | `1` (espera hasta 2 s el USB) | `0` en operación normal, para reducir tiempo despierto y consumo |
| `PIN_BOTON`, `PIN_BATERIA_ADC`, `PIN_BUZZER` | GPIO3, GPIO1, GPIO10 | Confirmar con el circuito armado |

Las constantes del enlace (canal, tiempos, reintentos, heartbeat, umbral de batería) están en
`firmware/comun/protocolo/protocolo.h` y se comparten con la base.

## Conexiones propuestas

Pendientes de confirmar con el circuito armado. Ningún cableado se ha probado todavía.

| Elemento | Conexión | Notas |
|---|---|---|
| Pulsador | GPIO3 ↔ GND | Activo en bajo con `INPUT_PULLUP`. GPIO0-GPIO5 son los únicos que despiertan de deep sleep; se evita GPIO2 (strapping). Puede necesitar pull-up externo. |
| Buzzer activo | `+` a GPIO10, `-` a GND | Activo en nivel alto, manejado directo desde el pin. Medir su corriente: si supera lo que el GPIO puede entregar con seguridad, se necesita un transistor. |
| Divisor de batería | `OUT+` — 1 MΩ — GPIO1 — 470 kΩ — GND, con 10 nF de GPIO1 a GND | Ver "Circuito propuesto" arriba. Nunca la batería directa a GPIO1. |
| Batería Li-ion 240 mAh | Soldada a los pads `BAT+` / `BAT-` del TP4056 | Respetar polaridad. Antes, cambiar R3 por 4.7 kΩ. |
| Alimentación del ESP32-C3 | Desde `OUT+` / `OUT-` del TP4056 | Por definir a qué pin de la Super Mini (5V o 3V3) y con qué regulación. No está decidido en el repositorio. |

## Hardware

- Botón en GPIO0-GPIO5 (únicos pines que despiertan al ESP32-C3 de deep sleep).
- Batería Li-ion 240 mAh con cargador TP4056.
- El módulo TP4056 comprado es la versión con protección (USB-C). Por confirmar con el módulo el
  voltaje de corte por descarga, para fijar `BATERIA_BAJA_MV` con margen sobre ese corte.
- La corriente de carga del módulo viene en 1 A; hay que cambiar R3 por 4.7 kΩ antes de conectar
  la batería de 240 mAh (ver nota técnica en el README principal).
- Por confirmar con el módulo: consumo real en deep sleep de la placa completa (LED de encendido,
  regulador, divisor de batería). Define la duración de la batería.

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

Que compile y pase las pruebas host no demuestra que el colgante funcione: no se ha probado radio,
alcance, ACK de una base real, deep sleep, botón, buzzer, ADC, batería ni consumo. La lista de
verificación para el hardware está en `pruebas_fisicas.md`.

## Integración con la base

El colgante depende de que el firmware de la base (`firmware/base-radar`, módulo `espnow_rx`)
cumpla lo siguiente. En esta rama `espnow_rx` todavía es un esqueleto (`TODO`).

- **MAC STA.** La base debe imprimir al arrancar la MAC de su interfaz STA, que es la que va en
  `BASE_MAC`. El colgante descarta toda trama cuya MAC de origen no sea exactamente esa, así que
  el ACK debe salir por la interfaz STA de la base.
- **Canal.** La base queda en el canal del router y el colgante lo encuentra con el barrido 1-13.
  El router debe estar en 2.4 GHz y en un canal de 1 a 13. La base debe usar
  `WiFi.setSleep(false)`. El colgante solo acepta un ACK recibido en el mismo canal en el que
  transmitió.
- **Formato del ACK.** 8 bytes según `protocolo.h`: `version = 1`, `tipo = MSG_ACK (3)`,
  `id_dispositivo` y `secuencia` iguales a los del mensaje recibido, `bateria_mv = 0`, y
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
  en los reintentos) hasta recibir un ACK válido. Si la base aplica literalmente "con
  `FLAG_ARRANQUE_FRIO`, olvidar la secuencia y aceptar como nuevo" (`docs/protocolo.md`, sección
  5), cada reintento de ese primer mensaje se procesaría como una alerta nueva. Conviene que la
  base trate como duplicado un mensaje con `FLAG_ARRANQUE_FRIO` y `FLAG_REINTENTO` cuya secuencia
  sea igual a la última aceptada. Se debe acordar con el responsable de la base.
- **Batería.** `bateria_mv = 0` significa lectura no disponible, no 0 V; llega con
  `FLAG_BATERIA_BAJA`. Mientras `BATERIA_MODO_SIMULADO = 1`, todos los mensajes llevan 3900 mV y el
  paquete no indica que sean simulados: la base y el panel no deben tratarlos como telemetría real.
- **Ausencia.** Heartbeat cada 15 min; la base marca ausente tras `COLGANTE_AUSENTE_S` (45 min).
- **Sin cifrado.** ESP-NOW sin PMK/LMK en ambos lados (ver "Transmisión ESP-NOW").

## Pendientes físicos

Según el estado registrado en el README principal, ninguna de estas tareas está hecha. No dependen
del firmware:

- Recibir la batería Li-ion 240 mAh y la pasta flux (pedidas, sin llegar).
- Cambiar R3 del TP4056 por una resistencia THT de 4.7 kΩ y confirmar la corriente con la hoja de
  datos de la celda.
- Soldar la batería a los pads `BAT+` / `BAT-` del TP4056.
- Definir cómo se alimenta la Super Mini desde `OUT+` / `OUT-`.
- Armar botón, buzzer y divisor de batería, y confirmar los pines.
- Diseñar e imprimir la carcasa y el cordón.
- Ejecutar `pruebas_fisicas.md` y ajustar con mediciones: `BATERIA_BAJA_MV`, tiempos de ACK,
  `CANAL_MAX` según el router, y necesidad de pull-up externo o transistor para el buzzer.
