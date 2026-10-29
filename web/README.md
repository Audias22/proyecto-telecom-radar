# web

Configuración de Firebase, panel web del cuidador y simulador de la base.

| Archivo | Contenido |
|---|---|
| `firebase.json` | Qué se publica en Hosting, archivo de reglas, puertos de emuladores |
| `.firebaserc` | Id del proyecto de Firebase (`monitor-respiracion`); no es secreto |
| `database.rules.json` | Reglas de seguridad de Realtime Database (ver `docs/modelo-datos.md`) |
| `panel/` | Panel web del cuidador (HTML/CSS/JS) que se publica con Firebase Hosting (`docs/panel.md`) |
| `simulador/` | Simulador de la base y verificación de las reglas (Python, solo librería estándar) |

Se usa el plan Spark (gratis): Realtime Database, Hosting y Authentication. No se usan Cloud
Functions ni nada que requiera el plan Blaze.

Proyecto: `monitor-respiracion`, base de datos
`https://monitor-respiracion-default-rtdb.firebaseio.com` (us-central1).

## Requisitos

- Node.js y Firebase CLI (`npm install -g firebase-tools`). Probado con Node 24.13 y
  firebase-tools 14.22.
- Python 3.8 o más nuevo para el simulador (sin paquetes extra).

## Cuentas

En la consola de Firebase, Authentication, crear con correo y contraseña:

- Una cuenta por base (la que va en `secrets.h` de la base). Hoy: `base01@monitor-respiracion.app`.
- Una cuenta por cada cuidador que usa el panel.

Después, en Realtime Database, desde la consola (la consola no pasa por las reglas):

- `/cuentas_dispositivo/{uid de la base}` = `"base01"`
- `/usuarios_panel/{uid del cuidador}` = `true`

Conviene desactivar el registro público de usuarios en Authentication para que nadie más pueda
crear cuentas.

## Publicar las reglas de la base de datos

Desde PowerShell, en la carpeta `web` (ahí están `firebase.json` y `.firebaserc`):

```
cd web
firebase login
firebase projects:list
firebase deploy --only database
```

- `firebase login` abre el navegador para iniciar sesión con la cuenta de Google dueña del
  proyecto. Se hace una sola vez por computadora.
- `firebase projects:list` debe mostrar `monitor-respiracion`.
- `--only database` publica solo `database.rules.json`; no toca Hosting ni los datos.

Después de publicar, correr la verificación del simulador (abajo). Para volver atrás, la consola
guarda el historial de reglas (Realtime Database > Reglas).

## Simulador de la base

`simulador/simulador.py` inicia sesión con la cuenta de la base (API REST de Firebase Auth) y
escribe por REST lo mismo que escribirá la base: estado cada minuto, una lectura cada 5 s,
heartbeat del colgante cada 15 min y eventos. Renueva el token antes de que venza (1 h) y vuelve a
iniciar sesión ante un 401. Sirve para probar el panel sin la base y para verificar las reglas.

Credenciales: copiar la plantilla y llenarla. `credenciales.local.json` no se sube al repositorio.

```
cd web\simulador
copy credenciales.example.json credenciales.local.json
notepad credenciales.local.json
```

- `api_key`: el `apiKey` del `firebaseConfig` de la app web "panel".
- `base.clave`: la contraseña de `base01@monitor-respiracion.app`.
- `cuidador`: correo y contraseña del cuidador. Opcional: solo los usa `verificar`; si quedan
  vacíos se omiten los casos del cuidador.

Modos:

```
py simulador.py verificar
py simulador.py normal
py simulador.py desconectada
py simulador.py bateria-baja --duracion 120
```

| Modo | Qué hace |
|---|---|
| `verificar` | Prueba las reglas: casos que deben pasar y casos que deben fallar (sin token, ruta de otra base, campos inválidos, `APNEA`, cuidador intentando escribir...). Imprime `OK`/`FALLO` por caso y termina con código 1 si alguno falla |
| `normal` | Base funcionando: estado, lecturas, heartbeat y un evento `BOTON` a los 30 s (`--boton-cada N` lo repite cada N s) |
| `desconectada` | Escribe 60 s (`--antes`), deja de escribir 240 s (`--corte`; el panel debe mostrar "base desconectada" a los 3 min) y al volver crea `BASE_DESCONECTADA` con la duración |
| `bateria-baja` | El colgante reporta 3400 mV y se crea un evento `BATERIA_BAJA` |

Opciones comunes: `--duracion S` (0 = hasta Ctrl+C), `--heartbeat S`, `--credenciales ruta`.

Notas:

- El simulador escribe en el proyecto real, en `/lecturas/base01`, con `version_firmware` =
  `"simulador"` y `detalle` de los eventos empezando con `simulador:`. Una hora de simulador son
  720 lecturas (~100 kB). Para borrar lo simulado: consola > Realtime Database, borrar los nodos.
- `verificar` borra sus lecturas de prueba al terminar, pero deja un evento `BOTON` con detalle
  `verificacion de reglas` (ni la base ni el cuidador pueden borrar eventos). Si se pasaron las
  credenciales del cuidador, queda marcado como atendido.
- Si `py` no existe, usar `python`.

## Panel

Publicado en https://monitor-respiracion.web.app. Detalle de qué muestra, qué consultas hace y cómo
se probó en `docs/panel.md`.

```
cd web
copy panel\config.ejemplo.js panel\config.js   (poner el firebaseConfig de la app "panel")
firebase serve --only hosting --port 5050       (prueba local)
firebase deploy --only hosting
```

`config.js` no se sube al repositorio. El `firebaseConfig` sale de
`firebase apps:sdkconfig web` o de la consola.

## Probar reglas sin tocar el proyecto real

Requiere Java (los emuladores de Firebase corren sobre Java; hoy no está instalado en la PC del
proyecto):

```
firebase emulators:start --only database,auth,hosting --project demo-monitor
```
