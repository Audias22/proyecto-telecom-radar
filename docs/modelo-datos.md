# Modelo de datos (Firebase Realtime Database)

Reglas en `web/database.rules.json`. La base escribe por la API REST
(`https://<proyecto>.firebaseio.com/<ruta>.json?auth=<idToken>`); el panel usa el SDK web.

## 1. Estructura

```
/dispositivos/{baseId}/estado
    online            bool     la base lo pone en true en cada actualización
    ultimo_contacto   número   hora del servidor (ms) de la última actualización
    rssi_wifi         número   dBm de la conexión WiFi de la base
    version_firmware  texto    p. ej. "0.1.0"

/colgantes/{id}
    bateria_mv        número   último voltaje reportado (mV)
    ultimo_heartbeat  número   hora del servidor (ms) del último mensaje recibido del colgante
    rssi              número   dBm con que la base recibió el último mensaje
    base              texto    base que lo recibió (opcional)

/lecturas/{baseId}/{ts}                      una cada 5 s (sección 1.1)
    persona           bool     true = "persona detectada": actividad de 30 s >= 0.03
    objetivo          bool     el radar tuvo objetivo (tramas de fase con dato) en los últimos 30 s
    actividad         número   mediana de |fase_total| en 30 s (opcional: solo con objetivo)
    distancia_cm      número   distancia que da el radar (opcional: solo con objetivo)
    resp_rpm          número   estimación del módulo, NO validada (opcional: solo si es > 0)
    latido_bpm        número   estimación del módulo, NO validada (opcional: solo si es > 0)
    diferida          bool     true si se envió desde la cola offline (opcional)

/eventos/{pushId}
    tipo              texto    APNEA | BOTON | BATERIA_BAJA | BASE_DESCONECTADA |
                               COLGANTE_AUSENTE | RADAR_SIN_DATOS
    origen            texto    "base01", "colgante1", ...
    timestamp         número   hora del servidor (ms)
    atendido          bool     false al crearse; el panel lo pasa a true
    detalle           texto    opcional, p. ej. "sin conexión por 12 min"
    atendido_por      texto    uid de quien lo atendió (lo escribe el panel)
    atendido_en       número   hora del servidor (ms) en que se atendió (lo escribe el panel)

/cuentas_dispositivo/{uid}  = "base01"   relaciona la cuenta de Auth de cada base con su baseId
/usuarios_panel/{uid}       = true       cuidadores con acceso al panel
```

`APNEA` es un tipo reservado: el nombre no se usa para otra cosa, pero la base no lo crea y las
reglas lo rechazan. La apnea por radar no se usa como alarma ni genera eventos (`radar.md`,
sección 13; `fallos.md`, "Tipos de evento"). Si algún día se activa el registro experimental de
"posible pausa respiratoria" (`radar.md`, 13.6), se agrega a las reglas en ese momento y sus
eventos serían de revisión, nunca una alarma.

`origen` es el `baseId` de la cuenta que crea el evento o `colganteN` (`colgante1`, ...).

### 1.1 Qué guarda cada lectura y por qué

Las mediciones de `radar.md` (secciones 9 a 13) dejan claro qué datos del radar sirven y cuáles
no, y la lectura se arma con eso:

- **`resp_rpm` y `latido_bpm` no son obligatorios.** Durante una noche real el módulo dio
  `resp_rpm` = 0 en el 57 % de las ventanas con objetivo (13.5) y no siguió el pacer en 8-30 rpm
  (9 y 11); siguió marcando respiración durante las apneas (11). Son una "estimación del módulo,
  no validada": se envían solo cuando el módulo da un valor mayor que 0, y si no vienen el panel
  muestra "sin dato", nunca "0 rpm" (13.6, punto 4). Que no estén cuando valen 0 también ahorra
  espacio.
- **`persona` es el dato principal**, con el criterio de `fallos.md`, escenario 8, y `radar.md`,
  13.6: actividad (mediana de |`fase_total`| sin las tramas en 0) en una ventana deslizante de
  30 s, umbral 0.03. Se evalúa en cada lectura (cada 5 s) sobre los últimos 30 s. `true` es
  "persona detectada"; `false` es "sin persona detectada", que **no** es "cama vacía" ni dispara
  nada (con la persona dormida el radar no la vio ~55 % del tiempo, 13.3).
- **`objetivo`** separa las dos causas de "sin persona detectada": el radar sin objetivo (43 % del
  sueño en 13.3) o con objetivo y actividad baja (11 %). Sirve para diagnosticar el montaje. Las
  reglas exigen `objetivo` = `true` cuando `persona` es `true` (sin fase no hay actividad).
- **`actividad`** se guarda (opcional, solo con objetivo) porque es el número del que sale
  `persona`: permite revisar el umbral con datos reales sin reflashear la base. Se redondea a
  3 decimales.
- **`distancia_cm`** solo con objetivo: es el dato que mostró el movimiento del sensor (13.1).
- **No se guarda la bandera `presencia` del módulo**: sigue en 1 con la cama vacía o apuntando a
  una pared (`radar.md`, 5 y 11), así que confunde más de lo que informa. `objetivo` la reemplaza.
- No se guardan las fases crudas (`fase_total`, `fase_resp`, `fase_latido`): llegan a ~16 por
  segundo y no caben en el plan Spark; para análisis se capturan por serial (`radar.md`, 6).

Si el radar no manda datos (escenario 9 de `fallos.md`), la base no escribe lecturas: el hueco en
`/lecturas` y el evento `RADAR_SIN_DATOS` son la señal.

Ejemplos:

```
"1791234567890":{"persona":true,"objetivo":true,"actividad":0.048,"distancia_cm":85,"resp_rpm":13,"latido_bpm":72}
"1791234567895":{"persona":true,"objetivo":true,"actividad":0.051,"distancia_cm":84}
"1791234567900":{"persona":false,"objetivo":false}
```

`/cuentas_dispositivo` y `/usuarios_panel` se llenan a mano desde la consola de Firebase y nadie
los puede leer ni escribir desde fuera.

## 2. Hora: servidor, no reloj del ESP32

El ESP32 no tiene un reloj confiable (arranca en 0 y depende de NTP). Todas las horas se toman del
servidor de Firebase:

- `estado.ultimo_contacto`, `colgantes.ultimo_heartbeat`, `eventos.timestamp` y `atendido_en` se
  escriben con el valor especial `{".sv": "timestamp"}`. El servidor lo reemplaza por su hora en
  milisegundos al guardar.
- `/lecturas/{baseId}/{ts}` usa la hora como clave. Una clave no puede ser `{".sv":"timestamp"}`,
  así que la base calcula `ts` a partir de la hora del servidor:
  1. Cada vez que actualiza `estado` (cada 60 s) lee de vuelta `ultimo_contacto` y guarda el par
     (hora del servidor, `millis()` en ese momento).
  2. Para cada lectura: `ts = hora_servidor_ref + (millis_lectura - millis_ref)`.
  3. El error es del orden de la latencia de una petición HTTPS, despreciable con una lectura cada
     5 s.
- Para lecturas guardadas en la cola sin red, la base guarda el `millis()` de cada medición y
  calcula `ts` con la misma fórmula al reenviarlas, aunque la referencia llegue después (la resta
  da negativo y funciona igual). Se marcan con `diferida: true`.
- Si la base se reinicia, las lecturas en cola se pierden (la cola está en RAM). Así nunca se
  mezcla un `millis()` de antes del reinicio con una referencia de después. Los eventos pendientes
  sí sobreviven (se guardan en NVS, `fallos.md`, "Cola offline"); su `timestamp` es la hora del
  servidor al recibirlos y `detalle` dice si esperaron.

`ts` se escribe con 13 dígitos (milisegundos desde 1970). Con longitud fija el orden alfabético de
las claves coincide con el orden de tiempo, así que el panel puede usar `orderByKey()`.

Escribir con `PUT /lecturas/{baseId}/{ts}` hace que reenviar una lectura sea idempotente: si la
base no recibió la respuesta y la reenvía, sobrescribe el mismo nodo en vez de duplicarlo.

Los eventos sí usan `POST` (push id generado por el servidor). Si la base reenvía un evento porque
no recibió respuesta, puede quedar duplicado; para eventos se prefiere un duplicado a una pérdida.

## 3. Reglas de acceso

| Ruta | Lee | Escribe |
|---|---|---|
| `/dispositivos/{baseId}/estado` | panel; la cuenta de esa base | solo la cuenta de esa base |
| `/colgantes/{id}` | panel | cualquier cuenta de base (`base` debe ser su propio `baseId`) |
| `/lecturas/{baseId}` | panel; la cuenta de esa base | solo la cuenta de esa base (también borrar) |
| `/eventos/{id}` (crear) | panel | cuentas de base, solo si el evento no existe y con `atendido` = `false` |
| `/eventos/{id}/atendido`, `atendido_por`, `atendido_en` | | solo el panel, solo sobre eventos existentes; `atendido` solo puede pasar a `true` y los otros dos se escriben una vez |
| `/cuentas_dispositivo`, `/usuarios_panel` | nadie | nadie (solo consola) |

"Panel" significa una cuenta autenticada que está en `/usuarios_panel`. "Base" significa una cuenta
autenticada que está en `/cuentas_dispositivo`. Sin sesión no se lee ni se escribe nada, y una
cuenta autenticada que no está en ninguna de las dos listas tampoco.

La base solo lee lo que necesita: su propio `estado` (para la referencia de hora y para calcular
cuánto estuvo apagada, escenario 6 de `fallos.md`) y sus propias lecturas (para el borrado de
lecturas viejas, sección 4). No lee eventos, colgantes ni datos de otras bases.

El cuidador solo lee y marca eventos como atendidos: no puede escribir estado, colgantes ni
lecturas, ni crear o borrar eventos.

Las reglas también validan:

- Tipos y rangos: `actividad` 0-10, `resp_rpm` mayor que 0 y hasta 60, `latido_bpm` mayor que 0 y
  hasta 250, `distancia_cm` 0-1000, `bateria_mv` 0-5000 (0 = sin dato, `protocolo.md`), `rssi`
  -127 a 0, `tipo` dentro de la lista (sin `APNEA`), clave de lectura de 13 dígitos.
- Las horas (`ultimo_contacto`, `ultimo_heartbeat`, `timestamp`, `atendido_en`) deben ser
  exactamente la hora del servidor (`=== now`), es decir, escritas con `{".sv":"timestamp"}`. Una
  base no puede inventar una hora.
- `origen` de un evento es el `baseId` de quien lo crea o `colganteN`: una base no puede crear
  eventos a nombre de otra.
- Se rechazan campos que no estén en este documento.

`estado.online` no es confiable por sí solo: una base apagada no puede escribir `false`. El panel
considera desconectada la base si `ultimo_contacto` tiene más de 3 minutos (3 periodos de
`estado`), sin importar el valor de `online`.

Las reglas se prueban contra el proyecto real con `web/simulador/simulador.py verificar` (ver
`web/README.md`).

## 4. Volumen y límites del plan Spark

Límites del plan Spark para Realtime Database que importan aquí (verificar en
https://firebase.google.com/pricing antes de la entrega): 1 GB almacenado, 10 GB de descarga al
mes, 100 conexiones simultáneas. Todo lo de este documento cabe en Spark: no se usan Cloud
Functions ni nada que requiera el plan Blaze.

### Almacenamiento

Tamaño en JSON de los ejemplos de la sección 1.1:

| Lectura | Bytes |
|---|---|
| Con objetivo y valores del módulo | 114 |
| Con objetivo, módulo en 0 (el caso más común dormido, 13.5) | 84 |
| Sin objetivo | 50 |

Firebase no publica el costo interno exacto por nodo, así que se estima de 100 a 200 bytes por
lectura (el peor caso del JSON, redondeado hacia arriba, y el doble como margen).

| Concepto | Valor |
|---|---|
| Lecturas por día (una cada 5 s) | 86 400 / 5 = 17 280 |
| Lecturas por mes (30 días) | 518 400 |
| Tamaño mensual a 100 B por lectura | ~52 MB |
| Tamaño mensual a 200 B por lectura | ~104 MB |
| Meses hasta llenar 1 GB | ~10 a ~19 |

Estado, colgantes y eventos son despreciables al lado de las lecturas (pocos nodos que se
sobrescriben, y eventos que deberían ser raros). El simulador escribe al mismo ritmo que la base:
una hora de simulador son 720 lecturas (~70-140 kB).

Conclusión: a unos 100 MB por mes, el 1 GB no se llena durante el proyecto, así que no se
implementa borrado de lecturas viejas. El tamaño real se revisa en la pestaña Uso de la consola
después de una semana de pruebas y se anota en `pruebas/`.

Mejora futura, para uso continuo más allá del proyecto: que la base borre una vez al día las
lecturas de más de 30 días (consulta `orderBy="$key"` con `endAt` y `shallow`, y escribe `null`;
las reglas permiten que la cuenta de la base lea y borre en su propia ruta de `/lecturas`). Con
eso el total se mantendría alrededor de 50-100 MB.

### Descarga

- 24 horas son 17 280 lecturas; a 84-114 bytes de JSON cada una, de 1.5 a 2 MB por carga
  completa del panel. Con 10 GB al mes da para miles de cargas, pero el panel debe consultar con
  `limitToLast()` o un rango de claves y nunca descargar todo `/lecturas`.
- Un panel abierto todo el día escuchando lecturas nuevas recibe ~2 MB por día (~60 MB al mes),
  más la sobrecarga del protocolo y de TLS, que Firebase también cuenta como descarga.
- Por defecto, la API REST devuelve en la respuesta los datos escritos, y eso cuenta como descarga.
  La base (y el simulador) escriben con `?print=silent` para que la respuesta venga vacía.
- La base y el simulador leen una sola hora por minuto (`ultimo_contacto`, ~13 bytes).

### Peticiones de la base

Una lectura cada 5 s implica una petición HTTPS cada 5 s. Abrir una conexión TLS nueva cada vez es
lento y consume memoria en el ESP32-C6; la base debe mantener la conexión abierta (keep-alive). Si
aun así no alcanza, la alternativa es agrupar varias lecturas en un solo `PATCH` cada 30 s, con el
mismo formato de claves.
