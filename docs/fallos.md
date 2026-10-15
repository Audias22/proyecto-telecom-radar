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
| 4 | Caída de WiFi | `WiFi.status()` distinto de conectado | Alerta local normal. Lecturas a `cola_offline`; eventos y avisos de Telegram pendientes en una cola aparte que no descarta. Reconexión con espera creciente. Al volver, primero eventos y Telegram, después lecturas en orden. Una petición HTTPS en curso cuando se cae la red termina por sus límites de tiempo (escenario 5). | base: `red_wifi`, `cola_offline`, `alerta_local` |
| 5 | Caída de Firebase (o token vencido), o internet que falla con el WiFi conectado | Error HTTP o tiempo agotado (cada espera HTTPS tiene un límite corto); 401 si el token venció; el vigilante de la tarea de red si una petición pasa de 45 s | Con 401, vuelve a iniciar sesión. Otros errores: igual que caída de WiFi, a la cola. Una petición colgada se corta cerrando su socket; como último recurso, la base se reinicia (nunca con una alerta local urgente activa). Telegram no depende de Firebase, así que el aviso al cuidador sale aunque Firebase falle. Ver "Bloqueo de la tarea de red" abajo. | base: `firebase_cliente`, `cuerpo_http`, `vigilante_red`, `cola_offline` |
| 6 | Base apagada (corte de luz, cable suelto) | La base no puede avisar. El panel ve `ultimo_contacto` con más de 3 min. El colgante no recibe ACK. | Panel muestra "base desconectada". El colgante da el patrón de error, así la persona sabe que el botón no funcionó. Al volver, la base compara la hora con su último `ultimo_contacto`, crea un evento `BASE_DESCONECTADA` con la duración y avisa por Telegram. | base, panel |
| 7 | Colgante sin batería | Heartbeat o alerta con `FLAG_BATERIA_BAJA`; o ningún mensaje en `COLGANTE_AUSENTE_S` | Con batería baja: evento `BATERIA_BAJA` y Telegram, una vez por descarga (no en cada heartbeat). Sin mensajes: evento `COLGANTE_AUSENTE` y Telegram; el panel lo marca ausente por `ultimo_heartbeat`. | base: `espnow_rx`, `telegram`; colgante: `bateria` |
| 8 | Persona no detectada en la cama | Actividad (mediana de \|`fase_total`\| en 30 s) por debajo de 0.03, o radar sin objetivo. **No** con la bandera `presencia` del radar, que sigue en 1 con la cama vacía (`radar.md`, sección 11) | Estado "sin persona detectada" en el panel, **no** "cama vacía": en una noche real el radar no vio a la persona dormida ~55 % del tiempo (`radar.md`, sección 13.3). No es una alarma. | base: `radar` |
| 9 | Radar sin datos o con datos inválidos | No llegan tramas en N segundos, o valores fuera de rango (p. ej. `resp_rpm` > 60) | La lectura se marca `valida = false` y no se envía. Si dura más de N segundos, se reinicia la UART del radar, aviso local suave, evento `RADAR_SIN_DATOS` y Telegram. El panel también lo nota porque dejan de llegar lecturas. | base: `radar` |
| 10 | Reinicio inesperado (watchdog, brownout, fallo) | Al arrancar, `esp_reset_reason()` distinto de encendido normal | Watchdog activo sobre `loop()` para que un bloqueo termine en reinicio y no en una base colgada. Al arrancar se reporta el motivo por Telegram. Se pierden las secuencias en RAM (el siguiente mensaje de cada colgante se acepta como nuevo, a lo sumo una alerta repetida) y la cola offline. | base: `main` |

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
   vueltas fuera de una petición por más de 45 s, **se reinicia la base**. Es el último recurso:
   se pierde la cola en RAM (eventos, colgantes y lecturas pendientes; se cuentan por serial antes
   de reiniciar), pero una tarea de red colgada no la iba a enviar nunca, y reiniciar es lo único
   que devuelve la nube sin intervención. El motivo queda en memoria RTC y el arranque siguiente
   lo muestra (`el arranque anterior termino en un reinicio del vigilante: ...`).
3. **Nunca se reinicia con una alerta local urgente activa** (`PATRON_BOTON` o `PATRON_APNEA`): el
   reinicio se pospone (`reinicio pospuesto hasta que termine la alerta local`) y ocurre al
   terminar o al silenciarla. Un aviso amarillo no lo pospone. El corte del socket no se pospone:
   no toca la alerta.

Se eligió cortar el socket antes que reiniciar porque recupera sin perder la cola y sin cortar la
radio (un reinicio también reinicia WiFi y ESP-NOW, unos segundos sin ACK al colgante). No se usa
el watchdog de tareas de ESP-IDF sobre la tarea de red: reiniciaría sin mirar la alerta local.

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

Sin Cloud Functions no hay un servidor que note que la base dejó de reportar y avise por Telegram
en el momento. Es un límite aceptado del diseño con plan Spark:

- El panel sí lo muestra en cuanto alguien lo abre.
- El colgante avisa a la persona con el patrón de error.
- Recomendación de instalación: alimentar la base desde un cargador con batería (power bank que
  permita cargar y alimentar a la vez). Por confirmar con el módulo el consumo de la base para
  estimar la autonomía.

### Tipos de evento

| Tipo | Escenario | Lo crea |
|---|---|---|
| `BOTON` | 1, 2 | base, al recibir una alerta nueva del colgante |
| `BATERIA_BAJA` | 7 | base, al recibir `FLAG_BATERIA_BAJA` (una vez por descarga) |
| `BASE_DESCONECTADA` | 6 | base, al volver, con la duración en `detalle` |
| `COLGANTE_AUSENTE` | 7 | base, si pasa `COLGANTE_AUSENTE_S` sin mensajes del colgante |
| `RADAR_SIN_DATOS` | 9 | base, si pasan N segundos sin lecturas válidas del radar |

`APNEA` no aparece en la tabla: el radar no crea eventos de apnea (ver "Apnea" arriba).

Todos los eventos se guardan en `/eventos` (ver `modelo-datos.md`) y se avisan por Telegram.
`COLGANTE_AUSENTE` y `RADAR_SIN_DATOS` se crean una sola vez por episodio: no se repiten mientras
la condición siga, y se pueden volver a crear cuando el colgante o el radar se recuperen y fallen
de nuevo.
