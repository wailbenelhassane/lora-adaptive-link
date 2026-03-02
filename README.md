# LoRa Adaptive Master-Slave Link

Sistema de comunicacion LoRa entre dos nodos Arduino (`master` y `slave`) con ajuste dinamico de parametros de radio segun la calidad real del enlace (RSSI/SNR).

## Que hace este proyecto

- Envia mensajes periodicos desde un nodo maestro a un nodo esclavo.
- El esclavo responde con eco y metricas de enlace.
- El maestro analiza muestras de RSSI/SNR y decide si mantener, optimizar o reforzar la configuracion LoRa.
- Ambos nodos sincronizan cambios de configuracion mediante mensajes `CONFIG` y `CONFIG_ACK`.
- Incluye mecanismo de recuperacion: si se pierde comunicacion, vuelve a una configuracion de maximo alcance.

## Arquitectura

- `master.ino`: logica de control, ajuste dinamico y coordinacion de configuracion.
- `slave.ino`: recepcion de datos, medicion de calidad y respuesta tipo eco.

Modelo de intercambio:

1. Maestro envia `DATA`.
2. Esclavo recibe, mide RSSI/SNR y responde `ECHO`.
3. Maestro acumula muestras y evalua calidad.
4. Si procede, maestro envia `CONFIG`.
5. Esclavo confirma con `CONFIG_ACK` y ambos aplican la nueva configuracion.

## Parametros LoRa ajustables

- `SF` (Spreading Factor): 7 a 12
- `BW` (Bandwidth): indices de 62.5 kHz a 500 kHz (inicial 125 kHz)
- `CR` (Coding Rate): 4/5 a 4/8
- `TX Power`: 2 a 20 dBm

Configuracion inicial en ambos nodos: enfoque de largo alcance (SF12, BW125 kHz, CR 4/8, 20 dBm).

## Requisitos

Hardware:

- 2 placas compatibles con Arduino + LoRa (SX127x/868 MHz).
- Antenas adecuadas para 868 MHz.
- OLED I2C en el nodo maestro (opcional pero usado por el sketch actual).

Software (Arduino IDE):

- Libreria `LoRa`
- Libreria `Adafruit GFX`
- Libreria `Adafruit SSD1306` (solo maestro)
- Libreria `Arduino_PMIC` (segun la placa)

## Como usar

1. Conecta y configura las dos placas en Arduino IDE.
2. Carga `slave.ino` en el nodo esclavo.
3. Carga `master.ino` en el nodo maestro.
4. Abre el monitor serie de ambos nodos a `115200`.
5. Observa:
   - envio/eco de mensajes,
   - metricas RSSI/SNR,
   - decisiones de ajuste,
   - resincronizacion automatica ante degradacion del enlace.

## Estructura del repositorio

```text
.
|- master.ino
|- slave.ino
|- README.md
```


