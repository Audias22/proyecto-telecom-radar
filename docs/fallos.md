# Escenarios de falla

Principio general: **la alerta local de la base (buzzer y LED) no depende de la red**. Si llega una
alerta del colgante, la base suena aunque no haya WiFi, Firebase ni Telegram. Todo lo que va a la
nube es un segundo aviso, con reintentos.

La detección de apnea por radar **no se usa como alarma** (`radar.md`, sección 13: noche completa
de sueño real). La alarma confiable es el botón del colgante; el radar informa "persona detectada" /
"sin persona detectada" y datos informativos.

| # | Escenario | Cómo se detecta | Qué hace el sistema | Módulos |
|---|---|---|---|---|
| 1 | Paquete perdido (alerta o ACK) | El colgante no recibe ACK en `ACK_TIMEOUT_MS` | Reenvía la misma secuencia con `FLAG_REINTENTO` hasta `ACK_REINTENTOS` veces; luego barre canales. Si nada responde, tres beeps largos para que la persona sepa que no llegó. | colgante: `espnow_tx`, `energia` |
| 2 | Paquete duplicado | La base ve una secuencia igual o anterior a la última de ese colgante | No repite la alerta ni el aviso a la nube, pero responde ACK con `FLAG_ACK_DUPLICADO` para que el colgante deje de reintentar. | base: `espnow_rx` |
| 3 | El router cambia de canal | El colgante no recibe ACK en el canal guardado | Barre los canales 1-13 con la misma secuencia; guarda en RTC el canal que responde. Ver `docs/protocolo.md`. | colgante: `espnow_tx` |
| 4 | Caída de WiFi | `WiFi.status()` distinto de conectado | Alerta local normal. Lecturas a `cola_offline` (RAM, 720 = 1 h; llena, se descartan las más viejas; al salir llevan `diferida: true`). Eventos y avisos de Telegram en una cola aparte que no descarta y se guarda en NVS mientras haya pendientes. Reconexión con espera creciente. Al volver, primero Telegram y los eventos, después el colgante y las lecturas en orden. Una petición HTTPS en curso cuando se cae la red termina por sus límites de tiempo (escenario 5). Ver "Cola offline" abajo. | base: `red_wifi`, `cola_offline`, `almacen`, `alerta_local` |
| 5 | Caída de Firebase (o token vencido), o internet que falla con el WiFi conectado | Error HTTP o tiempo agotado (cada espera HTTPS tiene un límite corto); 401 si el token venció; el vigilante de la tarea de red si una petición pasa de 45 s | Con 401, vuelve a iniciar sesión. Otros errores: igual que caída de WiFi, a la cola. Una petición colgada se corta cerrando su socket; como último recurso, la base se reinicia (nunca con una alerta local urgente activa). Telegram no depende de Firebase (conexión, espera tras error y límite propios), así que el aviso al cuidador sale aunque Firebase falle. Ver "Bloqueo de la tarea de red" y "Telegram" abajo. | base: `firebase_cliente`, `peticion_https`, `cuerpo_http`, `vigilante_red`, `cola_offline`, `telegram` |
| 6 | Base apagada (corte de luz, cable suelto) | La base no puede avisar. El panel ve `ultimo_contacto` con más de 3 min. El colgante no recibe ACK. | Panel muestra "base desconectada". El colgante da el patrón de error, así la persona sabe que el botón no funcionó. Al volver, la base lee el `ultimo_contacto` que dejó antes de escribir el nuevo; si pasaron 3 min o más (`BASE_DESCONECTADA_MIN_MS`) crea un evento `BASE_DESCONECTADA` con la duración en `detalle` y avisa por Telegram. Lo mismo al reconectar sin reiniciar (caída del router o de internet). | base: `firebase_cliente`, `detectores`; panel |
| 7 | Colgante sin batería | Heartbeat o alerta con `FLAG_BATERIA_BAJA`; o ningún mensaje en `COLGANTE_AUSENTE_S` (45 min, 3 heartbeats) | Con batería baja: evento `BATERIA_BAJA` y Telegram, una vez por descarga: no se repite hasta que llegue un mensaje sin la bandera y con 3700 mV o más (histéresis de 200 mV). Sin mensajes: evento `COLGANTE_AUSENTE` y Telegram, una vez por episodio (termina con el siguiente mensaje); el panel lo marca ausente por `ultimo_heartbeat`. El estado se guarda en NVS: un reinicio de la base no repite el aviso. | base: `detectores`, `espnow_rx`, `telegram`; colgante: `bateria` |
| 8 | Persona no detectada en la cama | Actividad (mediana de \|`fase_total`\| en 30 s) por debajo de 0.03, o radar sin objetivo. **No** con la bandera `presencia` del radar, que sigue en 1 con la cama vacía (`radar.md`, sección 11) | Estado "sin persona detectada" en el panel, **no** "cama vacía": en una noche real el radar no vio a la persona dormida ~55 % del tiempo (`radar.md`, sección 13.3). No es una alarma. | base: `radar` |
| 9 | Radar sin datos o con datos inválidos | No llegan tramas en 5 s (`RADAR_SIN_DATOS_MS`), o valores fuera de rango (p. ej. `resp_rpm` > 60) | Valores fuera de rango no se envían (`formato_nube` los omite). Sin tramas 5 s: se reinicia la UART cada 5 s, aviso local suave y no se escriben lecturas. Sin tramas 30 s: evento `RADAR_SIN_DATOS` y Telegram, una vez por episodio; el episodio termina tras 60 s seguidos con datos (`RADAR_RECUPERADO_MS`). El panel también lo nota porque dejan de llegar lecturas. | base: `radar`, `detectores` |
| 10 | Reinicio inesperado (watchdog, brownout, fallo) | Al arrancar, `esp_reset_reason()` distinto de encendido normal, botón RST o USB | Watchdog de tareas de ESP-IDF (10 s) sobre `loop()` y sobre la tarea de red fuera de sus peticiones, para que un bloqueo termine en reinicio y no en una base colgada. Al arrancar se registra el motivo por serial y, si fue inesperado, se avisa por Telegram. Los eventos pendientes y el estado de los detectores sobreviven (NVS). Se pierden las secuencias en RAM (el siguiente mensaje de cada colgante se acepta como nuevo, a lo sumo una alerta repetida) y las lecturas de la cola offline. | base: `main`, `watchdog_tareas`, `motivo_reinicio`, `almacen` |

### Qué está cubierto (base v0.6.0, 2026-10-27)

"Hardware" es lo que ya se vio funcionar con el módulo; "código y PC" es lo implementado, compilado
y probado en `pruebas_host/`, pero todavía **por confirmar con el módulo**.

| # | Escenario | Estado |
|---|---|---|
| 1, 2, 3 | Paquete perdido, duplicado, cambio de canal | Hardware (colgante en banco y base con el router, 2026-10-12 y 2026-10-14) |
| 4 | Caída de WiFi | Hardware: alerta local sin red y reconexión. Código y PC: cola offline de 1 h, eventos en NVS, orden al volver. Por confirmar con el módulo: 1 h sin router y el vaciado de la cola al volver |
| 5 | Caída de Firebase o de internet | Hardware (v0.5.1, 2026-10-15): sin internet el error sale en 5 s y la base reintenta sin colgarse. Por confirmar con el módulo: que Telegram salga con Firebase caído |
| 6 | Base apagada | Código y PC: `BASE_DESCONECTADA` con la duración. Por confirmar con el módulo |
| 7 | Colgante sin batería o ausente | Código y PC: `BATERIA_BAJA` con histéresis y `COLGANTE_AUSENTE`. Por confirmar con el módulo (batería simulada del colgante) |
| 8 | Persona no detectada | Hardware (`radar.md`, 13) |
| 9 | Radar sin datos | Hardware: reinicio de la UART y aviso local. Código y PC: `RADAR_SIN_DATOS` por episodio. Por confirmar con el módulo (comando `r`) |
| 10 | Reinicio inesperado | Código: watchdog de tareas y aviso por Telegram. Por confirmar con el módulo (comando `w`) |

Telegram completo (envío, certificado de `api.telegram.org` con el bundle, límite de frecuencia,
reintentos) está por confirmar con el módulo. Guía de prueba de cada evento en el README de la base.

## Detalles y decisiones

### Apnea (no se usa como alarma)

- **Resultados preliminares del 2026-10-09** (2 apneas acostado, un solo tipo de detector): con la
  persona acostada a ~50 cm, el mejor detector probado (caída de la actividad) detectó la apnea de
  prueba en una de las dos validaciones cruzadas, con ~10 s de retraso, y dio una falsa alarma
  cada ~75 s de respiración normal. El `resp_rpm` del módulo siguió marcando respiración durante
  las apneas. Detalle en `radar.md`, sección 11.
- **Revisión del 2026-10-09** (`radar.md`, sección 12): la lectura del radar es correcta (sin
  tramas perdidas, decodificación verificada, firmware v1.6.12). Las pruebas se hicieron fuera del
  montaje recomendado por Seeed (1 m sobre la cabecera, 45°, solo durante el sueño, ~1 min de
  establecimiento). El detector v2 (actividad, 10 s, umbral 0.7, 20 s) detectó las 3 apneas en
  26-29 s, pero con 3.2 falsas alarmas cada 10 min; el criterio es ≤ 1. No cumple con estos datos.
  Falta una sesión de ~50 min en el montaje recomendado (sección 12.5) para decidir.
- **Noche completa del 2026-10-12** (`radar.md`, sección 13): con una persona dormida (casi toda la
  noche boca abajo) el detector v2 dio ~6 falsas alarmas por hora de sueño (5 con 25 o 30 s, que
  además dejan de detectar a tiempo las 3 apneas anteriores), y el radar no tuvo objetivo ~48 % del
  sueño. **Decisión: la apnea por radar no dispara alarmas.** "Posible pausa respiratoria" queda
  como registro experimental desactivado por defecto (sección 13.6).
- La apnea detectada por radar no dispara alarmas ni crea eventos.
- El tipo `APNEA` queda reservado en el modelo de datos, pero la base no lo crea y las reglas lo
  rechazan. Si algún día se activa el registro experimental de la sección 13.6, se agrega a las
  reglas y sus eventos serían de revisión, nunca una alarma.

Historia:

- Definición de trabajo inicial: presencia continua, distancia en rango y respiración ausente (o
  `resp_rpm` = 0) durante un tiempo umbral.
- Medido el 2026-10-06 (`radar.md`, sección 5): con el radar apuntando a una pared y sin nadie,
  el radar reporta presencia, respiración 0 y un latido congelado. Esa combinación es la misma
  que la definición de apnea de arriba, así que presencia + `resp_rpm` = 0 **no basta**. La
  detección tiene que exigir además señal de fase de una persona (en esa prueba `fase_resp` fue
  ~60 veces menor con la pared que con una persona) o que la persona se haya detectado
  respirando antes de que la respiración cayera a 0.
- Validación del 2026-10-07 (`radar.md`, secciones 9 y 10), con la persona sentada a ~84 cm:
  ninguna señal del radar siguió la frecuencia respiratoria real (8-31 rpm) y un detector por
  caída de amplitud no detectó una apnea de 20 s. Se repitió acostado (2026-10-09, sección 11) y
  el resultado se mantuvo.
- Límite conocido: una apnea, o una caída fuera del cono del radar, solo se avisa si la persona
  presiona el botón del colgante.

### Reconexión WiFi y ESP-NOW (escenario 4)

Mientras la base busca el router, su radio cambia de canal y el colgante puede no encontrarla.
Por eso los intentos de reconexión se espacian (espera creciente) en vez de buscar sin parar:
entre intentos la base queda en un canal fijo y el barrido del colgante puede encontrarla.

Implementado en `firmware/base-radar/src/red_wifi.cpp`:

- La reconexión automática del framework está desactivada (`WiFi.setAutoReconnect(false)`). En
  arduino-esp32 3.3.7 el framework igual reintenta una vez tras la primera desconexión
  (`first_connect` en `STA.cpp`); la base corta ese reintento con `esp_wifi_disconnect()`.
- Tras perder la conexión espera `WIFI_ESPERA_INICIAL_MS` (5 s) y cada intento fallido duplica
  la espera hasta `WIFI_ESPERA_MAXIMA_MS` (120 s): 5, 10, 20, 40, 80, 120, 120... s. Un intento
  que no termina en `WIFI_INTENTO_MAX_MS` (20 s) se corta. Al conectar, la cuenta vuelve a cero.
- Cada conexión, desconexión (con canal, RSSI y motivo) e intento fallido sale por serial, y
  también cada cambio de canal de la radio (`radio: canal X -> Y (wifi estado)`), leído cada
  100 ms. Durante un intento se pueden escapar saltos más cortos que eso.

Por confirmar con el módulo: en qué canal queda la radio del ESP32-C6 entre intentos de
reconexión. Se mide con la prueba "apagar el router" del README de la base, mirando las líneas
`radio: canal` en estado `esperando`. Si queda en el canal del router, el colgante la sigue
encontrando sin barrer.

### Bloqueo de la tarea de red (escenarios 4 y 5)

**Falla medida el 2026-10-14** (base v0.5.0, internet del router fallando a ratos con el WiFi
conectado): la base inició sesión en Firebase a los 7 s con el uid correcto y después, durante
más de 6 min, el comando `e` mostró `ninguna escritura OK todavia; OK 0, errores 0`, `hora del
servidor NO sincronizada`, cola de colgantes 1/8 y lecturas 24/24 con 33 descartadas. `loop()`
siguió vivo (alerta del colgante, LED, buzzer y botón BOOT). La tarea de red quedó dentro de una
petición sin devolver error.

**Causa más probable (verificada en el código y contra el servidor real).** La primera petición
después de iniciar sesión es el `PATCH` de `/dispositivos/{BASE_ID}/estado` con `?print=silent`.

1. Firebase responde `HTTP/1.1 204 No Content` **sin `Content-Length`**, sin `Transfer-Encoding`
   y con `Connection: keep-alive` (medido con curl el 2026-10-14; el 200 y el 401 sí traen
   `Content-Length`).
2. `firebase_cliente.cpp` llamaba a `http.getString()` con cualquier código > 0. En
   arduino-esp32 3.3.7, `getString()` con `_size == -1` llama a `writeToStreamDataBlock(-1)`, que
   hace `while (connected() && len == -1)` con `available()` en 0 y `delay(1)`: **no tiene ningún
   límite de tiempo**. Sale solo cuando el servidor cierra la conexión.
3. El servidor de Firebase cierra una conexión inactiva a los **75.4 s** (medido: GET con
   `print=silent` por una conexión keep-alive, cierre del servidor a los 75.4 s). Con internet
   bueno, cada escritura habría tardado ~75 s. Con el internet del router cortándose, ese cierre
   (FIN o `close_notify` TLS) se pierde: la conexión queda medio abierta, `connected()` sigue en
   verdadero y nada la termina. El keepalive TCP que activa `ssl_client` usa los valores de lwIP
   (`TCP_KEEPIDLE_DEFAULT` 2 h, 9 sondas cada 75 s): la habría detectado recién a las ~2 h 11 min.

Eso explica los números: el contador de OK sube después de `getString()`, que nunca volvió; sin
la lectura de `ultimo_contacto` no hay hora y las lecturas se acumulan y se descartan. El internet
intermitente no es la causa, pero convierte una espera de 75 s en una indefinida.

**Todas las esperas de una petición en arduino-esp32 3.3.7** (`NetworkClientSecure`,
`ssl_client.cpp`, `HTTPClient.cpp`, `NetworkManager.cpp`) y el límite que tienen ahora
(`FIREBASE_TIMEOUT_*` en `config.h`, iguales en la conexión de Auth y en la de la base de datos):

| Espera | Dónde | Antes | Ahora |
|---|---|---|---|
| DNS | `Network.hostByName()` → `lwip_getaddrinfo()`, bloquea hasta que lwIP responde | lwIP: `DNS_MAX_RETRIES` 4 con reintentos a 1, 1, 2 y 3 s, ~7 s por servidor DNS (hasta 3, `CONFIG_LWIP_DNS_MAX_SERVERS`) | igual: no se puede cambiar sin recompilar lwIP; está acotado y el vigilante lo cubre |
| `connect` TCP | `start_ssl_client()`: socket no bloqueante y `select()` con el timeout del connect | 10 s | 5 s (`FIREBASE_TIMEOUT_CONEXION_MS`) |
| Handshake TLS | `ssl_starttls_handshake()`: repite `mbedtls_ssl_handshake()` hasta `handshake_timeout` | **120 s** (constructor de `NetworkClientSecure`) | 10 s (`FIREBASE_TIMEOUT_TLS_MS`; `setHandshakeTimeout()` recibe segundos) |
| Escritura TLS | `send_ssl_data()`: corta si pasa `socket_timeout` sin avance (es el timeout del connect) | 10 s | 5 s |
| Cabeceras de la respuesta | `handleHeaderResponse()`: `_tcpTimeout` sin recibir nada | 10 s | 6 s (`FIREBASE_TIMEOUT_RESPUESTA_MS`) |
| Cuerpo sin `Content-Length` ni chunked (el 204) | `writeToStreamDataBlock(-1)`: `while (connected())` | **sin límite** | no se lee: un 204 no tiene cuerpo (`cuerpo_http`) |
| Cuerpo con `Content-Length` (base de datos) | `writeToStreamDataBlock(len)`: `readBytes()` devuelve 0 cada `_tcpTimeout` y el bucle sigue mientras la conexión parezca abierta | **sin límite** si el servidor deja de mandar sin cerrar | 6 s en total (`FIREBASE_TIMEOUT_CUERPO_MS`) |
| Cuerpo chunked (Auth responde así) | `writeToStream()`: `readStringUntil()` y un bloque por chunk | por lectura, sin total | 6 s en total |
| Reuso de la conexión keep-alive | `HTTPClient::connect()` reusa si `connected()`; una conexión medio abierta parece conectada | se descubría al esperar la respuesta (10 s) y se reintentaba una vez | se cierra antes si lleva más de 45 s sin uso (`FIREBASE_REUSO_MAX_MS`, por debajo de los 75 s del servidor); el reintento único sigue |

Peor caso de una petición con un servidor DNS: 7 + 5 + 10 + 5 + 6 + 6 = 39 s.

**Vigilante** (`src/vigilante_red.h`, desde `loop()` una vez por segundo, sin bloquear). La tarea
de red anota cada petición (método, ruta sin token, inicio) y da una vuelta cada 500 ms como
mucho fuera de ellas.

1. Si una petición pasa de `FIREBASE_PETICION_MAX_MS` (45 s, por encima del peor caso de 39 s),
   se registra `VIGILANTE: PATCH dispositivos/base01/estado lleva 45 s sin terminar; se cierra su
   socket` y `loop()` hace `shutdown()` de ese socket. lwIP lo pasa por su hilo `tcpip`, así que es
   seguro desde otra tarea, y despierta a la tarea de red con un error en el `select`, `recv` o
   `send` en que esté. El `close()` lo hace la tarea de red al recibir el error: si lo hiciera
   `loop()`, el número de socket se podría reusar mientras la tarea de red todavía lo usa. Después
   la tarea **recrea el cliente TLS** de la base de datos (pudo quedar cortado en medio de un
   registro TLS). Es la recuperación normal: no pierde nada de la cola y la petición se reintenta
   con la espera creciente de siempre.
2. Si `FIREBASE_REINICIO_TRAS_CORTE_MS` (30 s) después del corte la petición sigue sin terminar
   (bloqueada donde el socket no la despierta, como el DNS de lwIP), o si la tarea deja de dar
   vueltas fuera de una petición por más de 45 s, **se reinicia la base**. Es el último recurso.
   Antes se guardan en NVS los eventos pendientes (desde la v0.6.0); se pierden las lecturas y
   los colgantes en RAM (se cuentan por serial), pero una tarea de red colgada no los iba a enviar
   nunca, y reiniciar es lo único que devuelve la nube sin intervención. El motivo queda en
   memoria RTC; el arranque siguiente lo muestra (`Arranque: vigilante de la red: ...`) y lo
   avisa por Telegram como reinicio inesperado.
3. **Nunca se reinicia con una alerta local urgente activa** (`PATRON_BOTON` o `PATRON_APNEA`): el
   reinicio se pospone (`reinicio pospuesto hasta que termine la alerta local`) y ocurre al
   terminar o al silenciarla. Un aviso amarillo no lo pospone. El corte del socket no se pospone:
   no toca la alerta.

Se eligió cortar el socket antes que reiniciar porque recupera sin perder la cola y sin cortar la
radio (un reinicio también reinicia WiFi y ESP-NOW, unos segundos sin ACK al colgante). El
vigilante cubre las peticiones de Firebase y de Telegram (`peticion_https`). Durante una petición
la tarea de red sale del watchdog de tareas de ESP-IDF: con 10 s reiniciaría en medio de un
handshake TLS o de una espera de DNS válidos, y sin mirar la alerta local. Fuera de las peticiones
sí está en el watchdog (ver "Watchdog de tareas" abajo).

**Traza** (`FIREBASE_TRAZA 1` en `config.h`; con 0 solo salen los errores y los cortes). Una
línea por petición, sin token ni API key (tiempos de ejemplo):

```
# [    18342 ms] firebase: POST auth/signInWithPassword -> HTTP 200 (1712 ms)
# [    19011 ms] firebase: PATCH dispositivos/base01/estado -> HTTP 204 (402 ms)
# [    19190 ms] firebase: GET dispositivos/base01/estado/ultimo_contacto -> HTTP 200 (178 ms)
# [    96210 ms] firebase: PUT lecturas/base01/1791234567890 -> error -11 read Timeout (6004 ms)
# [   140022 ms] firebase: PUT lecturas/base01/1791234572890 -> error -1 sin conexion (DNS, TCP o TLS) (7031 ms)
```

El comando `e` agrega la petición en curso y cuánto lleva, la última terminada, el último error
con su texto y la cantidad de cortes del vigilante.

Por confirmar con el módulo: repetir la prueba con el internet del router cortado (README de la
base, guía de Firebase, paso 9): no debe volver a quedar `OK 0, errores 0`; deben verse errores con
su texto y, al volver internet, escrituras OK en menos de un minuto.

### Base apagada (escenario 6)

Al volver, la base avisa cuánto estuvo sin contacto (`firebase_cliente`, `detectores`):

1. Al arrancar, antes del primer `PATCH` del estado (que lo pisaría), lee
   `/dispositivos/{BASE_ID}/estado/ultimo_contacto`: es la hora del servidor del último estado que
   escribió antes de apagarse. `null` (base nueva) no es un evento.
2. Escribe el estado y lee el `ultimo_contacto` nuevo, como siempre (referencia de hora).
3. Si entre los dos pasaron `BASE_DESCONECTADA_MIN_MS` (3 min, el mismo umbral del panel) o más,
   crea `BASE_DESCONECTADA` con `detalle` "sin conexión por 12 min" y lo avisa por Telegram ("La
   base base01 volvió a estar en línea (sin conexión por 12 min)").
4. Sin reiniciar pasa lo mismo: cada estado enviado se compara con el anterior. Una caída del
   router o de internet de 3 min o más también da el evento, una vez, al volver.

Las dos horas son del servidor, así que la duración no depende del reloj de la base. Un reinicio
rápido (watchdog, vigilante) da menos de 3 min y no crea el evento; lo cubre el aviso de reinicio
inesperado. Sin Firebase configurado (solo Telegram) no hay hora del servidor y no se crea.

Sin Cloud Functions no hay un servidor que note que la base dejó de reportar y avise por Telegram
en el momento: el aviso sale recién cuando la base vuelve. Es un límite aceptado del diseño con
plan Spark:

- El panel sí lo muestra en cuanto alguien lo abre.
- El colgante avisa a la persona con el patrón de error.
- Recomendación de instalación: alimentar la base desde un cargador con batería (power bank que
  permita cargar y alimentar a la vez). Por confirmar con el módulo el consumo de la base para
  estimar la autonomía.

### Tipos de evento

| Tipo | Escenario | Lo crea | `origen` / `detalle` | Se repite |
|---|---|---|---|---|
| `BOTON` | 1, 2 | base, al recibir una alerta nueva del colgante | `colganteN` / "boton del colgante" | cada pulsación (un duplicado no) |
| `BATERIA_BAJA` | 7 | base, al recibir `FLAG_BATERIA_BAJA` | `colganteN` / "3420 mV" | una vez por descarga: hasta un mensaje sin la bandera y con ≥ 3700 mV (`BATERIA_RECUPERADA_MV`) |
| `BASE_DESCONECTADA` | 6 | base, al arrancar o reconectar, si pasaron ≥ 3 min desde su último `ultimo_contacto` | `base01` / "sin conexión por 12 min" | una vez por corte |
| `COLGANTE_AUSENTE` | 7 | base, si pasa `COLGANTE_AUSENTE_S` (45 min) sin ningún mensaje del colgante | `colganteN` / "sin mensajes por 45 min" | una vez por episodio: termina con el siguiente mensaje |
| `RADAR_SIN_DATOS` | 9 | base, tras 30 s sin tramas del radar | `base01` / "sin datos por 30 s" | una vez por episodio: termina tras 60 s seguidos con datos |

`APNEA` no aparece en la tabla: el radar no crea eventos de apnea (ver "Apnea" arriba).

Todos los eventos se guardan en `/eventos` (ver `modelo-datos.md`) y se avisan por Telegram. El
reinicio inesperado (escenario 10) se avisa solo por Telegram: las reglas publicadas no tienen ese
tipo. Ninguno se repite mientras dure la condición; se vuelven a crear cuando la condición termina
y empieza de nuevo. El estado de cada episodio (colgantes conocidos, batería avisada, ausencias,
radar sin datos) se guarda en NVS, así un reinicio de la base en medio de un episodio no lo avisa
otra vez. `timestamp` es la hora del servidor al recibir el evento (las reglas exigen `now`): un
evento que esperó en la cola lo dice en `detalle` ("enviado N s despues" o "de antes de un reinicio
de la base").

Por qué estos tiempos (`config.h`):

- **Batería, 200 mV de histéresis.** Cerca de 3.5 V el voltaje de una Li-ion sube y baja con la
  carga, la temperatura y el ruido del ADC; sin margen cada vaivén sobre el umbral sería un evento
  nuevo. 3.7 V sin la bandera solo se ve después de cargar.
- **Colgante ausente, 45 min (3 heartbeats).** Un heartbeat perdido es normal: la base buscando el
  router cambia de canal y el colgante no reintenta un heartbeat hasta el siguiente. Dos seguidos
  pasan en una caída larga del router. Con tres la ausencia es real (batería agotada, fuera de
  alcance) y se avisa en menos de una hora. Es `COLGANTE_AUSENTE_S` de `protocolo.h`.
- **Radar, 30 s y 60 s.** A los 5 s sin tramas la UART se reinicia; un corte que se arregla con
  ese reinicio no es un evento. A los 30 s ya hubo 5 reinicios sin respuesta. Un radar que va y
  viene no repite el aviso: el episodio termina recién con 60 s seguidos de datos.

### Cola offline (escenario 4)

`src/cola_offline.h`, dentro de la cola de la tarea de red (`cola_peticiones`):

- **Lecturas**: en RAM, en orden de medición, hasta `COLA_OFFLINE_LECTURAS` (720, 1 h a una cada
  5 s, ~20 kB). Llena, se descarta la más antigua (`descartadas` en el comando `e`). Al enviarse,
  la que salió más de 10 s después de medirse lleva `diferida: true`. Se pierden con un reinicio:
  su `millis()` no sirve con la referencia de hora de después (`modelo-datos.md`, sección 2).
- **Eventos**: hasta 32, para Firebase y Telegram por separado (cada uno sale de la cola cuando
  llegó a los dos). Nunca los desplaza una lectura y no se descartan; con la cola llena, un `BOTON`
  reemplaza al evento más antiguo que no sea `BOTON` (un aviso de botón no se pierde nunca por
  falta de lugar) y otro tipo se rechaza y se cuenta. Mientras haya pendientes se guardan en NVS
  (`almacen`, clave `eventos`, hasta ~2.8 kB; se borra la clave con la cola vacía). La tarea de red
  los escribe cada vez que cambian, también sin red; el vigilante los escribe antes de reiniciar.
  Al arrancar se restauran, en el mismo orden, y salen con "de antes de un reinicio de la base".
- **Orden al volver la red**: en cada vuelta de la tarea de red, primero un aviso de Telegram
  (`BOTON` primero), después Firebase: eventos en orden, el estado si toca, colgantes y lecturas
  en orden de medición. Probado en `pruebas_host/`.

Límite conocido: un evento creado durante los ~0.5 s que tarda la tarea de red en guardarlo se
pierde si la base se apaga justo en ese momento (la alerta local ya sonó).

### Telegram

`src/telegram.cpp` y `src/mensajes_telegram.cpp`. `sendMessage` por HTTPS desde la tarea de red,
con el certificado verificado con el bundle de CA de ESP-IDF (`peticion_https`, igual que
Firebase; nunca `setInsecure()`).

- **Independiente de Firebase**: conexión, espera tras error y límite de frecuencia propios. En
  cada vuelta la tarea de red atiende Telegram antes que Firebase, así que un Firebase caído o en
  su espera de 60 s no retrasa el aviso. Si una petición de Firebase está en curso al llegar un
  `BOTON`, el aviso espera a que termine (normalmente menos de 1 s; como mucho los 45 s del
  vigilante).
- **Prioridad**: el `BOTON` sale primero, espera solo 1 s desde el mensaje anterior y no cuenta
  para el tope por hora. Los demás: uno cada 3 s como mucho y 12 por hora. Lo que excede el tope
  espera en la cola, no se pierde.
- **Reintentos** sin bloquear `loop()`: con error de red o 5xx, 2 s y el doble en cada fallo
  hasta 30 s; un 429 trae `retry_after` y se respeta. Con 400/401/403/404 (token o chat inválidos,
  bot fuera del grupo) reintentar no sirve: el aviso se descarta con un error por serial y en `e`,
  para no trabar la cola; el evento en Firebase no se afecta.
- Con `TELEGRAM_TOKEN` o `TELEGRAM_CHAT_ID` de ejemplo, Telegram queda desactivado y se avisa por
  serial (`telegram: deshabilitado: ...`). Pasos para crear el bot en el README de la base.

### Watchdog de tareas y motivo del reinicio (escenario 10)

- **Watchdog de tareas de ESP-IDF** (`watchdog_tareas`), 10 s (`WDT_TAREAS_TIMEOUT_MS`), con
  panic y reinicio. Vigila `loop()` (lo alimenta el núcleo de Arduino en cada vuelta) y la tarea de
  red (se alimenta en cada vuelta). `loop()` nunca bloquea: vence solo si algo lo cuelga. La tarea
  de red sale del watchdog durante cada petición HTTPS y vuelve al terminarla; ese tramo lo cubre
  el vigilante (arriba). ESP-IDF lo arranca con 5 s; la base lo pasa a 10 s para cubrir de sobra
  una escritura en NVS o un Serial lleno sin monitor.
- **Motivo del reinicio** (`motivo_reinicio`): al arrancar se lee `esp_reset_reason()` y se
  imprime (`# Arranque: watchdog de tareas (reinicio inesperado)`). Inesperados, avisados por
  Telegram: watchdog de tareas o de interrupciones, panic, brownout, falla de alimentación, CPU
  bloqueada y el reinicio del vigilante de la red (con su motivo, guardado en RTC). Normales, solo
  por serial: encendido, botón RST, USB (carga o monitor) y JTAG. Un corte de luz es un encendido
  normal: si duró 3 min o más lo cubre `BASE_DESCONECTADA`.

