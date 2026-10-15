# Pruebas host de la base

Pruebas unitarias de la lógica pura de la base que se compilan y ejecutan en un PC, sin ESP32,
sin radar, sin Arduino y sin red.

`test_logica.cpp` compila los `.cpp` reales y no duplica su lógica:

- `src/ventana_actividad.cpp`: criterio de "persona detectada" (mediana de |`fase_total`| sin
  ceros en 30 s, umbral 0.03) y "objetivo".
- `src/formato_nube.cpp`: JSON de lecturas, estado, colgante, evento e inicio de sesión; clave
  `ts` de 13 dígitos; hora del servidor desde `millis()`; lectura de las respuestas de Firebase.
- `src/cola_peticiones.cpp`: cola acotada de `loop()` a la tarea de red.
- `include/config.h`: capacidades, ventana y umbral.

Los JSON esperados son los mismos que escribe `web/simulador/simulador.py`, que pasó la
verificación contra las reglas publicadas. No usa `secrets.h`. Cada caso imprime `[ OK  ]` o
`[FALLO]`, y el programa termina con código `0` solo si todos pasan.

Estado actual: **29 casos y 137 verificaciones**.

| Área | Qué se verifica |
|---|---|
| Ventana | Vacía y solo ceros: sin objetivo; ignora 0 y NaN y usa el valor absoluto; mediana impar y par (promedio de los dos del medio, como `numpy.median`); umbral 0.03 incluido; ventana (t-30 s, t]; la persona se va y a los 30 s no hay objetivo; con más de 640 tramas se pisan las más antiguas; tasa real de 16.2 tramas/s con 1.5 % de ceros; vuelta de `millis()` |
| Lectura | Ejemplo completo de `docs/modelo-datos.md`; sin objetivo solo `persona: false, objetivo: false`; módulo en 0 se omite; `actividad` recortada a 10; `resp_rpm` que redondea a 0, > 60 o `latido_bpm` > 250 se omiten; `distancia_cm` > 1000 se omite; `diferida`; buffer chico |
| Clave y hora | 13 dígitos exactos; 12 o 14 dígitos y 0 (sin referencia) se rechazan; `ts` antes y después de la referencia y con vuelta de `millis()` |
| Estado, colgante, evento | JSON exactos con `{".sv":"timestamp"}`; RSSI recortado a -127..0; batería fuera de 0-5000 como 0; evento con y sin `detalle`; escape de comillas y control |
| Auth | Cuerpo de `signInWithPassword` con escape; `idToken`, `refreshToken`, `localId`, `expiresIn` de una respuesta con espacios; errores de Auth y de la base de datos; claves ausentes, no texto o que no caben |
| Cola | Orden eventos, colgantes, lecturas; sin hora del servidor no salen lecturas; un envío fallido no saca la petición; lecturas llenas descartan la más antigua; las lecturas nunca desplazan eventos y un evento sin lugar se rechaza y se cuenta; confirmar tras una lectura pisada no saca la nueva; un colgante reemplaza al pendiente del mismo id; más colgantes que lugares |

Ejecutar desde la raíz del repositorio:

```
powershell -File firmware\base-radar\pruebas_host\ejecutar.ps1
bash firmware/base-radar/pruebas_host/ejecutar.sh
```

Si no hay `g++` ni `clang++`, el script de PowerShell usa `python -m ziglang c++`
(`python -m pip install ziglang==0.13.0`).
