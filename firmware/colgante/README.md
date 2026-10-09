# firmware/colgante

Firmware del ESP32-C3 Super Mini del colgante.

Ciclo: deep sleep -> despierta por botón o por timer -> envía ALERTA o HEARTBEAT por ESP-NOW ->
espera ACK (reintentos y barrido de canales) -> beep -> deep sleep. Detalle en `docs/protocolo.md`.

## Módulos (`src/`)

| Módulo | Responsabilidad |
|---|---|
| `boton` | Botón de auxilio, antirrebote y causa del despertar (implementado) |
| `espnow_tx` | Envío, espera de ACK, reintentos, barrido, canal y secuencia en RTC |
| `energia` | Deep sleep, fuentes de despertar, beeps de confirmación y error |
| `bateria` | Voltaje de la batería por ADC |

`espnow_tx`, `energia` y `bateria` todavía tienen su implementación pendiente. `main.cpp` por ahora
solo imprime nombre y versión.

## Botón de auxilio

- Se conecta entre GPIO3 y GND y se configura como `INPUT_PULLUP`; por eso está activo en nivel
  bajo.
- Una pulsación o liberación debe permanecer estable durante `BOTON_ANTIRREBOTE_MS` (30 ms por
  defecto) para aceptarse.
- `despertoPorBoton()` distingue `ESP_SLEEP_WAKEUP_GPIO` del despertar periódico por temporizador
  y verifica que GPIO3 esté presente en el estado de wake.
- `presionado()` devuelve el estado estable, no un evento nuevo en cada llamada. En el ciclo del
  firmware se debe generar una sola alerta por despertar, aunque el botón continúe presionado.
- Antes de dormir, `esperarSoltar()` permite esperar una liberación estable. La espera está
  limitada por `BOTON_ESPERA_LIBERACION_MS` (5 s por defecto): devuelve `true` si se liberó y
  `false` si agotó el tiempo, sin quedar bloqueada.
- La función `energia::dormir()` deberá habilitar GPIO3 en nivel bajo con
  `esp_deep_sleep_enable_gpio_wakeup()` solamente si `esperarSoltar()` devolvió `true`, además del
  temporizador del heartbeat. Si devuelve `false`, habilitar wake en nivel bajo con el botón aún
  presionado causaría despertares inmediatos y alertas repetidas; energía deberá omitir esa fuente
  durante ese ciclo o posponer el deep sleep. Esa integración continúa pendiente.

Sin hardware no se han validado todavía el rebote real del pulsador, el despertar desde deep
sleep, la conservación del pull-up interno durante el sueño ni el comportamiento eléctrico ante
una pulsación prolongada. Si el pull-up interno no mantiene un nivel estable en deep sleep, habrá
que añadir una resistencia externa sin cambiar la lógica activa en bajo.

## Configuración

- `include/config.h`: versión y pines (propuestos, por confirmar).
- `include/secrets.h`: MAC de la base e id del colgante. Copiar de `include/secrets.example.h`.

## Hardware

- Botón en GPIO0-GPIO5 (únicos pines que despiertan al ESP32-C3 de deep sleep).
- Batería Li-ion 240 mAh con cargador TP4056.
- El módulo TP4056 comprado es la versión con protección (USB-C). Por confirmar con el módulo el
  voltaje de corte por descarga, para fijar `BATERIA_BAJA_MV` con margen sobre ese corte.
- La corriente de carga del módulo viene en 1 A; hay que cambiar R3 por 4.7 kΩ antes de conectar
  la batería de 240 mAh (ver nota técnica en el README principal).
- Por confirmar con el módulo: consumo real en deep sleep de la placa completa (LED de encendido,
  regulador, divisor de batería). Define la duración de la batería.
- Mientras el colgante duerme, el puerto USB desaparece. Para cargar firmware, mantener BOOT
  presionado al conectar si el puerto no aparece.
