/* ---------------------------------------------------------------------
 * Grado Ingeniería Informática. Cuarto curso. Internet de las Cosas, IC
 * Grupo 41
 * Autores:
 *      - Wail Ben El Hassane Boudhar
 *      - Mohamed O. Haroun Zarkik
 * NODO MAESTRO - Inicio con Máximo Alcance, Optimización Progresiva
 * ---------------------------------------------------------------------
 */

#include <SPI.h>
#include <LoRa.h>
#include <Arduino_PMIC.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ------------ CONFIGURACIÓN -------------
#define TX_LAPSE_MS 5000
#define OLED_DISPLAY_TIME 2000
#define ADJUSTMENT_SAMPLES 5  // Más muestras para estabilidad
#define CONFIG_WAIT_TIME 4000

#define MSG_TYPE_DATA 0x01
#define MSG_TYPE_ECHO 0x02
#define MSG_TYPE_CONFIG 0x03
#define MSG_TYPE_CONFIG_ACK 0x04

const uint8_t localAddress = 0xAA;
uint8_t destination = 0xBB;

volatile bool txDoneFlag = true;
volatile bool transmitting = false;

// ------------ UMBRALES DE CALIDAD (Más conservadores) -------------
#define RSSI_EXCELLENT -50   // Solo optimizar con señal muy fuerte
#define RSSI_GOOD -70
#define RSSI_FAIR -90
#define RSSI_POOR -110

#define SNR_EXCELLENT 12.0
#define SNR_GOOD 8.0
#define SNR_FAIR 3.0
#define SNR_POOR -3.0

// ------------ PARÁMETROS DINÁMICOS -------------
// INICIAR CON CONFIGURACIÓN DE MÁXIMO ALCANCE
uint8_t currentSF = 12;        // SF máximo para largo alcance
const uint8_t minSF = 7;
const uint8_t maxSF = 12;

uint8_t currentBW = 7;         // 125kHz para mejor sensibilidad
const uint8_t minBW = 6;       // 62.5kHz
const uint8_t maxBW = 9;       // 500kHz

uint8_t currentCR = 8;         // CR 4/8 máxima corrección de errores
const uint8_t minCR = 5;
const uint8_t maxCR = 8;

uint8_t currentPower = 20;     // Potencia máxima
const uint8_t minPower = 2;
const uint8_t maxPower = 20;

// Configuración pendiente
uint8_t pendingSF = 0;
uint8_t pendingBW = 0;
uint8_t pendingCR = 0;
uint8_t pendingPower = 0;
bool configPending = false;
uint32_t configSentTime = 0;

double bandwidth_kHz[10] = {
  7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3,
  41.7E3, 62.5E3, 125E3, 250E3, 500E3
};

// Métricas acumuladas
struct SignalMetrics {
  int rssi_sum;
  float snr_sum;
  uint8_t samples;
} slaveMetrics = {0, 0.0, 0};

uint32_t txStartTime = 0;
uint32_t lastSuccessfulRx = 0;
uint16_t successfulPackets = 0;
uint16_t failedPackets = 0;
uint8_t consecutiveFails = 0;
bool linkEstablished = false;

enum State {
  STATE_NORMAL,
  STATE_SENDING_CONFIG,
  STATE_WAITING_CONFIG_ACK
};
State currentState = STATE_NORMAL;

// ------------ OLED -------------
void oledMsg(const String &line1, const String &line2 = "", const String &line3 = "", const String &line4 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println(line1);
  if (line2 != "") display.println(line2);
  if (line3 != "") display.println(line3);
  if (line4 != "") display.println(line4);
  display.display();
}

// ------------ APLICAR CONFIGURACIÓN -------------
void applyConfiguration() {
  LoRa.idle();
  delay(50);

  LoRa.setSignalBandwidth(long(bandwidth_kHz[currentBW]));
  LoRa.setSpreadingFactor(currentSF);
  LoRa.setCodingRate4(currentCR);
  LoRa.setTxPower(currentPower, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setPreambleLength(currentSF >= 11 ? 16 : (currentSF >= 9 ? 12 : 8));
  LoRa.enableCrc();
  LoRa.setSyncWord(0x12);

  delay(50);
  LoRa.receive();

  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   CONFIGURACIÓN APLICADA               ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("  SF: " + String(currentSF));
  Serial.println("  BW: " + String(bandwidth_kHz[currentBW]/1000.0) + " kHz");
  Serial.println("  CR: 4/" + String(currentCR));
  Serial.println("  Power: " + String(currentPower) + " dBm");

  // Cálculo aproximado de alcance
  float rangeKm = 0.5;
  if(currentSF >= 12 && currentPower >= 17) rangeKm = 15.0;
  else if(currentSF >= 11) rangeKm = 8.0;
  else if(currentSF >= 10) rangeKm = 4.0;
  else if(currentSF >= 9) rangeKm = 2.0;
  else if(currentSF >= 8) rangeKm = 1.0;

  Serial.println("  Alcance estimado: ~" + String(rangeKm) + " km");
  Serial.println("========================================\n");

  oledMsg("Config Aplicada",
          "SF:" + String(currentSF) + " CR:4/" + String(currentCR),
          "BW:" + String(bandwidth_kHz[currentBW]/1000.0) + "k P:" + String(currentPower),
          "~" + String(rangeKm) + "km alcance");
  delay(OLED_DISPLAY_TIME);
}

// ------------ ENVIAR CONFIGURACIÓN AL ESCLAVO -------------
void sendConfigToSlave(uint8_t sf, uint8_t bw, uint8_t cr, uint8_t pwr) {
  Serial.println("→ Enviando nueva configuración al esclavo...");
  Serial.println("  SF:" + String(sf) + " BW:" + String(bandwidth_kHz[bw]/1000.0) +
                 "kHz CR:4/" + String(cr) + " Pwr:" + String(pwr) + "dBm");

  uint8_t payload[5];
  payload[0] = MSG_TYPE_CONFIG;
  payload[1] = sf;
  payload[2] = bw;
  payload[3] = cr;
  payload[4] = pwr;

  transmitting = true;
  txDoneFlag = false;

  sendMessage(payload, 5, 0xFFFF);

  pendingSF = sf;
  pendingBW = bw;
  pendingCR = cr;
  pendingPower = pwr;
  configPending = true;
  configSentTime = millis();
  currentState = STATE_WAITING_CONFIG_ACK;

  oledMsg("Enviando Config", "SF:" + String(sf) + " Pwr:" + String(pwr), "Esperando ACK...");
}

// ------------ EVALUAR CALIDAD -------------
String evaluateSignalQuality(int rssi, float snr) {
  if(rssi > RSSI_EXCELLENT && snr > SNR_EXCELLENT) return "EXCELENTE";
  if(rssi > RSSI_GOOD && snr > SNR_GOOD) return "BUENA";
  if(rssi > RSSI_FAIR && snr > SNR_FAIR) return "ACEPTABLE";
  if(rssi > RSSI_POOR && snr > SNR_POOR) return "POBRE";
  return "MUY POBRE";
}

// ------------ AJUSTE DINÁMICO (Conservador) -------------
void adjustConfiguration(int avgRSSI, float avgSNR) {
  uint8_t newSF = currentSF;
  uint8_t newBW = currentBW;
  uint8_t newCR = currentCR;
  uint8_t newPower = currentPower;
  bool changed = false;
  String action = "";

  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   ANÁLISIS DE CALIDAD DE SEÑAL         ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("RSSI promedio: " + String(avgRSSI) + " dBm");
  Serial.println("SNR promedio: " + String(avgSNR) + " dB");
  Serial.println("Calidad: " + evaluateSignalQuality(avgRSSI, avgSNR));
  Serial.println("Enlace establecido: " + String(linkEstablished ? "SÍ" : "NO"));
  Serial.println("----------------------------------------");

  // Solo optimizar si el enlace está muy estable
  if(avgRSSI > RSSI_EXCELLENT && avgSNR > SNR_EXCELLENT && linkEstablished) {
    Serial.println("→ Señal excelente y enlace estable. Optimización LEVE:");

    // Solo reducir potencia levemente
    if(newPower > minPower + 5) {
      newPower -= 2;
      action += "Power↓ ";
      changed = true;
    }
  }

  else if(avgRSSI > RSSI_GOOD && avgSNR > SNR_GOOD) {
    Serial.println("→ Señal buena. MANTENIENDO configuración.");
    action = "Sin cambios (óptimo)";
  }

  else if(avgRSSI > RSSI_FAIR && avgSNR > SNR_FAIR) {
    Serial.println("→ Señal aceptable. MANTENIENDO configuración.");
    action = "Sin cambios";
  }

  else if(avgRSSI > RSSI_POOR && avgSNR > SNR_POOR) {
    Serial.println("→ Señal pobre. MEJORANDO:");

    if(newPower < maxPower - 2) {
      newPower += 3;
      action += "Power↑ ";
      changed = true;
    }
    if(newSF < maxSF) {
      newSF++;
      action += "SF↑ ";
      changed = true;
    }
    if(newCR < maxCR) {
      newCR++;
      action += "CR↑ ";
      changed = true;
    }
  }

  else {
    Serial.println("→ Señal muy pobre. MEJORA MÁXIMA:");

    newPower = maxPower;
    newSF = maxSF;
    newBW = 7;  // 125kHz
    newCR = maxCR;
    action = "Config MÁXIMA";
    changed = true;
  }

  Serial.println("Acciones: " + action);
  Serial.println("========================================\n");

  if(changed) {
    sendConfigToSlave(newSF, newBW, newCR, newPower);
  }
}

// ------------ SETUP -------------
void setup() {
  Serial.begin(115200);
  while (!Serial);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
    Serial.println("Error OLED");
    while(1);
  }
  oledMsg("Inicializando...", "Modo Largo Alcance");
  delay(1500);

  if (!LoRa.begin(868E6)) {
    Serial.println("Error LoRa");
    oledMsg("Error LoRa!");
    while(1);
  }

  // Se empieza con MÁXIMO ALCANCE
  applyConfiguration();

  LoRa.onReceive(onReceive);
  LoRa.onTxDone(TxFinished);
  LoRa.receive();

  oledMsg("Maestro Activo", "LARGO ALCANCE", "SF:12 BW:125k", "Pwr:20dBm ~15km");
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   MAESTRO - Modo Largo Alcance        ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("Iniciado con configuración máxima");
  Serial.println("SF:12 BW:125kHz CR:4/8 Power:20dBm");
  Serial.println("Alcance: hasta 15km línea de vista\n");
  delay(OLED_DISPLAY_TIME);
}

// ------------ LOOP -------------
void loop() {
  static uint32_t lastSendTime_ms = 0;
  static uint16_t msgCount = 0;

  // Timeout de ACK de configuración
  if(currentState == STATE_WAITING_CONFIG_ACK &&
     (millis() - configSentTime) > CONFIG_WAIT_TIME) {
    Serial.println("Timeout ACK config. Aplicando...");
    currentSF = pendingSF;
    currentBW = pendingBW;
    currentCR = pendingCR;
    currentPower = pendingPower;
    applyConfiguration();
    currentState = STATE_NORMAL;
    configPending = false;
  }

  // Timeout de comunicación
  if(lastSuccessfulRx > 0 && (millis() - lastSuccessfulRx) > 30000) {
    consecutiveFails++;
    Serial.println("SIN COMUNICACIÓN > 30s. Fallos: " + String(consecutiveFails));

    if(consecutiveFails >= 2) {
      Serial.println("Restaurando configuración MÁXIMA...");
      sendConfigToSlave(maxSF, 7, maxCR, maxPower);
      consecutiveFails = 0;
      linkEstablished = false;
      lastSuccessfulRx = millis();
    }
  }

  // Enviar datos solo en estado normal
  if (currentState == STATE_NORMAL && !transmitting &&
      ((millis() - lastSendTime_ms) > TX_LAPSE_MS)) {

    String debugMsg = "M" + String(msgCount);

    uint8_t payload[50];
    uint8_t payloadLength = 0;

    payload[payloadLength++] = MSG_TYPE_DATA;

    for(int i = 0; i < debugMsg.length() && payloadLength < 48; i++) {
      payload[payloadLength++] = debugMsg.charAt(i);
    }

    transmitting = true;
    txDoneFlag = false;
    txStartTime = millis();

    sendMessage(payload, payloadLength, msgCount);

    oledMsg("TX #" + String(msgCount),
            debugMsg,
            "SF:" + String(currentSF) + " P:" + String(currentPower),
            linkEstablished ? "ENLACE OK" : "Buscando...");

    Serial.println("→ TX #" + String(msgCount) + " | " + debugMsg +
                   " | SF:" + String(currentSF) + " P:" + String(currentPower) + "dBm");

    msgCount++;
    lastSendTime_ms = millis();
  }

  if (transmitting && txDoneFlag) {
    transmitting = false;
    LoRa.receive();
  }
}

// ------------ ENVÍO -------------
void sendMessage(uint8_t* payload, uint8_t length, uint16_t id) {
  while(!LoRa.beginPacket()) delay(5);

  LoRa.write(destination);
  LoRa.write(localAddress);
  LoRa.write((uint8_t)(id >> 8));
  LoRa.write((uint8_t)(id & 0xFF));
  LoRa.write(length);
  LoRa.write(payload, length);

  LoRa.endPacket(true);
}

// ------------ RECEIVE -------------
void onReceive(int size) {
  if (transmitting && !txDoneFlag) txDoneFlag = true;
  if (size == 0) return;

  uint32_t rxTime = millis();

  uint8_t recipient = LoRa.read();
  uint8_t sender = LoRa.read();
  uint16_t msgId = (LoRa.read() << 8) | LoRa.read();
  uint8_t length = LoRa.read();

  uint8_t buffer[50];
  for (uint8_t i=0; i<length && i<50; i++) buffer[i]=LoRa.read();

  uint8_t msgType = buffer[0];

  // ACK de configuración
  if(msgType == MSG_TYPE_CONFIG_ACK && currentState == STATE_WAITING_CONFIG_ACK) {
    Serial.println("✓ ACK Config recibido. Aplicando...");

    currentSF = pendingSF;
    currentBW = pendingBW;
    currentCR = pendingCR;
    currentPower = pendingPower;
    applyConfiguration();

    currentState = STATE_NORMAL;
    configPending = false;
    consecutiveFails = 0;
    lastSuccessfulRx = rxTime;

    return;
  }

  // Eco con métricas
  if(msgType == MSG_TYPE_ECHO) {
    uint32_t roundTripTime = rxTime - txStartTime;

    int8_t slaveRSSI = (int8_t)buffer[1];
    int8_t slaveSNR = (int8_t)buffer[2];

    String echoMsg = "";
    if(length > 3) {
      for(uint8_t i=3; i<length; i++) {
        echoMsg += (char)buffer[i];
      }
    }

    int localRSSI = LoRa.packetRssi();
    float localSNR = LoRa.packetSnr();

    slaveMetrics.rssi_sum += slaveRSSI;
    slaveMetrics.snr_sum += slaveSNR;
    slaveMetrics.samples++;

    consecutiveFails = 0;
    lastSuccessfulRx = rxTime;
    successfulPackets++;

    // Marcar enlace como establecido tras primer eco exitoso
    if(!linkEstablished) {
      linkEstablished = true;
      Serial.println("★★★ ENLACE ESTABLECIDO ★★★");
    }

    oledMsg("✓ ECO #" + String(msgId),
            echoMsg,
            "RSSI:" + String(slaveRSSI) + " SNR:" + String(slaveSNR),
            "RTT:" + String(roundTripTime) + "ms");

    Serial.println("✓ ECO | " + echoMsg + " | Esclavo RSSI:" + String(slaveRSSI) +
                   " SNR:" + String(slaveSNR) + " | RTT:" + String(roundTripTime) +
                   "ms | " + evaluateSignalQuality(slaveRSSI, slaveSNR));
    Serial.println("  Local RSSI:" + String(localRSSI) + " SNR:" + String(localSNR));

    if(slaveMetrics.samples >= ADJUSTMENT_SAMPLES) {
      int avgRSSI = slaveMetrics.rssi_sum / slaveMetrics.samples;
      float avgSNR = slaveMetrics.snr_sum / slaveMetrics.samples;

      Serial.println("  → " + String(slaveMetrics.samples) + " muestras acumuladas. Evaluando...");
      adjustConfiguration(avgRSSI, avgSNR);

      slaveMetrics.rssi_sum = 0;
      slaveMetrics.snr_sum = 0;
      slaveMetrics.samples = 0;
    } else {
      Serial.println("  Muestra " + String(slaveMetrics.samples) + "/" + String(ADJUSTMENT_SAMPLES));
    }

    delay(1000);
  }
}

void TxFinished() {
  txDoneFlag = true;
}
