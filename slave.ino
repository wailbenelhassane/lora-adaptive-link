
/* ---------------------------------------------------------------------
 * Grado Ingeniería Informática. Cuarto curso. Internet de las Cosas, IC
 * Grupo 41
 * Autores:
 *      - Wail Ben El Hassane Boudhar
 *      - Mohamed O. Haroun Zarkik
 * NODO ESCLAVO - Inicio con Máximo Alcance
 * ---------------------------------------------------------------------
*/

#include <SPI.h>
#include <LoRa.h>
#include <Arduino_PMIC.h>

#define MSG_TYPE_DATA 0x01
#define MSG_TYPE_ECHO 0x02
#define MSG_TYPE_CONFIG 0x03
#define MSG_TYPE_CONFIG_ACK 0x04

const uint8_t localAddress = 0xBB;
uint8_t destination = 0xAA;

volatile bool txDoneFlag = true;
volatile bool transmitting = false;

// ------------ PARÁMETROS DINÁMICOS -------------
//  INICIAR CON CONFIGURACIÓN DE MÁXIMO ALCANCE
uint8_t currentSF = 12;        // SF máximo
uint8_t currentBW = 7;         // 125kHz
uint8_t currentCR = 8;         // CR 4/8 máximo
uint8_t currentPower = 20;     // Potencia máxima

// Nueva configuración pendiente
uint8_t newSF = 0;
uint8_t newBW = 0;
uint8_t newCR = 0;
uint8_t newPower = 0;
bool configReceived = false;

double bandwidth_kHz[10] = {
  7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3,
  41.7E3, 62.5E3, 125E3, 250E3, 500E3
};

// Métricas
uint32_t lastRxTime = 0;
uint32_t txStartTime = 0;
uint16_t packetsReceived = 0;
uint16_t echosSent = 0;

// Buffer para mensaje recibido
String lastReceivedMsg = "";
int lastRSSI = 0;
float lastSNR = 0;

enum SlaveState {
  STATE_NORMAL,
  STATE_SENDING_ACK,
  STATE_APPLYING_CONFIG
};
SlaveState currentState = STATE_NORMAL;

// ---------- APLICAR CONFIGURACIÓN ----------
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
  Serial.println("║   CONFIGURACIÓN SINCRONIZADA          ║");
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
}

// ---------- ENVIAR ACK DE CONFIGURACIÓN ----------
void sendConfigAck() {
  Serial.println("→ Enviando ACK de configuración...");

  uint8_t payload[1];
  payload[0] = MSG_TYPE_CONFIG_ACK;

  transmitting = true;
  txDoneFlag = false;

  sendMessage(payload, 1, 0xFFFE);

  currentState = STATE_APPLYING_CONFIG;
}

// ---------- EVALUAR CALIDAD ----------
String evaluateSignalQuality(int rssi, float snr) {
  if(rssi > -50 && snr > 12) return "EXCELENTE";
  if(rssi > -70 && snr > 8) return "BUENA";
  if(rssi > -90 && snr > 3) return "ACEPTABLE";
  if(rssi > -110 && snr > -3) return "POBRE";
  return "MUY POBRE";
}

void setup() {
  Serial.begin(115200);
  while(!Serial);

  if (!LoRa.begin(868E6)) {
    Serial.println("Error LoRa");
    while(1);
  }

  //Se empieza con MÁXIMO ALCANCE
  applyConfiguration();

  LoRa.onReceive(onReceive);
  LoRa.onTxDone(TxFinished);
  LoRa.receive();

  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   ESCLAVO - Modo Largo Alcance        ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("Iniciado con configuración máxima");
  Serial.println("SF:12 BW:125kHz CR:4/8 Power:20dBm");
  Serial.println("Alcance: hasta 15km línea de vista");
  Serial.println("Esperando mensajes del maestro...\n");
}


void loop() {
  // Aplicar configuración pendiente después de ACK
  if(currentState == STATE_APPLYING_CONFIG && !transmitting) {
    delay(500);

    Serial.println("→ Aplicando nueva configuración...");
    currentSF = newSF;
    currentBW = newBW;
    currentCR = newCR;
    currentPower = newPower;
    applyConfiguration();

    currentState = STATE_NORMAL;
    configReceived = false;
  }

  // Enviar eco si hay mensaje pendiente
  if (currentState == STATE_NORMAL && !transmitting && lastReceivedMsg.length() > 0) {

    uint8_t payload[50];
    uint8_t len = 0;

    payload[len++] = MSG_TYPE_ECHO;
    payload[len++] = (uint8_t)lastRSSI;
    payload[len++] = (uint8_t)lastSNR;

    for(int i = 0; i < lastReceivedMsg.length() && len < 48; i++) {
      payload[len++] = lastReceivedMsg.charAt(i);
    }

    transmitting = true;
    txDoneFlag = false;
    txStartTime = millis();

    sendMessage(payload, len, echosSent);

    Serial.println("→ ECO #" + String(echosSent) + " | " + lastReceivedMsg +
                   " | RSSI:" + String(lastRSSI) + " SNR:" + String(lastSNR));

    echosSent++;
    lastReceivedMsg = "";
  }

  if (transmitting && txDoneFlag) {
    uint32_t txDuration = millis() - txStartTime;
    Serial.println("  ✓ Enviado en " + String(txDuration) + " ms\n");
    transmitting = false;
    LoRa.receive();
  }
}


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


void onReceive(int size) {
  if (transmitting && !txDoneFlag) txDoneFlag = true;
  if (size==0) return;

  uint32_t rxTime = millis();

  uint8_t recipient = LoRa.read();
  uint8_t sender = LoRa.read();
  uint16_t msgId = (LoRa.read() << 8) | LoRa.read();
  uint8_t length = LoRa.read();

  uint8_t buffer[50];
  for (uint8_t i=0; i<length && i<50; i++) buffer[i]=LoRa.read();

  uint8_t msgType = buffer[0];

  // NUEVA CONFIGURACIÓN del maestro
  if(msgType == MSG_TYPE_CONFIG) {
    newSF = buffer[1];
    newBW = buffer[2];
    newCR = buffer[3];
    newPower = buffer[4];

    Serial.println("========================================");
    Serial.println("✓ NUEVA CONFIGURACIÓN RECIBIDA");
    Serial.println("  SF: " + String(newSF));
    Serial.println("  BW: " + String(bandwidth_kHz[newBW]/1000.0) + " kHz");
    Serial.println("  CR: 4/" + String(newCR));
    Serial.println("  Power: " + String(newPower) + " dBm");
    Serial.println("========================================");

    configReceived = true;
    sendConfigAck();

    return;
  }

  // DATOS normales del maestro
  if(msgType == MSG_TYPE_DATA) {
    String receivedMsg = "";
    if(length > 1) {
      for(uint8_t i=1; i<length; i++) {
        receivedMsg += (char)buffer[i];
      }
    }

    // Capturar métricas INMEDIATAMENTE
    lastRSSI = LoRa.packetRssi();
    lastSNR = LoRa.packetSnr();
    long freqError = LoRa.packetFrequencyError();

    lastReceivedMsg = receivedMsg;
    packetsReceived++;
    lastRxTime = rxTime;

    String quality = evaluateSignalQuality(lastRSSI, lastSNR);

    Serial.println("========================================");
    Serial.println("✓ RX MAESTRO #" + String(msgId) + " | " + receivedMsg);
    Serial.println("RSSI:" + String(lastRSSI) + "dBm SNR:" + String(lastSNR) +
                   "dB FErr:" + String(freqError) + "Hz");
    Serial.println("Calidad: " + quality);
    Serial.println("RX:" + String(packetsReceived) + " Echo:" + String(echosSent));
    Serial.println("========================================");

    // Mostrar estadísticas cada 10 paquetes
    if(packetsReceived % 10 == 0) {
      Serial.println("\n★ Estadísticas:");
      Serial.println("  Total recibidos: " + String(packetsReceived));
      Serial.println("  Total ecos: " + String(echosSent));
      Serial.println("  Config actual: SF:" + String(currentSF) +
                     " BW:" + String(bandwidth_kHz[currentBW]/1000.0) +
                     "kHz P:" + String(currentPower) + "dBm\n");
    }
  }
}


void TxFinished() {
  txDoneFlag = true;
}
