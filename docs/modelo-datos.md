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

/lecturas/{baseId}/{ts}
    resp_rpm          número   respiraciones por minuto
    latido_bpm        número   latidos por minuto
    presencia         bool
    distancia_cm      número
    diferida          bool     true si se envió desde la cola offline (opcional)

/eventos/{pushId}
    tipo              texto    APNEA | BOTON | BATERIA_BAJA | BASE_DESCONECTADA
    origen            texto    "base01", "colgante1", ...
    timestamp         número   hora del servidor (ms)
    atendido          bool     false al crearse; el panel lo pasa a true
    detalle           texto    opcional, p. ej. "sin respiración por 25 s"
    atendido_por      texto    uid de quien lo atendió (lo escribe el panel)
    atendido_en       número   hora del servidor (ms) en que se atendió (lo escribe el panel)

/cuentas_dispositivo/{uid}  = "base01"   relaciona la cuenta de Auth de cada base con su baseId
/usuarios_panel/{uid}       = true       cuidadores con acceso al panel
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
  mezcla un `millis()` de antes del reinicio con una referencia de después.

`ts` se escribe con 13 dígitos (milisegundos desde 1970). Con longitud fija el orden alfabético de
las claves coincide con el orden de tiempo, así que el panel puede usar `orderByKey()`.

Escribir con `PUT /lecturas/{baseId}/{ts}` hace que reenviar una lectura sea idempotente: si la
base no recibió la respuesta y la reenvía, sobrescribe el mismo nodo en vez de duplicarlo.

Los eventos sí usan `POST` (push id generado por el servidor). Si la base reenvía un evento porque
no recibió respuesta, puede quedar duplicado; para eventos se prefiere un duplicado a una pérdida.

## 3. Reglas de acceso

| Ruta | Lee | Escribe |
|---|---|---|
| `/dispositivos/{baseId}/estado` | panel y bases | solo la cuenta de esa base |
| `/colgantes/{id}` | panel y bases | cualquier cuenta de base |
| `/lecturas/{baseId}` | panel y bases | solo la cuenta de esa base |
| `/eventos/{id}` (crear) | panel y bases | cuentas de base, solo si el evento no existe |
| `/eventos/{id}/atendido`, `atendido_por`, `atendido_en` | | solo el panel, solo sobre eventos existentes, y `atendido` solo puede pasar a `true` |
| `/cuentas_dispositivo`, `/usuarios_panel` | nadie | nadie (solo consola) |

"Panel" significa una cuenta autenticada que está en `/usuarios_panel`. "Base" significa una cuenta
autenticada que está en `/cuentas_dispositivo`. Una cuenta autenticada que no está en ninguna de
las dos listas no puede leer nada.

Las reglas también validan tipos y rangos (`resp_rpm` 0-60, `latido_bpm` 0-250, `distancia_cm`
0-1000, `rssi` -127 a 0, `tipo` dentro de la lista) y rechazan campos que no estén en este
documento.

`estado.online` no es confiable por sí solo: una base apagada no puede escribir `false`. El panel
considera desconectada la base si `ultimo_contacto` tiene más de 3 minutos (3 periodos de
`estado`), sin importar el valor de `online`.

## 4. Volumen y límites del plan Spark

Límites del plan Spark para Realtime Database que importan aquí (verificar en
https://firebase.google.com/pricing antes de la entrega): 1 GB almacenado, 10 GB de descarga al
mes, 100 conexiones simultáneas.

### Almacenamiento

Una lectura en JSON:

```
"1791234567890":{"resp_rpm":16.5,"latido_bpm":72,"presencia":true,"distancia_cm":85}
```

Son unos 84 bytes de texto. Firebase no publica el costo interno exacto por nodo, así que se
estima un rango de 100 a 200 bytes por lectura.

| Concepto | Valor |
|---|---|
| Lecturas por día (una cada 5 s) | 86 400 / 5 = 17 280 |
| Lecturas por mes (30 días) | 518 400 |
| Tamaño mensual a 100 B por lectura | ~52 MB |
| Tamaño mensual a 200 B por lectura | ~104 MB |
| Meses hasta llenar 1 GB | ~10 a ~19 |

Estado, colgantes y eventos son despreciables al lado de las lecturas (pocos nodos que se
sobrescriben, y eventos que deberían ser raros).

Conclusión: el proyecto cabe con margen durante el semestre. Para uso continuo, la base borra una
vez al día las lecturas de más de 30 días (consulta `orderByKey` con `endAt` y escribe `null`).
Con eso el total se mantiene alrededor de 50-100 MB. El tamaño real se revisa en la pestaña Uso de
la consola después de una semana de pruebas y se anota en `pruebas/`.

### Descarga

- 24 horas son 17 280 lecturas; a unos 84 bytes de JSON cada una, cerca de 1.5 MB por carga
  completa del panel.
  Con 10 GB al mes da para miles de cargas, pero el panel debe consultar con `limitToLast()` o un
  rango de claves y nunca descargar todo `/lecturas`.
- Por defecto, la API REST devuelve en la respuesta los datos escritos, y eso cuenta como descarga.
  La base debe escribir con `?print=silent` para que la respuesta venga vacía.

### Peticiones de la base

Una lectura cada 5 s implica una petición HTTPS cada 5 s. Abrir una conexión TLS nueva cada vez es
lento y consume memoria en el ESP32-C6; la base debe mantener la conexión abierta (keep-alive). Si
aun así no alcanza, la alternativa es agrupar varias lecturas en un solo `PATCH` cada 30 s, con el
mismo formato de claves.
