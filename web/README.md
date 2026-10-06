# web

Configuración de Firebase y panel web del cuidador.

| Archivo | Contenido |
|---|---|
| `firebase.json` | Qué se publica en Hosting, archivo de reglas, puertos de emuladores |
| `.firebaserc.example` | Plantilla del id de proyecto. Copiar como `.firebaserc` |
| `database.rules.json` | Reglas de seguridad de Realtime Database (ver `docs/modelo-datos.md`) |
| `panel/` | Panel web (HTML/JS) que se publica con Firebase Hosting |

Se usa el plan Spark (gratis): Realtime Database, Hosting y Authentication. No se usan Cloud
Functions porque no están disponibles en ese plan.

## Primer uso

Requiere Node.js y Firebase CLI (`npm install -g firebase-tools`).

```
cd web
copy .firebaserc.example .firebaserc        (poner el id real del proyecto)
copy panel\config.example.js panel\config.js (poner la configuración web del proyecto)
firebase login
firebase deploy --only database             (sube las reglas)
firebase deploy --only hosting              (publica el panel)
```

## Cuentas

En la consola de Firebase, Authentication, crear con correo y contraseña:

- Una cuenta por base (la que va en `secrets.h` de la base).
- Una cuenta por cada cuidador que usa el panel.

Después, en Realtime Database, desde la consola (la consola no pasa por las reglas):

- `/cuentas_dispositivo/{uid de la base}` = `"base01"`
- `/usuarios_panel/{uid del cuidador}` = `true`

Conviene desactivar el registro público de usuarios en Authentication para que nadie más pueda
crear cuentas.

## Probar reglas sin tocar el proyecto real

```
firebase emulators:start --only database,auth,hosting --project demo-monitor
```
