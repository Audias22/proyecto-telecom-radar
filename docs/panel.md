# Panel web del cuidador

Código en `web/panel/`, publicado con Firebase Hosting en https://monitor-respiracion.web.app.
HTML, CSS y JavaScript sin paso de compilación; Firebase JS SDK modular 10.14.1 desde el CDN de
gstatic. Las gráficas son SVG propio (sin librerías). Está pensado primero para el celular.

| Archivo | Contenido |
|---|---|
| `index.html` | Estructura: sesión, franja de emergencia, radar, equipos, cinta, gráficas, eventos |
| `estilos.css` | Estética de hoja clínica (marfil, papel milimetrado, tinta azul, rojo solo para alertas). Solo tema claro |
| `app.js` | Sesión, listeners de Realtime Database y pintado de la vista |
| `graficas.js` | Gráficas de línea con cursor y globo, y la cinta de 6 h con los eventos encima |
| `alerta.js` | Modo emergencia: título de la pestaña parpadeando y sonido (Web Audio) |
| `formato.js` | Umbrales, nombres de eventos, horas, porcentaje de batería y barras de señal |
| `config.js` | Configuración web de Firebase y `BASE_ID`. **No se sube a git**; se copia de `config.ejemplo.js` |

## Qué muestra

- **Sesión**: correo y contraseña (Firebase Auth). La cuenta debe estar en `/usuarios_panel`
  (`web/README.md`, "Cuentas"); si no está, el panel lo dice ("Sin permisos").
- **Radar**: "persona detectada" / "sin persona detectada" (con el motivo: sin objetivo o actividad
  baja, y la aclaración de que no significa cama vacía) / "radar sin datos recientes" si no llega
  una lectura en 30 s. Latido, actividad, distancia y hace cuánto llegó la última lectura.
- **Anillo de respiración**: con persona detectada y una `resp_rpm` en los últimos 30 s, se
  expande y contrae con un periodo de 60 / `resp_rpm` segundos (inhalación ~40 % del ciclo). Sin
  persona o sin dato queda quieto y gris y dice "sin dato", nunca "0". Se usa la última estimación
  de 30 s porque el módulo da `resp_rpm` en menos de la mitad de las lecturas (`radar.md`, 13.5).
  Respiración y latido llevan siempre la etiqueta "estimación del radar, no validada
  clínicamente". El panel no habla de apnea ni hace diagnósticos.
- **Base**: en línea si `ultimo_contacto` tiene 3 min o menos, si no "desconectada"
  (`modelo-datos.md`, sección 3; no se usa `online`). Hace cuánto, RSSI del WiFi en barras y
  versión de firmware.
- **Colgantes** (uno por nodo de `/colgantes`): pila dibujada con el porcentaje, voltaje, último
  contacto y RSSI en barras. Estado: **ausente** si `ultimo_heartbeat` pasa de 45 min
  (`COLGANTE_AUSENTE_S`), **batería baja** bajo 3500 mV (`BATERIA_BAJA_MV`) o con 0 mV (sin dato),
  si no **normal**. El porcentaje sale de una curva típica de Li-ion (3300 mV = 0 %, 4200 mV =
  100 %) y se muestra como aproximado: falta la curva de descarga medida (`protocolo.md`).
- **Cinta de las últimas 6 horas**: un minuto por columna: persona detectada (mayoría de lecturas
  del minuto), objetivo con actividad baja, sin objetivo o sin lecturas (línea punteada). Los
  eventos van clavados encima (rojo = botón; círculo hueco = sin atender); al pasar el mouse,
  tocar o enfocar con teclado muestran hora, tipo y detalle. El recuadro punteado marca el rango
  de las gráficas.
- **Gráficas** (15 min / 1 h / 6 h, se recuerda la elección): respiración, latido y actividad, cada
  una con su eje (sin doble eje). El trazo se corta donde faltan datos. Cuando hay más lecturas
  que medio píxel por punto (6 h, o 1 h en el celular), cada punto es la mediana de su tramo y el
  globo dice de cuántas lecturas. La actividad usa escala de raíz para que el umbral 0.03 se vea
  aunque haya picos de 1 o más.
- **Eventos**: los últimos 80 más todos los que sigan sin atender, con hora, tipo, detalle y
  origen. "Atendido" escribe `atendido: true`, `atendido_por` (uid) y `atendido_en` (hora del
  servidor) en un solo `update`. Se muestra "Atendido por ti" o "por otro cuidador (uid…)"; las
  reglas solo guardan el uid, así que el panel no puede mostrar el correo de otro cuidador.
- **Modo emergencia**: mientras haya un `BOTON` sin atender, franja roja fija arriba con el más
  reciente y "Marcar atendido", fondo del panel en rojo pálido, título de la pestaña alternando
  "AUXILIO: botón presionado", y un pitido cada 2.5 s. El navegador no deja sonar audio antes de
  la primera interacción, así que la franja dice "Toca la pantalla para activar el sonido" hasta
  entonces. "Silenciar sonido" solo afecta a ese equipo. Con varios `BOTON` pendientes hay que
  atenderlos todos.
- **Errores visibles**: credenciales incorrectas, sin internet o sin conexión con Firebase (franja
  ámbar; el SDK se reconecta solo), sin permisos (franja roja), fallo al guardar "Atendido" (junto
  al evento), y aviso si `app.js` o `config.js` no cargan en 8 s.

Respeta `prefers-reduced-motion`: el anillo queda fijo y no hay animaciones de números, destellos
ni parpadeo de la franja.

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
versión publicada. Sin probar: el sonido en un celular real y el aviso de sin conexión cortando la
red.
