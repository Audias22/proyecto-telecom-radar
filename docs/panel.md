# Panel web del cuidador

Código en `web/panel/`, publicado con Firebase Hosting en https://monitor-respiracion.web.app.
HTML, CSS y JavaScript sin paso de compilación; Firebase JS SDK modular 10.14.1 desde el CDN de
gstatic. Las gráficas son SVG propio (sin librerías). Está pensado primero para el celular.

| Archivo | Contenido |
|---|---|
| `index.html` | Estructura: sesión, franja de emergencia, radar, equipos, cinta, gráficas, eventos |
| `estilos.css` | Estética de hoja clínica (marfil, papel milimetrado, tinta azul, rojo solo para alertas). Solo tema claro |
| `app.js` | Sesión, listeners de Realtime Database y pintado de la vista |
| `graficas.js` | Gráficas de línea, detalle de un punto, eje de tiempo y la cinta de 6 h con los eventos encima |
| `ampliada.js` | Vista ampliada de una gráfica: zoom, arrastre, pellizco, Volver / Atrás / Esc |
| `alerta.js` | Modo emergencia: título de la pestaña parpadeando y sonido (Web Audio) |
| `formato.js` | Umbrales, nombres de eventos, horas, porcentaje de batería y barras de señal |
| `config.js` | Configuración web de Firebase y `BASE_ID`. **No se sube a git**; se copia de `config.ejemplo.js` |

## Qué muestra

- **Sesión**: correo y contraseña (Firebase Auth). La cuenta debe estar en `/usuarios_panel`
  (`web/README.md`, "Cuentas"); si no está, el panel lo dice ("Sin permisos").
- **Radar**: "persona detectada" / "sin persona detectada" (con el motivo: sin objetivo o actividad
  baja, y la aclaración de que no significa cama vacía) / sin datos si no llega una lectura en
  20 s (ver "Tiempos de cada estado"). Latido, actividad, distancia y hace cuánto llegó la última
  lectura.
- **Anillo de respiración**: con persona detectada y una `resp_rpm` en los últimos 30 s, se
  expande y contrae con un periodo de 60 / `resp_rpm` segundos (inhalación ~40 % del ciclo). Sin
  persona o sin dato queda quieto y gris y dice "sin dato", nunca "0". Se usa la última estimación
  de 30 s porque el módulo da `resp_rpm` en menos de la mitad de las lecturas (`radar.md`, 13.5).
  Respiración y latido llevan siempre la etiqueta "estimación del radar, no validada
  clínicamente". El panel no habla de apnea ni hace diagnósticos.
- **Base**: en línea / sin datos hace X s / desconectada según su señal más reciente (ver "Tiempos
  de cada estado"; no se usa `online`). "Último dato" con hora hh:mm:ss, RSSI del WiFi en barras
  y versión de firmware.
- **Colgantes** (uno por nodo de `/colgantes`): pila dibujada con el porcentaje, voltaje, último
  contacto y RSSI en barras. Estado: **ausente** si `ultimo_heartbeat` pasa de 45 min
  (`COLGANTE_AUSENTE_S`), **sin reporte reciente** (ámbar) si pasa de 30 min con la base en línea,
  **batería baja** bajo 3500 mV (`BATERIA_BAJA_MV`) o con 0 mV (sin dato),
  si no **normal**. El porcentaje sale de una curva típica de Li-ion (3300 mV = 0 %, 4200 mV =
  100 %) y se muestra como aproximado: falta la curva de descarga medida (`protocolo.md`).
- **Datos viejos**: el colgante y el radar solo llegan a Firebase a través de la base. Si la base
  está desconectada, el colgante dice "sin datos (base desconectada)" en vez de normal / batería
  baja / ausente, la pila y las cifras quedan en gris y el voltaje y la señal dicen "último dato"
  con la hora; el radar dice "Sin datos (base desconectada)" con la hora de la última lectura y el
  WiFi de la base dice "último dato". Si el que perdió la conexión es el panel, lo mismo con
  "panel sin conexión", y la base tampoco se marca en línea ni desconectada.
- **Cinta de las últimas 6 horas**: un minuto por columna: persona detectada (mayoría de lecturas
  del minuto), objetivo con actividad baja, sin objetivo o sin lecturas (línea punteada). Los
  eventos van clavados encima (rojo = botón; círculo hueco = sin atender); al pasar el mouse,
  tocar o enfocar con teclado muestran hora, tipo y detalle. El recuadro punteado marca el rango
  de las gráficas.
- **Gráficas** (15 min / 1 h / 6 h, se recuerda la elección): respiración, latido y actividad, cada
  una con su eje (sin doble eje). El trazo se corta donde faltan datos. Cuando hay más lecturas
  que medio píxel por punto (6 h, o 1 h en el celular), cada punto es la mediana de su tramo y el
  detalle lo aclara. La actividad usa escala de raíz para que el umbral 0.03 se vea aunque haya
  picos de 1 o más; el umbral se nombra en la leyenda de la gráfica y su marca del eje va en
  ámbar, así no queda tapado por la línea. El eje de tiempo elige el paso para dejar ~90 px entre
  etiquetas (de 5 s a 2 h) y pasa a hh:mm:ss con pasos de menos de un minuto.
- **Detalle de un punto** (vista normal y ampliada): con el mouse encima, o al tocar o deslizar el
  dedo sobre la gráfica, se marca la lectura real más cercana (no la mediana) con una línea
  vertical y una tarjeta: hora hh:mm:ss, valor, y de esa misma lectura si había persona detectada
  y la actividad. Vale cualquier toque dentro de 1/12 del ancho visible (zona de toque
  generosa). La tarjeta se ubica arriba del punto (el dedo tapa lo de abajo) y nunca sale de la
  pantalla. Con el dedo queda puesta hasta tocar en otro lado o hacer scroll, y se mantiene al
  llegar datos nuevos.
- **Vista ampliada**: el botón "Ampliar" de cada gráfica la abre; en escritorio también el clic
  sobre la gráfica. En el celular tocar la gráfica muestra el detalle del punto y no la abre, para
  que un toque no haga dos cosas. Es pantalla completa en el celular y un panel grande en
  escritorio. Trabaja sobre las 6 h ya descargadas (no hace consultas nuevas):
  - Zoom con pellizco o rueda del mouse alrededor del dedo o del puntero, arrastre con un dedo o
    con el mouse, y botones "+", "−" y "Restablecer" (también las teclas + y −). Entre 1 min y
    6 h.
  - Arriba se ve el tramo visible ("23:24:12 – 23:25:12 · 1 min") y si está **en vivo** (el
    borde derecho es "ahora" y avanza con cada lectura, sin cambiar el zoom) o **detenida en
    este tramo** (después de moverse al pasado; los datos nuevos llegan pero el tramo no se
    mueve). "Restablecer" vuelve al rango del selector, en vivo.
  - "← Volver", el botón Atrás del celular o del navegador y Esc la cierran (al abrir se agrega
    una entrada al historial, así Atrás no sale del panel).
  - La franja de emergencia queda encima, en su versión delgada, y la vista empieza debajo de
    ella.
- **Eventos**: los últimos 80 más todos los que sigan sin atender, con hora, tipo, detalle y
  origen. "Atendido" escribe `atendido: true`, `atendido_por` (uid) y `atendido_en` (hora del
  servidor) en un solo `update`. Se muestra "Atendido por ti" o "por otro cuidador (uid…)"; las
  reglas solo guardan el uid, así que el panel no puede mostrar el correo de otro cuidador.
- **Contadores**: la franja cuenta solo botones ("5 botones sin atender"); Eventos cuenta todos los
  tipos ("10 avisos sin atender").
- **Modo emergencia**: mientras haya un `BOTON` sin atender, franja roja fija arriba con cuántos
  botones hay, el más reciente y "Marcar atendido" (marca ese). Con 2 o más aparece "Atender todos
  (N)", que pide confirmación en un diálogo y marca solo los que estaban pendientes al abrirlo (un
  `update` por evento, el mismo que el de un evento suelto). Al bajar más de 60 px la franja se
  reduce a una barra delgada ("Auxilio / N botones", "Marcar atendido" y "Silenciar") y vuelve a
  su tamaño al subir cerca del inicio. Fondo del panel en rojo pálido, título de la pestaña alternando
  "AUXILIO: botón presionado", y un pitido cada 2.5 s. El navegador no deja sonar audio antes de
  la primera interacción, así que la franja dice "Toca la pantalla para activar el sonido" hasta
  entonces. "Silenciar sonido" solo afecta a ese equipo. Con varios `BOTON` pendientes hay que
  atenderlos todos.
- **Errores visibles**: credenciales incorrectas, sin internet o sin conexión con Firebase (franja
  ámbar; el SDK se reconecta solo), sin permisos (franja roja), fallo al guardar "Atendido" (junto
  al evento), y aviso si `app.js` o `config.js` no cargan en 8 s.

Respeta `prefers-reduced-motion`: el anillo queda fijo y no hay animaciones de números, destellos
ni parpadeo de la franja.

## Tiempos de cada estado

El panel recalcula todos estos estados cada segundo con un reloj propio, aunque no llegue nada de
Firebase, y otra vez en cuanto la pestaña vuelve a estar visible (en el celular, con la pantalla
apagada o la pestaña en segundo plano, el navegador frena o congela los temporizadores). No hace
falta recargar. "Ahora" es la hora del servidor: `Date.now()` más `.info/serverTimeOffset`, y
todas las horas de los datos (`ultimo_contacto`, la clave de cada lectura, `ultimo_heartbeat`)
también son del servidor, así que un reloj mal puesto en el celular no cambia ningún estado.

**Base.** Su señal más reciente es la mayor entre `ultimo_contacto` (la base lo escribe cada
60 s) y la hora de la última lectura (cada 5 s).

| Sin señal | Estado | Por qué |
|---|---|---|
| hasta 20 s | en línea (verde) | Cubre perder hasta 3 lecturas seguidas sin alarmar |
| más de 20 s | sin datos hace X s (ámbar) | Con lecturas cada 5 s, 20 s ya son 4 perdidas: se avisa rápido, pero sin decir "desconectada" |
| más de 3 min | desconectada (rojo) | El mismo umbral que usa la base para el evento `BASE_DESCONECTADA` y el aviso por Telegram (`BASE_DESCONECTADA_MIN_MS`, `fallos.md` escenario 6), así panel y Telegram dicen lo mismo |

Si el radar deja de mandar pero la base sigue viva, la base solo escribe `estado` cada 60 s.
Con el umbral de 20 s pasaría a ámbar entre una actualización y la siguiente. Por eso, cuando
la última lectura es de antes del último `estado` por más de 70 s, el umbral del ámbar pasa a
75 s (60 s de periodo más margen). En la práctica, al callarse el radar la base pasa una sola
vez por ámbar (hasta su siguiente `estado`) y después queda en línea, y el radar dice "Radar sin
datos recientes".

**Radar.** Sin lecturas en 20 s, el radar no se presenta como dato actual: latido, actividad y
distancia dicen "sin dato" y el anillo queda quieto. El título depende de la causa: "Sin datos
hace X s" (ámbar) si la base está en ámbar, "Radar sin datos recientes" si la base sigue en línea
(falla del radar), y "Sin datos (base desconectada)" a los 3 min. La respiración y el latido usan
la última estimación de 30 s, porque el módulo no la da en todas las lecturas.

**Colgante.** Manda un heartbeat cada 15 min (`HEARTBEAT_INTERVALO_S` en
`firmware/comun/protocolo/protocolo.h`; `HEARTBEAT_PRUEBA_S` en `firmware/colgante/include/config.h`
está en 0, así que vale el normal).

| Sin mensajes del colgante | Estado | Por qué |
|---|---|---|
| hasta 30 min | normal (o batería baja) | Hasta un heartbeat perdido es normal (un mensaje que no llegó) |
| más de 30 min, con la base en línea | sin reporte reciente (ámbar) | El doble del intervalo: se perdieron al menos 2 heartbeats. Aviso temprano, no se declara ausente |
| más de 45 min | ausente (rojo) | `COLGANTE_AUSENTE_S` (3 heartbeats): el mismo umbral del evento `COLGANTE_AUSENTE` de la base |
| base desconectada (o panel sin conexión) | sin datos (base desconectada), en gris | El colgante solo llega a Firebase a través de la base: sin base, su último dato no dice nada del estado actual |

"Sin reporte reciente" solo se muestra con la base en línea (no en ámbar ni desconectada): si la
base no está al día, lo que falta es la base, no el colgante.

**Panel sin conexión.** Si `.info/connected` es falso, base, colgante y radar dicen "sin datos
(panel sin conexión)" en gris: el panel no sabe si la base sigue escribiendo.

## Qué descarga (plan Spark)

| Listener | Consulta | Volumen |
|---|---|---|
| Lecturas | `/lecturas/base01`, `orderByKey()`, `startAt(ahora - 6 h)`, después solo las nuevas | ~4300 lecturas al abrir (~0.4 MB), ~1.7 kB/min abierto |
| Estado de la base | `/dispositivos/base01/estado` | un nodo por minuto |
| Colgantes | `/colgantes` | un nodo cada 15 min por colgante |
| Eventos recientes | `/eventos`, `orderByChild('timestamp')`, `limitToLast(80)` | pocos kB |
| Eventos sin atender | `/eventos`, `orderByChild('atendido')`, `equalTo(false)` | pocos kB; usa el `.indexOn` de `atendido` |

Nunca se descarga todo `/lecturas`. Las gráficas de 15 min y 1 h usan la misma ventana de 6 h
que la cinta. La hora "ahora" es la del servidor (`.info/serverTimeOffset`), así que un reloj mal
puesto en el celular no cambia el estado en línea / desconectada.

La consulta de lecturas se abre después de la primera respuesta del estado de la base: si sale
antes de que la conexión tenga el token de la sesión, Firebase la rechaza con
`permission_denied` y la cancela (pasó en las pruebas).

## Publicar y probar

```
cd web
copy panel\config.ejemplo.js panel\config.js     (llenar con el firebaseConfig de la app "panel")
firebase serve --only hosting --port 5050        (prueba local en http://localhost:5050)
firebase deploy --only hosting
```

El `firebaseConfig` se obtiene con `firebase apps:sdkconfig web` o en la consola (Configuración
del proyecto > Tus apps > panel). Hosting sirve html, js y css con `Cache-Control: no-cache` para
que un cambio llegue al recargar.

Para tener datos sin la base: `web/simulador/simulador.py normal` (`web/README.md`). Sus eventos
tienen `detalle` que empieza con `simulador:`.

Prueba del 28 oct 2026, en Edge (Playwright) contra el proyecto real con el simulador en modo
`normal` y la cuenta del cuidador: inicio de sesión (y error con contraseña incorrecta), sin
desbordamiento horizontal a 390 px, escritorio a 1366 px, globo en gráficas y cinta, modo
emergencia con el `BOTON` del simulador y "Atendido" guardando quién y cuándo. Lo mismo contra la
versión publicada.

Prueba del 28 oct 2026 (correcciones): la franja compacta, los contadores y "Atender todos"
(cancelar no escribe nada; confirmar hace un `update` por botón y la franja desaparece) se
probaron a 390 px y 1366 px con un Firebase falso: Playwright sirve versiones simuladas de los
módulos del SDK y registra los `update` sin escribir en el proyecto. "Datos viejos" se probó
también con los datos reales (base desconectada), solo leyendo.

Prueba del 28 oct 2026 (gráficas): a 390 px con toques simulados (Playwright; pellizco y
arrastre de dedo por CDP) y a 1366 px con mouse, con el Firebase falso (que además agrega una
lectura cada 2 s) y con los datos reales solo leyendo: detalle en la vista normal y en la
ampliada dentro de la pantalla, Ampliar, franja de emergencia encima, pellizco, "+" hasta 1 min
con el eje en segundos, arrastre al pasado que no se mueve con 3 lecturas nuevas, en vivo que
avanza sin perder el zoom, Restablecer, rueda, arrastre con mouse, Atrás, Volver, Esc y
`prefers-reduced-motion` sin animación de apertura.

Prueba del 29 oct 2026 (tiempos): con el Firebase falso (lecturas cada 5 s, estado cada 60 s) y
el reloj simulado de Playwright, sin recargar: al cortar todo, en línea hasta 19 s, "sin datos
hace 29 s" en ámbar en la base y el radar, el contador sube, y "desconectada" pasados 3 min, con
el colgante en "sin datos (base desconectada)". Con el radar callado y el estado cada 60 s, un
solo paso por ámbar y luego en línea durante 6 min, nunca desconectada. Colgante: normal a 20 y
25 min, "sin reporte reciente" a 31 min (también al pasar el tiempo sin recargar), ausente a 46
min, y gris con la base desconectada. Con el reloj del celular 5 min adelantado, el offset del
servidor mantiene la base en línea. Sin probar: el pellizco y el arrastre en un celular real (el
sonido, el login y el modo emergencia ya se probaron en el celular) y el aviso de sin conexión
cortando la red.
