# Sistema de monitoreo de respiración y auxilio para adultos mayores

Proyecto de Telecomunicaciones — Décimo Semestre
Universidad Mariano Gálvez, Guatemala
Periodo: 9 de agosto – 24 de octubre de 2026

| Integrante | Carné |
|---|---|
| Rudi Audias Guevara Mejicanos | 1190-22-8232 |
| Jose Abraham Bojorquez Rodriguez | 1190-22-13219 |
| Marcos Estuardo Franco Hernandez | 1190-22-5604 |
| Brenner Yubiny Granados Pinto | 1190-22-898 |

Desde el 12 de octubre de 2026, el desarrollo y la integración del proyecto los hace Rudi Audias
Guevara Mejicanos; el resto del equipo pasó a otros proyectos de la universidad.

---

## Estado general del proyecto

> Esta sección se actualiza en conjunto, cada sábado en el sprint, o cuando algo cambie de estado.

| Componente | Estado | Notas |
|---|---|---|
| Radar (MR60BHA2) | Recibido y probado | Guatemala Digital, Q322.40 (con envío), orden #1493658. Lectura integrada en el firmware de la base: ~16 muestras de fase por segundo con persona. Probado sentado, acostado y una noche completa: detecta a una persona cuando la ve, pero durmiendo la perdió ~la mitad de la noche; la detección de apnea **no se usa como alarma** (`docs/radar.md`, sección 13) |
| Colgante (botón + ESP32) | Pasó las pruebas en banco | Firmware de Abraham revisado y fusionado el 12 oct (39 casos de prueba en PC). Pruebas 1 a 9 en banco el 12 oct, por USB y con batería simulada: arranque, ALERTA con ACK, beeps, sin receptor, barrido de canales, ACK perdido y duplicados, heartbeat por timer, sin falsas alertas y potencia de transmisión (`pruebas/resultados/2026-10-12_colgante_banco.md`). Falta: despertar en distintos momentos del intervalo, RSSI bajo de la antena del ESP32-C3 (~-85 dBm a 3 m), alcance en la casa. 27 oct: armado con batería (R3 del TP4056 cambiada por 4.7 kΩ, carga a ~237 mA; `OUT+` al pin 5V del C3; divisor 100 kΩ / 100 kΩ en GPIO1) y firmware con la medición real de batería; falta comparar la lectura con el multímetro y medir el consumo en deep sleep (multímetro sin escala de mA). Conexiones en `firmware/colgante/README.md`. Falta diseñar/imprimir carcasa y cordón |
| Base + notificación | Firebase probado en hardware; Telegram, eventos y cola offline listos, por confirmar en hardware | Usa el XIAO ESP32-C6 del kit con su antena externa. Lee el radar, recibe al colgante por ESP-NOW (ACK y duplicados), maneja el LED WS2812 y el buzzer en D10, y el botón BOOT silencia la alerta. Se conecta al router con reconexión propia. Probado el 15 oct (v0.5.1): escribe en Firebase estado, lecturas cada 5 s, colgante y evento BOTON (~270 ms del botón al evento), y sin internet reporta el error en 5 s y reintenta sin colgarse. v0.6.0 (27 oct, compilado y 63 casos de prueba en PC): avisos por Telegram, eventos BATERIA_BAJA, COLGANTE_AUSENTE, RADAR_SIN_DATOS y BASE_DESCONECTADA una vez por episodio, 1 h de lecturas sin red, eventos que sobreviven a un reinicio, watchdog de tareas y aviso de reinicio inesperado; tabla de particiones sin OTA (app de 3 MB, al 40 %). Guía de prueba en `firmware/base-radar/README.md` |
| Firebase + panel web | Panel publicado | Modelo de datos y reglas publicados (57 casos de verificación); la base y el simulador (`web/simulador/`) escriben en el proyecto real. Panel del cuidador en https://monitor-respiracion.web.app (28 oct): estado de la base y del colgante, radar con anillo de respiración, cinta de 6 h, gráficas, eventos con "Atendido" y modo emergencia. Probado con el simulador en Edge; falta probarlo en un celular real (`docs/panel.md`) |
| Documento de propuesta | Listo | Ver `/docs/propuesta.docx` (o donde se suba) |
| Cronograma | Listo | 11 semanas, del 9 ago al 24 oct |

**Próximo sprint (sábado):** definir aquí antes de cada reunión qué se va a lograr ese día.

---

## Arquitectura del sistema

El sistema tiene dos vías de detección que trabajan juntas, más una unidad base que las conecta con el cuidador. Detalle técnico y diagrama en `docs/arquitectura.md`.

**1. Vía automática — Radar (60 GHz FMCW)**
Un módulo MR60BHA2 se coloca cerca de la cama y mide el micro-movimiento del pecho de la persona, sin cámara y sin contacto. Funciona en oscuridad total y a través de sábanas o ropa. El módulo trae integrado un XIAO ESP32-C6 que procesa la señal.

Estado de las pruebas (octubre 2026, `docs/radar.md`, secciones 11-13): con el cuarto vacío el radar no confunde objetos con una persona, pero en una noche real perdió a la persona dormida (boca abajo) cerca de la mitad del tiempo, así que informa "persona detectada" / "sin persona detectada", no "cama vacía". La frecuencia respiratoria no está validada y el mejor detector de apnea dio ~6 falsas alarmas por hora de sueño: **la apnea por radar no dispara alarmas**.

- Rango confiable de medición: 0.4 a 2 metros
- Cono de detección: aproximadamente 80° horizontal y 80° vertical
- Una persona a la vez, en reposo
- La frecuencia respiratoria y el latido que entrega el módulo son estimaciones no validadas

**2. Vía manual — Colgante con botón de auxilio**
Un colgante con un solo botón grande (decisión firme: sin múltiples botones ni voz, por simplicidad para adultos mayores). Usa un ESP32-C3 Super Mini con buzzer, batería Li-ion de 240 mAh y cargador TP4056. Pasa el tiempo en deep sleep; al presionar el botón despierta, envía una alerta por ESP-NOW (2.4 GHz) a la base, espera confirmación (ACK) y vuelve a dormir. Un beep corto confirma que la alerta llegó; tres beeps largos indican que no llegó. También despierta cada 15 minutos para mandar un heartbeat con el nivel de batería. Protocolo en `docs/protocolo.md`.

**3. Unidad base**
Es el mismo XIAO ESP32-C6 del kit del radar, sin un segundo microcontrolador. Lee el radar, recibe las alertas del colgante, activa un buzzer y LED locales, y por WiFi:

- Envía lecturas, estado y eventos a Firebase Realtime Database por HTTPS.
- Envía las alertas al cuidador por Telegram, directamente desde la base.

La alerta local no depende de la red: si falla el WiFi o Firebase, la base suena igual y guarda las lecturas en una cola para reenviarlas. Escenarios de falla en `docs/fallos.md`.

**4. Nube y panel web**
Firebase en plan Spark (gratis): Realtime Database para los datos, Authentication para las cuentas y Hosting para un panel web con gráficas de respiración y la lista de eventos. No se usan Cloud Functions porque no están en el plan gratis; por eso la base manda los avisos de Telegram por su cuenta. Modelo de datos en `docs/modelo-datos.md`.

### Las señales del sistema

| Enlace | Banda / tipo | Función |
|---|---|---|
| Sensor (radar) | 60 GHz, FMCW (onda continua modulada en frecuencia) | Mide el movimiento del pecho; en el sistema indica si hay una persona en la cama |
| Alertador (colgante -> base) | 2.4 GHz, digital por paquetes, ESP-NOW sobre 802.11 | Comunica la alerta manual, bajo consumo |
| Base -> nube | WiFi 802.11 + HTTPS (API REST de Firebase) | Lecturas, estado y eventos |
| Aviso al cuidador (base -> teléfono) | WiFi 802.11 + HTTPS (API de bots de Telegram) | Notifica al cuidador en su celular |

### Diferenciador

El sistema vigila sin cámara, lo que permite instalarlo en el dormitorio — el lugar donde más se necesita monitoreo y donde una cámara sería inaceptable por privacidad. El botón es la vía de alerta confiable; el radar complementa con información de presencia en la cama sin que la persona tenga que hacer nada.

### Límites conocidos (declarados desde ya, no ocultos)

- Detecta a una persona a la vez, en reposo, a corta distancia
- No es una cámara, no identifica objetos ni personas
- El radar no cubre 360°, solo su cono de detección
- No se recomienda usar varios radares juntos para cubrir 360° (interferencia mutua entre módulos de 60 GHz)
- Si la base se apaga, el aviso por Telegram no sale en ese momento (no hay servidor propio); el panel lo muestra y el colgante avisa con el patrón de error
- **La apnea por radar no dispara alarmas.** No se siguió la respiración real (8-30 rpm) y en una noche completa el mejor detector dio ~6 falsas alarmas por hora de sueño. Una apnea solo se avisa con el botón (`docs/radar.md`, sección 13; `docs/fallos.md`)
- Durmiendo boca abajo, o si el radar se mueve, el radar puede no ver a la persona durante horas; "sin persona detectada" no significa cama vacía
- La bandera de presencia del propio radar sigue en 1 con la cama vacía (detecta almohada y cobijas); la base usa su propia medida de actividad para saber si hay una persona

---

## Estructura del repositorio

```
firmware/
  comun/protocolo/   protocolo.h: formato ESP-NOW y constantes, compartido por ambos firmwares
  base-radar/        proyecto PlatformIO del XIAO ESP32-C6 (base)
  colgante/          proyecto PlatformIO del ESP32-C3 Super Mini (colgante)
web/
  firebase.json, .firebaserc.example, database.rules.json
  panel/             panel web (HTML/JS) publicado con Firebase Hosting
analisis/            scripts de Python para procesamiento de señal (filtros, FFT)
pruebas/             datos crudos y resultados de mediciones
docs/                documentación técnica
```

Cada carpeta tiene su propio README.

---

## Cómo compilar el firmware

### 1. Instalar PlatformIO

Opción recomendada: Visual Studio Code con la extensión **PlatformIO IDE** (al abrir el repositorio, VS Code la sugiere por `.vscode/extensions.json`). La extensión instala PlatformIO Core.

Opción solo terminal: con Python 3 instalado,

```
pip install platformio
```

El comando `pio` queda disponible. Si no aparece en el PATH, la extensión de VS Code lo deja en `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`.

### 2. Plataforma pioarduino

Ambos proyectos usan pioarduino (arduino-esp32 v3) en lugar de la plataforma oficial, porque la oficial no soporta bien el ESP32-C6. La versión está fija en cada `platformio.ini` (55.03.37, arduino-esp32 3.3.7) y se descarga sola al compilar por primera vez.

Al compilar la primera vez, pioarduino instala en el entorno Python de PlatformIO su propia versión del núcleo (`pioarduino-core`) y otras dependencias. En Windows esa instalación puede fallar si `pio.exe` está en uso, y deja PlatformIO sin módulos (por ejemplo, `No module named 'urllib3'`). Si pasa, cerrar VS Code y ejecutar en PowerShell:

```
$pe = "$env:USERPROFILE\.platformio\penv"
& "$pe\Scripts\python.exe" -m pip install "https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip"
```

y volver a compilar. La primera compilación descarga el compilador y el framework (varios cientos de MB) y tarda varios minutos.

### 3. Crear los archivos de secretos

Cada firmware necesita un `include/secrets.h` que no se sube al repositorio (está en `.gitignore`). Se crea copiando la plantilla:

```
copy firmware\base-radar\include\secrets.example.h firmware\base-radar\include\secrets.h
copy firmware\colgante\include\secrets.example.h  firmware\colgante\include\secrets.h
```

y llenando los valores:

- Base: WiFi, URL y API key de Firebase, correo y clave de la cuenta del dispositivo, id de la base, token y chat_id del bot de Telegram (pasos para crear el bot con @BotFather en `firmware/base-radar/README.md`).
- Colgante: MAC de la base e id del colgante. El colgante no lleva credenciales de WiFi ni de la nube.

Nunca poner credenciales reales en `secrets.example.h`.

### 4. Compilar y cargar

Desde la raíz del repositorio:

```
pio run -d firmware/base-radar
pio run -d firmware/colgante

pio run -d firmware/colgante -t upload
pio device monitor -d firmware/colgante
```

En VS Code: abrir la carpeta del proyecto (`firmware/base-radar` o `firmware/colgante`) y usar los botones de compilar y cargar de la barra de PlatformIO.

### Panel web y Firebase

Panel del cuidador: https://monitor-respiracion.web.app (descripción en `docs/panel.md`).
Instrucciones de Firebase, simulador y publicación en `web/README.md`.

---

## Avance — Audias (radar y lógica de detección)

### 9 agosto 2026
- Definición final del proyecto y documento de propuesta completado

### 12 octubre 2026
- Noche completa de sueño con el radar y decisión del detector para la base (`docs/radar.md`, sección 13)
- Revisión del firmware del colgante y fusión a `main`: espera al monitor USB solo en arranque en frío, regla de duplicados con `FLAG_ARRANQUE_FRIO` corregida en `docs/protocolo.md` y llevada a `firmware/comun/protocolo/duplicados.h`, con 2 casos de prueba nuevos
- Desde esta fecha llevo solo el desarrollo y la integración
- Preparación de la primera prueba del colgante en hardware: receptor ESP-NOW de prueba para un ESP32 clásico (`firmware/pruebas/receptor_espnow`), HEARTBEAT al encender, secuencia inicial aleatoria, intervalo de heartbeat corto para pruebas, y guía de conexión y lista de pruebas en `firmware/colgante/pruebas_fisicas.md`

### 27 octubre 2026
- Nube de la base, parte 2 (v0.6.0): cola offline de 1 h con eventos guardados en NVS, eventos BATERIA_BAJA (con histéresis), COLGANTE_AUSENTE, RADAR_SIN_DATOS y BASE_DESCONECTADA una vez por episodio, avisos por Telegram con el botón primero y límite de frecuencia, watchdog de tareas y aviso de reinicio inesperado, particiones sin OTA. 63 casos de prueba en PC. Falta probarlo en hardware (`docs/fallos.md`, "Qué está cubierto")

- Colgante armado con batería: R3 del TP4056 cambiada por 4.7 kΩ (~237 mA), batería en B+/B−, `OUT+` al pin 5V del C3 (sin conectar nunca el USB del C3 con la batería), divisor 100 kΩ / 100 kΩ sin capacitor en GPIO1. Firmware con la medición real de batería (atenuación de 12 dB, rango 0-2500 mV según la hoja de datos del ESP32-C3), 43 casos de prueba en PC. Consumo en deep sleep sin medir

### 28 octubre 2026
- Panel web del cuidador (`web/panel/`, `docs/panel.md`), publicado en Firebase Hosting: inicio de sesión, base en línea / desconectada, colgante con batería y señal, radar con anillo de respiración que sigue `resp_rpm` (marcada como estimación no validada), cinta de 6 h con los eventos encima, gráficas de 15 min / 1 h / 6 h, eventos con "Atendido" y modo emergencia (franja, título y sonido) con un BOTON sin atender. Índice de `atendido` en las reglas y 2 casos nuevos en la verificación (57/57)

---

## Avance — Abraham (colgante)

### 8 octubre 2026
- Botón de emergencia en GPIO3: pull-up interno, antirrebote, causa del despertar y espera limitada de liberación (`b5bb5f9`)

### 9 octubre 2026
- Energía: deep sleep con wake por botón y por timer de 15 min, recuperación de botón atascado y buzzer de confirmación/error (`8fdf77e`)
- ESP-NOW: envío unicast a la base, ACK de aplicación, reintentos con la misma secuencia, barrido de canales 1-13, canal y secuencia en RTC (`afa76e9`)
- Batería: modo simulado (3900 mV fijos) y lectura ADC con divisor preparada para el hardware (`44054d4`)
- Flujo principal integrado: ALERTA por botón, HEARTBEAT por timer, sin transmisión en arranque en frío ni en recuperación del botón (`5d15ab4`)
- Pruebas automatizadas en PC (`firmware/colgante/pruebas_host/`): 35 casos y 538 verificaciones aprobados en Windows y Linux; decisión de despertar aislada en `src/despertar.h`. El firmware compila con PlatformIO (`a4102da`)
- Documentación del colgante completada: configuración, conexiones propuestas, dependencias con la base y lista de pruebas físicas (`firmware/colgante/pruebas_fisicas.md`)
- Nada probado todavía en hardware: falta la batería, armar el circuito, cambiar R3 del TP4056, carcasa y cordón. Para probar el enlace se necesita `espnow_rx` de la base (ver "Integración con la base" en `firmware/colgante/README.md`)

### Entrega (registrada el 12 octubre 2026)
- Entregó el firmware completo del colgante (botón, deep sleep, ESP-NOW con ACK y barrido de canales, batería simulada) y las pruebas en PC (`firmware/colgante/pruebas_host/`, 35 casos y 538 verificaciones), escritos y probados sin hardware. Fusionado a `main` el 12 de octubre tras la revisión de Audias

---

## Avance — Marcos (base y notificación al cuidador)

### (fecha)
-

---

## Avance — Brenner

### (fecha)
-

---

## Notas de compras / cotización

| Componente | Tienda | Precio | Tiempo de entrega | Estado |
|---|---|---|---|---|
| MR60BHA2 (radar + ESP32-C6 integrado) | Guatemala Digital | Q322.40 (incluye envío) — Orden #1493658 | 21/09/2026 | Recibido y probado |
| ESP32-C3 Super Mini (x2, para colgante y pruebas) | Electrónica DIY | Q75 c/u (Q150 total) | Por confirmar | Comprado |
| Batería Li-ion 3.7V 240mAh (301645) | Tettsa | Q45 | Recibida | Recibida |
| Módulo de carga TP4056 (con protección, USB-C) | Electrónica DIY | Q12.50 | Por confirmar | Comprado |
| Módulo de buzzer activo en bajo con PNP S8550 (azul), para el colgante | Electrónica DIY | Q12 | Por confirmar | Comprado |
| Pulsador 12x12mm (tapa redonda, x2) | Electrónica DIY | Q2 c/u (Q4 total) | Por confirmar | Comprado |
| Pasta flux Miyako W-3, 50g | Tettsa | Q20 | Por confirmar | Pedido, sin llegar |

Otros materiales en uso:

- **Buzzer de la base**: módulo de buzzer activo sin transistor (negro, tipo KY-012), en D10 del
  XIAO ESP32-C6. Suena con el pin en alto y toma la corriente directo del GPIO.
- **Resistencias del colgante**: dos de 100 kΩ para el divisor de batería (GPIO1) y una THT de
  4.7 kΩ en lugar de R3 del TP4056 (corriente de carga ~237 mA). Usadas el 27 oct.
- **Multímetro**: mide voltaje bien; el fusible de la escala de mA probablemente está quemado, así
  que por ahora no sirve para medir corriente (consumo del colgante, corriente del buzzer, corriente
  de carga del TP4056).

**Subtotal Guatemala Digital (radar, orden #1493658):** **Q322.40** (incluye envío)

**Subtotal Electrónica DIY (colgante, pedido #35862):** componentes Q178.50 + envío Q30.00 = **Q208.50**

**Subtotal Tettsa (batería + flux):** Q45 + Q20 = Q65.00 + envío Q35.00 = **Q100.00**

**Total general del proyecto:** Q322.40 + Q208.50 + Q100.00 = **Q630.90**

**Nota técnica — ESP32-C3:** usa núcleo RISC-V, igual que el ESP32-C6 del radar (a diferencia del ESP32-WROOM-32 clásico, que usa Xtensa). Se eligió por su tamaño reducido, ideal para el colgante, y menor riesgo de fricción en ESP-NOW al estar más emparentado con el chip del radar. Pendiente confirmar en pruebas el pin correcto de wake-up desde deep sleep para el botón.

**Nota técnica — Batería:** la batería LiPo CNHL Ministar 450mAh originalmente elegida se agotó en Electrónica DIY. Se reemplazó por una Li-ion 3.7V 240mAh (menor capacidad, pero más compacta). Esta batería viene con cables sueltos sin conector JST-PH 2.0, así que se conecta soldando los cables directo a los pads BAT+ y BAT- de la placa TP4056 (respetando polaridad), en vez de usar un conector intermedio. Para eso se compró pasta flux, que facilita la adherencia de la soldadura.

**Nota técnica — Corriente de carga del TP4056:** el módulo (HW-373 V1.2.1) venía configurado para cargar a 1 A (R3 de 1.202 kΩ), demasiado para una celda de 240 mAh. El 27 oct se quitó R3; un pad quedó dañado, así que la THT de 4.7 kΩ va soldada directo de la patita 2 (PROG) del TP4056 al pad OUT−. Cargando, V(PROG) = 0.918 V, que con la fórmula de la hoja de datos (I = V_PROG / R_PROG × 1200) da unos 237 mA, alrededor de 1C para esta batería. Por confirmar con la hoja de datos de la celda que acepta esa corriente de carga; si pide menos, una resistencia mayor baja la corriente (por ejemplo, 10 kΩ da unos 120 mA). Detalle en `firmware/colgante/README.md`.
