# firmware/comun

Código compartido por los dos firmwares.

- `protocolo/protocolo.h`: formato del paquete ESP-NOW y constantes del enlace colgante -> base
  (canal, tiempos de ACK, reintentos, heartbeat, umbral de batería). La especificación está en
  `docs/protocolo.md`.
- `protocolo/duplicados.h`: regla de la base para decidir si un mensaje del colgante es nuevo o
  un duplicado (`docs/protocolo.md`, sección 5). Lógica pura; la prueban las pruebas host del
  colgante.

Los proyectos `base-radar` y `colgante` la incluyen con
`lib_deps = symlink://../comun/protocolo` en su `platformio.ini`, así que el archivo existe en un
solo lugar. Cualquier cambio aquí afecta a ambos firmwares: hay que recompilar y volver a cargar
los dos.
