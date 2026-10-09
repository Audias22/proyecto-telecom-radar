# firmware/colgante

Firmware del ESP32-C3 Super Mini del colgante.

Ciclo: deep sleep -> despierta por botón o por timer -> envía ALERTA o HEARTBEAT por ESP-NOW ->
espera ACK (reintentos y barrido de canales) -> beep -> deep sleep. Detalle en `docs/protocolo.md`.

## Módulos (`src/`)

| Módulo | Responsabilidad |
|---|---|
| `boton` | Botón de auxilio, antirrebote y causa del despertar (implementado) |
| `espnow_tx` | Envío, espera de ACK, reintentos, barrido, canal y secuencia en RTC (implementado) |
| `energia` | Deep sleep, fuentes de despertar, beeps de confirmación y error (implementado) |
| `bateria` | Voltaje simulado o medido por ADC (implementado; simulación activa) |

`main.cpp` por ahora solo imprime nombre y versión; aún no integra el ciclo completo.

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
- El estado se conserva en RTC y `recuperandoBotonAtascado()` permite que el futuro flujo principal
  omita radio, heartbeat y sonidos durante ese wake de recuperación. El indicador se acepta solo
  cuando la causa de wake es el timer; un reinicio por otra causa lo limpia como obsoleto.
- Cuando se detecta la liberación, el wake activo en bajo se restaura automáticamente; no queda
  deshabilitado permanentemente. Durante los 5 segundos de recuperación no se puede distinguir una
  liberación seguida de una nueva pulsación: esa es la ventana máxima temporal sin detección. El
  valor deberá ajustarse con mediciones de consumo y pruebas de uso reales.
- Cualquier error al limpiar o habilitar fuentes se informa por Serial, aplica una pausa de 1 s y
  evita entrar en deep sleep con una configuración incompleta. El futuro flujo principal deberá
  decidir si reintenta o adopta otro estado seguro cuando `dormir()` regrese.
- El buzzer activo de GPIO10 se maneja directamente en nivel alto. La confirmación dura 120 ms y
  el error son tres sonidos de 500 ms separados por 250 ms. El módulo no invoca estos sonidos al
  dormir ni durante un heartbeat; deberán llamarse únicamente desde el futuro flujo de alerta.
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
  ADC. `modoSimulado()` permite que la futura integración evite presentar ese valor como una
  medición física.
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
validez separado, si el futuro flujo transmite ese valor la base debe interpretarlo como lectura
no disponible o fallo ADC, no como una medición física de `0 V`; además se marcará batería baja
por seguridad. `lecturaValida()` permite distinguir el centinela antes de transmitir. Afirmar que
un dato es físico requiere simultáneamente `!modoSimulado()` y `lecturaValida(valor)`.

La calibración interna no corrige tolerancias del divisor, ruido de la placa, fugas por la alta
impedancia ni error residual del ADC.

Cuando llegue el hardware se debe comparar GPIO1 y la batería con un multímetro en varios puntos
entre carga completa y descarga, repetir lecturas con y sin radio activo, verificar el error de
las resistencias reales y medir consumo en deep sleep. Solo esa curva permitirá confirmar o
ajustar `BATERIA_BAJA_MV = 3500 mV`; hasta entonces el umbral sigue siendo una hipótesis de diseño.

## Configuración

- `include/config.h`: versión y pines (propuestos, por confirmar).
- `include/secrets.h`: MAC de la base e id del colgante. Copiar de `include/secrets.example.h`.

## Hardware

- Botón en GPIO0-GPIO5 (únicos pines que despiertan al ESP32-C3 de deep sleep).
- Batería Li-ion 240 mAh con cargador TP4056.
- El módulo TP4056 comprado es la versión con protección (USB-C). Por confirmar con el módulo el
  voltaje de corte por descarga, para fijar `BATERIA_BAJA_MV` con margen sobre ese corte.
- La corriente de carga del módulo viene en 1 A; hay que cambiar R3 por 4.7 kΩ antes de conectar
  la batería de 240 mAh (ver nota técnica en el README principal).
- Por confirmar con el módulo: consumo real en deep sleep de la placa completa (LED de encendido,
  regulador, divisor de batería). Define la duración de la batería.
- Mientras el colgante duerme, el puerto USB desaparece. Para cargar firmware, mantener BOOT
  presionado al conectar si el puerto no aparece.
