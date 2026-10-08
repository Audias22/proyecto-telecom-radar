# Escenarios de falla

Principio general: **la alerta local de la base (buzzer y LED) no depende de la red**. Si llega una
alerta del colgante o el radar detecta apnea, la base suena aunque no haya WiFi, Firebase ni
Telegram. Todo lo que va a la nube es un segundo aviso, con reintentos.

| # | Escenario | Cómo se detecta | Qué hace el sistema | Módulos |
|---|---|---|---|---|
| 1 | Paquete perdido (alerta o ACK) | El colgante no recibe ACK en `ACK_TIMEOUT_MS` | Reenvía la misma secuencia con `FLAG_REINTENTO` hasta `ACK_REINTENTOS` veces; luego barre canales. Si nada responde, tres beeps largos para que la persona sepa que no llegó. | colgante: `espnow_tx`, `energia` |
| 2 | Paquete duplicado | La base ve una secuencia igual o anterior a la última de ese colgante | No repite la alerta ni el aviso a la nube, pero responde ACK con `FLAG_ACK_DUPLICADO` para que el colgante deje de reintentar. | base: `espnow_rx` |
| 3 | El router cambia de canal | El colgante no recibe ACK en el canal guardado | Barre los canales 1-13 con la misma secuencia; guarda en RTC el canal que responde. Ver `docs/protocolo.md`. | colgante: `espnow_tx` |
| 4 | Caída de WiFi | `WiFi.status()` distinto de conectado | Alerta local normal. Lecturas a `cola_offline`; eventos y avisos de Telegram pendientes en una cola aparte que no descarta. Reconexión con espera creciente. Al volver, primero eventos y Telegram, después lecturas en orden. | base: `red_wifi`, `cola_offline`, `alerta_local` |
| 5 | Caída de Firebase (o token vencido) | Error HTTP o tiempo agotado; 401 si el token venció | Con 401, vuelve a iniciar sesión. Otros errores: igual que caída de WiFi, a la cola. Telegram no depende de Firebase, así que el aviso al cuidador sale aunque Firebase falle. | base: `firebase_cliente`, `cola_offline` |
| 6 | Base apagada (corte de luz, cable suelto) | La base no puede avisar. El panel ve `ultimo_contacto` con más de 3 min. El colgante no recibe ACK. | Panel muestra "base desconectada". El colgante da el patrón de error, así la persona sabe que el botón no funcionó. Al volver, la base compara la hora con su último `ultimo_contacto`, crea un evento `BASE_DESCONECTADA` con la duración y avisa por Telegram. | base, panel |
| 7 | Colgante sin batería | Heartbeat o alerta con `FLAG_BATERIA_BAJA`; o ningún mensaje en `COLGANTE_AUSENTE_S` | Con batería baja: evento `BATERIA_BAJA` y Telegram, una vez por descarga (no en cada heartbeat). Sin mensajes: evento `COLGANTE_AUSENTE` y Telegram; el panel lo marca ausente por `ultimo_heartbeat`. | base: `espnow_rx`, `telegram`; colgante: `bateria` |
| 8 | Persona fuera de la cama | `presencia = false` | No se evalúa apnea. Solo se evalúa si hay presencia continua y la distancia está dentro del rango confiable (40-200 cm). Ausencia no es apnea. | base: `radar` y la lógica de detección |
| 9 | Radar sin datos o con datos inválidos | No llegan tramas en N segundos, o valores fuera de rango (p. ej. `resp_rpm` > 60) | La lectura se marca `valida = false` y no se envía ni se usa para detectar apnea. Si dura más de N segundos, se reinicia la UART del radar, aviso local suave, evento `RADAR_SIN_DATOS` y Telegram. El panel también lo nota porque dejan de llegar lecturas. | base: `radar` |
| 10 | Reinicio inesperado (watchdog, brownout, fallo) | Al arrancar, `esp_reset_reason()` distinto de encendido normal | Watchdog activo sobre `loop()` para que un bloqueo termine en reinicio y no en una base colgada. Al arrancar se reporta el motivo por Telegram. Se pierden las secuencias en RAM (el siguiente mensaje de cada colgante se acepta como nuevo, a lo sumo una alerta repetida) y la cola offline. | base: `main` |

## Detalles y decisiones

### Apnea (escenario 8)

- Definición de trabajo: presencia continua, distancia en rango y respiración ausente (o
  `resp_rpm` = 0) durante un tiempo umbral. El umbral está por definir con pruebas; se parte de
  20 s para reducir falsas alarmas. Se registra cada prueba en `pruebas/`.
- Medido el 2026-10-06 (`radar.md`, sección 5): con el radar apuntando a una pared y sin nadie,
  el radar reporta presencia, respiración 0 y un latido congelado. Esa combinación es la misma
  que la definición de apnea de arriba, así que presencia + `resp_rpm` = 0 **no basta**. La
  detección tiene que exigir además señal de fase de una persona (en esa prueba `fase_resp` fue
  ~60 veces menor con la pared que con una persona) o que la persona se haya detectado
  respirando antes de que la respiración cayera a 0.
- Validación del 2026-10-07 (`radar.md`, secciones 9 y 10), con la persona sentada a ~84 cm:
  ninguna señal del radar siguió la frecuencia respiratoria real (8-31 rpm) y un detector por
  caída de amplitud no detectó una apnea de 20 s. La detección de apnea queda pendiente de
  repetir las pruebas en la posición de uso real (acostado, radar sobre el pecho).
- Si la persona se mueve mucho (se da vuelta en la cama) el radar puede dar lecturas inestables.
  Esas lecturas no deben disparar alerta; se descartan como en el escenario 9.
- Límite conocido: si la persona sale de la cama y no vuelve (por ejemplo, una caída fuera del
  cono del radar), el sistema no lo detecta como apnea. Solo queda el botón del colgante.

### Reconexión WiFi y ESP-NOW (escenario 4)

Mientras la base busca el router, su radio cambia de canal y el colgante puede no encontrarla.
Por eso los intentos de reconexión se espacian (espera creciente) en vez de buscar sin parar:
entre intentos la base queda en un canal fijo y el barrido del colgante puede encontrarla.
Por confirmar con el módulo: en qué canal queda la radio del ESP32-C6 entre intentos de reconexión.

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
| `APNEA` | 8 | base, al detectar respiración ausente con presencia |
| `BOTON` | 1, 2 | base, al recibir una alerta nueva del colgante |
| `BATERIA_BAJA` | 7 | base, al recibir `FLAG_BATERIA_BAJA` (una vez por descarga) |
| `BASE_DESCONECTADA` | 6 | base, al volver, con la duración en `detalle` |
| `COLGANTE_AUSENTE` | 7 | base, si pasa `COLGANTE_AUSENTE_S` sin mensajes del colgante |
| `RADAR_SIN_DATOS` | 9 | base, si pasan N segundos sin lecturas válidas del radar |

Todos los eventos se guardan en `/eventos` (ver `modelo-datos.md`) y se avisan por Telegram.
`COLGANTE_AUSENTE` y `RADAR_SIN_DATOS` se crean una sola vez por episodio: no se repiten mientras
la condición siga, y se pueden volver a crear cuando el colgante o el radar se recuperen y fallen
de nuevo.
