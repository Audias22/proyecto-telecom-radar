# Pruebas host de la base

Pruebas unitarias de la lógica pura de la base que se compilan y ejecutan en un PC, sin ESP32,
sin radar, sin Arduino y sin red.

`test_logica.cpp` compila los `.cpp` reales y no duplica su lógica:

- `src/ventana_actividad.cpp`: criterio de "persona detectada" (mediana de |`fase_total`| sin
  ceros en 30 s, umbral 0.03) y "objetivo".
- `src/formato_nube.cpp`: JSON de lecturas, estado, colgante, evento e inicio de sesión; clave
  `ts` de 13 dígitos; hora del servidor desde `millis()`; lectura de las respuestas de Firebase.
- `src/cola_peticiones.cpp`: cola acotada de `loop()` a la tarea de red.
- `src/cola_offline.cpp`: lecturas sin red (1 h, se descartan las más viejas) y eventos para
  Firebase y Telegram que no se descartan y se guardan en NVS.
- `src/detectores.cpp`: `BATERIA_BAJA` con histéresis, `COLGANTE_AUSENTE` y `RADAR_SIN_DATOS` una
  vez por episodio, estado guardado tras un reinicio y duración de `BASE_DESCONECTADA`.
- `src/mensajes_telegram.cpp`: texto de cada aviso y límite de frecuencia de Telegram.
- `src/cuerpo_http.cpp`: cuerpo de las respuestas HTTP con memoria fija (en lugar de
  `HTTPClient::getString()`, que no tiene límite de tiempo).
- `src/vigilante_red.cpp`: cuándo cortar una petición colgada y cuándo reiniciar la base.
- `include/config.h`: capacidades, ventana, umbral, tiempos del vigilante, de los eventos y de
  Telegram. `firmware/comun/protocolo/protocolo.h`: `BATERIA_BAJA_MV` y `COLGANTE_AUSENTE_S`.

Los JSON esperados son los mismos que escribe `web/simulador/simulador.py`, que pasó la
verificación contra las reglas publicadas. No usa `secrets.h`. Cada caso imprime `[ OK  ]` o
`[FALLO]`, y el programa termina con código `0` solo si todos pasan.

Estado actual (2026-10-27): **63 casos y 1674 verificaciones** (la mayoría, del caso "tarea
sana" del vigilante, que revisa cada 500 ms durante 7.5 min simulados).

| Área | Qué se verifica |
|---|---|
| Ventana | Vacía y solo ceros: sin objetivo; ignora 0 y NaN y usa el valor absoluto; mediana impar y par (promedio de los dos del medio, como `numpy.median`); umbral 0.03 incluido; ventana (t-30 s, t]; la persona se va y a los 30 s no hay objetivo; con más de 640 tramas se pisan las más antiguas; tasa real de 16.2 tramas/s con 1.5 % de ceros; vuelta de `millis()` |
| Lectura | Ejemplo completo de `docs/modelo-datos.md`; sin objetivo solo `persona: false, objetivo: false`; módulo en 0 se omite; `actividad` recortada a 10; `resp_rpm` que redondea a 0, > 60 o `latido_bpm` > 250 se omiten; `distancia_cm` > 1000 se omite; `diferida`; buffer chico |
| Clave y hora | 13 dígitos exactos; 12 o 14 dígitos y 0 (sin referencia) se rechazan; `ts` antes y después de la referencia y con vuelta de `millis()` |
| Estado, colgante, evento | JSON exactos con `{".sv":"timestamp"}`; RSSI recortado a -127..0; batería fuera de 0-5000 como 0; evento con y sin `detalle`; escape de comillas y control |
| Auth | Cuerpo de `signInWithPassword` con escape; `idToken`, `refreshToken`, `localId`, `expiresIn` de una respuesta con espacios; errores de Auth y de la base de datos; claves ausentes, no texto o que no caben |
| Cuerpo HTTP | 204, 304 y 1xx sin cuerpo aunque falte `Content-Length` (el 204 de `?print=silent` llega así); `Content-Length` sin consumir de más; chunked entero, de a un byte, con extensión, con trailer y con solo LF; chunked mal formado (no hexadecimal, falta el CRLF, tamaño absurdo, línea vacía); cuerpo que no cabe se consume y se marca; hasta el cierre del servidor |
| Vigilante | Tarea sana con peticiones cortas: nunca actúa; corta a los 45 s, una sola vez, y reinicia 30 s después; el corte recupera la tarea y una petición nueva se vigila desde cero; una alerta local urgente pospone el reinicio pero no el corte; tarea sin vueltas fuera de una petición: reinicio sin corte; vuelta de `millis()`; método y ruta largos se recortan |
| Cola | Orden eventos, colgantes, lecturas; sin hora del servidor no salen lecturas; un envío fallido no saca la petición; lecturas llenas descartan la más antigua; las lecturas nunca desplazan eventos y un evento sin lugar se rechaza y se cuenta; confirmar tras una lectura pisada no saca la nueva; un colgante reemplaza al pendiente del mismo id; más colgantes que lugares |
| Cola offline | Al reconectar, eventos en orden, colgante y lecturas en orden de medición, y Telegram por su cuenta con el BOTON primero; 1 h de lecturas y 50 más descartan las 50 más viejas; destinos Firebase y Telegram por separado (REINICIO solo a Telegram); con la cola llena un BOTON reemplaza al evento más antiguo que no es BOTON y otro tipo se rechaza; guardar y restaurar tras un reinicio (orden, `previo_reinicio`, solo destinos activos, datos cortados u otro formato se ignoran); cola vacía |
| Eventos | Batería: evento con la primera bandera, vaivén sobre el umbral y "sin dato" no rearman, 3700 mV rearma; episodio con espera de inicio y de fin, una sola vez, el radar que va y viene no repite, vuelta de `millis()`; colgante ausente una vez, vuelve con el siguiente mensaje, episodio nuevo; batería por colgante; más colgantes que lugares; estado tras un reinicio (misma descarga y misma ausencia no se repiten, ausencia contada desde el arranque); `BASE_DESCONECTADA` desde 3 min, no con el estado normal de 60 s ni sin anterior; duraciones "45 s", "12 min", "3 h 5 min", "2 d 4 h" |
| Telegram | Texto de cada aviso; atraso de más de 1 min y aviso de antes de un reinicio; texto recortado; cuerpo de `sendMessage` con escape; `retry_after` y `description`; 3 s entre mensajes y 1 s para el BOTON; tope de 12 por hora que el BOTON no espera; espera tras error creciente hasta 30 s, `retry_after` mayor manda, un envío correcto la termina |

Ejecutar desde la raíz del repositorio:

```
powershell -File firmware\base-radar\pruebas_host\ejecutar.ps1
bash firmware/base-radar/pruebas_host/ejecutar.sh
```

Si no hay `g++` ni `clang++`, el script de PowerShell usa `python -m ziglang c++`
(`python -m pip install ziglang==0.13.0`).
