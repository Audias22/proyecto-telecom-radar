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

---

## Estado general del proyecto

> Esta sección se actualiza en conjunto, cada sábado en el sprint, o cuando algo cambie de estado.

| Componente | Estado | Notas |
|---|---|---|
| Radar (MR60BHA2) | Recibido y probado | Guatemala Digital, Q289, orden #1493658. Ya entrega datos con el ejemplo de Arduino IDE |
| Colgante (botón + ESP32) | Componentes comprados; batería y flux pedidos, sin llegar | ESP32-C3 x2, cargador TP4056, buzzer activo y pulsador 12x12mm x2 comprados. Batería Li-ion 240 mAh y pasta flux pedidas a Tettsa, sin llegar. Pendiente quitar R3 del TP4056 y poner una THT de 4.7 kΩ (ver nota técnica de la batería). Falta diseñar/imprimir carcasa y cordón. Estructura del firmware y protocolo definidos |
| Base + notificación | Estructura y especificación definidas | Sin código funcional. Usa el XIAO ESP32-C6 del kit |
| Firebase + panel web | Estructura y especificación definidas | Sin código funcional. Modelo de datos y reglas definidos |
| Documento de propuesta | Listo | Ver `/docs/propuesta.docx` (o donde se suba) |
| Cronograma | Listo | 11 semanas, del 9 ago al 24 oct |

**Próximo sprint (sábado):** definir aquí antes de cada reunión qué se va a lograr ese día.

---

## Arquitectura del sistema

El sistema tiene dos vías de detección que trabajan juntas, más una unidad base que las conecta con el cuidador. Detalle técnico y diagrama en `docs/arquitectura.md`.

**1. Vía automática — Radar (60 GHz FMCW)**
Un módulo MR60BHA2 se coloca cerca de la cama y mide el micro-movimiento del pecho de la persona al respirar, sin cámara y sin contacto. Funciona en oscuridad total y a través de sábanas o ropa. El módulo trae integrado un XIAO ESP32-C6 que procesa la señal. Si detecta que la respiración se detiene, genera una alerta.

- Rango confiable de medición: 0.4 a 2 metros
- Cono de detección: aproximadamente 80° horizontal y 80° vertical
- Una persona a la vez, en reposo
- La respiración es el dato confiable; el latido es más frágil y se toma como dato secundario

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
| Sensor (radar) | 60 GHz, FMCW (onda continua modulada en frecuencia) | Mide el movimiento del pecho |
| Alertador (colgante -> base) | 2.4 GHz, digital por paquetes, ESP-NOW sobre 802.11 | Comunica la alerta manual, bajo consumo |
| Base -> nube | WiFi 802.11 + HTTPS (API REST de Firebase) | Lecturas, estado y eventos |
| Aviso al cuidador (base -> teléfono) | WiFi 802.11 + HTTPS (API de bots de Telegram) | Notifica al cuidador en su celular |

### Diferenciador

El sistema vigila sin cámara, lo que permite instalarlo en el dormitorio — el lugar donde más se necesita monitoreo y donde una cámara sería inaceptable por privacidad. El botón no compite con el radar, lo complementa: el radar detecta lo involuntario, el botón cubre lo voluntario.

### Límites conocidos (declarados desde ya, no ocultos)

- Detecta a una persona a la vez, en reposo, a corta distancia
- No es una cámara, no identifica objetos ni personas
- El radar no cubre 360°, solo su cono de detección
- No se recomienda usar varios radares juntos para cubrir 360° (interferencia mutua entre módulos de 60 GHz)
- Si la base se apaga, el aviso por Telegram no sale en ese momento (no hay servidor propio); el panel lo muestra y el colgante avisa con el patrón de error

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

- Base: WiFi, URL y API key de Firebase, correo y clave de la cuenta del dispositivo, id de la base, token y chat_id del bot de Telegram.
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

Instrucciones en `web/README.md`.

---

## Avance — Audias (radar y lógica de detección)

### 9 agosto 2026
- Definición final del proyecto y documento de propuesta completado

---

## Avance — Abraham (colgante)

### (fecha)
-

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
| Batería Li-ion 3.7V 240mAh (301645) | Tettsa | Q45 | Por confirmar | Pedido, sin llegar |
| Módulo de carga TP4056 (con protección, USB-C) | Electrónica DIY | Q12.50 | Por confirmar | Comprado |
| Buzzer activo | Electrónica DIY | Q12 | Por confirmar | Comprado |
| Pulsador 12x12mm (tapa redonda, x2) | Electrónica DIY | Q2 c/u (Q4 total) | Por confirmar | Comprado |
| Pasta flux Miyako W-3, 50g | Tettsa | Q20 | Por confirmar | Pedido, sin llegar |

**Subtotal Electrónica DIY (colgante, pedido #35862):** Q220.50 + envío Q30.00 = **Q250.50**

**Subtotal Tettsa (batería + flux):** Q45 + Q20 = **Q65.00**

**Nota técnica — ESP32-C3:** usa núcleo RISC-V, igual que el ESP32-C6 del radar (a diferencia del ESP32-WROOM-32 clásico, que usa Xtensa). Se eligió por su tamaño reducido, ideal para el colgante, y menor riesgo de fricción en ESP-NOW al estar más emparentado con el chip del radar. Pendiente confirmar en pruebas el pin correcto de wake-up desde deep sleep para el botón.

**Nota técnica — Batería:** la batería LiPo CNHL Ministar 450mAh originalmente elegida se agotó en Electrónica DIY. Se reemplazó por una Li-ion 3.7V 240mAh (menor capacidad, pero más compacta). Esta batería viene con cables sueltos sin conector JST-PH 2.0, así que se conecta soldando los cables directo a los pads BAT+ y BAT- de la placa TP4056 (respetando polaridad), en vez de usar un conector intermedio. Para eso se compró pasta flux, que facilita la adherencia de la soldadura.

**Nota técnica — Corriente de carga del TP4056:** el módulo viene configurado para cargar a 1 A, demasiado para una celda de 240 mAh. La corriente la fija la resistencia de programación (R3 en este módulo). Pendiente quitar R3 y poner una resistencia THT de 4.7 kΩ. Según la fórmula de la hoja de datos del TP4056 (I = 1200 V / R_PROG), 4.7 kΩ da unos 255 mA, alrededor de 1C para esta batería. Por confirmar con la hoja de datos de la celda que acepta esa corriente de carga; si pide menos, una resistencia mayor baja la corriente (por ejemplo, 10 kΩ da unos 120 mA).
