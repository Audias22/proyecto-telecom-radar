# Sistema de monitoreo de respiración y auxilio para adultos mayores

Proyecto de Telecomunicaciones — Décimo Semestre
Universidad Mariano Gálvez, Guatemala
Integrantes: Audias, Abraham, Marcos
Periodo: 9 de agosto – 24 de octubre de 2026

---

## Estado general del proyecto

> Esta sección se actualiza en conjunto, cada sábado en el sprint, o cuando algo cambie de estado.

| Componente | Estado | Notas |
|---|---|---|
| Radar (MR60BHA2) | Por cotizar/comprar | Pendiente confirmar tiempo de entrega en Guatemala Digital |
| Colgante (botón + ESP32) | No iniciado | |
| Base + notificación | No iniciado | |
| Documento de propuesta | Listo | Ver `/docs/propuesta.docx` (o donde se suba) |
| Cronograma | Listo | 11 semanas, del 9 ago al 24 oct |

**Próximo sprint (sábado):** definir aquí antes de cada reunión qué se va a lograr ese día.

---

## Arquitectura del sistema

El sistema tiene dos vías de detección que trabajan juntas, más una unidad base que las conecta con el cuidador.

**1. Vía automática — Radar (60 GHz FMCW)**
Un módulo MR60BHA2 se coloca cerca de la cama y mide el micro-movimiento del pecho de la persona al respirar, sin cámara y sin contacto. Funciona en oscuridad total y a través de sábanas o ropa. El módulo trae integrado un ESP32-C6 que procesa la señal. Si detecta que la respiración se detiene, genera una alerta.

- Rango confiable de medición: 0.4 a 2 metros
- Cono de detección: aproximadamente 80° horizontal y 80° vertical
- Una persona a la vez, en reposo
- La respiración es el dato confiable; el latido es más frágil y se toma como dato secundario

**2. Vía manual — Colgante con botón de auxilio**
Un colgante con un solo botón grande (decisión firme: sin múltiples botones ni voz, por simplicidad para adultos mayores). Al presionarlo, un ESP32 en modo deep sleep despierta y envía una alerta por radiofrecuencia mediante ESP-NOW (2.4 GHz) a la unidad base. El deep sleep permite que la batería dure mucho más tiempo.

**3. Unidad base**
Recibe ambas señales (radar y colgante). Activa un zumbador y LED local para alertar en la casa, y notifica al teléfono del cuidador por WiFi. Es posible que el mismo ESP32-C6 que trae el módulo de radar pueda cumplir esta función, evitando comprar un segundo ESP32 — esto se confirma al tener el módulo en mano.

### Las tres señales del sistema

| Enlace | Banda / tipo | Función |
|---|---|---|
| Sensor (radar) | 60 GHz, FMCW (onda continua modulada en frecuencia) | Mide el movimiento del pecho |
| Alertador (colgante → base) | 2.4 GHz, digital por paquetes, ESP-NOW sobre 802.11 | Comunica la alerta manual, bajo consumo |
| Aviso al cuidador (base → teléfono) | WiFi 802.11 | Notifica al cuidador en su celular |

### Diferenciador

El sistema vigila sin cámara, lo que permite instalarlo en el dormitorio — el lugar donde más se necesita monitoreo y donde una cámara sería inaceptable por privacidad. El botón no compite con el radar, lo complementa: el radar detecta lo involuntario, el botón cubre lo voluntario.

### Límites conocidos (declarados desde ya, no ocultos)

- Detecta a una persona a la vez, en reposo, a corta distancia
- No es una cámara, no identifica objetos ni personas
- El radar no cubre 360°, solo su cono de detección
- No se recomienda usar varios radares juntos para cubrir 360° (interferencia mutua entre módulos de 60 GHz)

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

## Notas de compras / cotización

| Componente | Tienda | Precio | Tiempo de entrega | Estado |
|---|---|---|---|---|
| MR60BHA2 | Guatemala Digital | Por confirmar | ~2 semanas | Por pedir |
| Botón pulsador grande | | | | |
| Batería LiPo 3.7V | | | | |
| Cargador TP4056 | | | | |
| Buzzer | | | | |
