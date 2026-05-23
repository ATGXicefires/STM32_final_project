/*
 * ESP32 UART bridge for STM32F407 USART1.
 *
 * Wiring:
 *   STM32 PA9  (USART1_TX) -> ESP32 RX2_PIN
 *   STM32 PA10 (USART1_RX) <- ESP32 TX2_PIN
 *   STM32 GND              <-> ESP32 GND
 *
 * Default ESP32 pins:
 *   RX2_PIN = GPIO16
 *   TX2_PIN = GPIO17
 */

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
static const char WIFI_SSID[] = "YOUR_WIFI_SSID";
static const char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
static const char PC_HOST[] = "172.20.10.2";
#endif

static const int RX2_PIN = 16;
static const int TX2_PIN = 17;
static const uint32_t UART_BAUD = 921600;
static const bool DEBUG_PWM_ENABLE = false;
static const int DEBUG_PWM_PIN = 4;
static const int DEBUG_PWM_DUTY = 10;
static const uint16_t PC_PORT = 5000;
static const uint16_t AUD_LISTEN_PORT = 5001;

static const char PCM_MAGIC[] = "PCM1";
static const char AUD_MAGIC[] = "AUD1";
static const size_t PCM_HEADER_REMAINING_BYTES = 20;
static const uint32_t PCM_PAYLOAD_CAPACITY = 16000;
static const size_t AUD_HEADER_BYTES = 24;
static const uint32_t AUD_SAMPLE_RATE = 16000;
static const uint32_t AUD_ACK_BYTES = 1024;
static const uint32_t AUD_UART_CHUNK_BYTES = 256;

enum RxMode {
  RX_LINE,
  RX_PCM_HEADER,
  RX_PCM_PAYLOAD,
  RX_PCM_DROP
};

enum AudTcpMode {
  AUD_TCP_IDLE,
  AUD_TCP_HEADER,
  AUD_TCP_PAYLOAD
};

static String stm32Line;
static RxMode rxMode = RX_LINE;
static uint8_t magicIndex = 0;
static uint8_t pcmHeader[PCM_HEADER_REMAINING_BYTES];
static uint32_t pcmHeaderIndex = 0;
static uint8_t pcmPayload[PCM_PAYLOAD_CAPACITY];
static uint32_t pcmPayloadIndex = 0;
static uint32_t pcmSampleRate = 0;
static uint32_t pcmSampleCount = 0;
static uint32_t pcmPayloadBytes = 0;
static uint32_t pcmSeq = 0;
static uint32_t pcmExpectedChecksum = 0;

static WiFiServer audServer(AUD_LISTEN_PORT);
static WiFiClient audClient;
static bool audServerStarted = false;
static AudTcpMode audTcpMode = AUD_TCP_IDLE;
static uint8_t audHeader[AUD_HEADER_BYTES];
static uint32_t audHeaderIndex = 0;
static uint32_t audSampleRate = 0;
static uint32_t audSampleCount = 0;
static uint32_t audPayloadBytes = 0;
static uint32_t audSeq = 0;
static uint32_t audChecksum = 0;
static uint32_t audPayloadForwarded = 0;
static uint32_t audNextAckAt = AUD_ACK_BYTES;

static uint32_t readLe32(const uint8_t *buffer) {
  return ((uint32_t)buffer[0]) |
         ((uint32_t)buffer[1] << 8) |
         ((uint32_t)buffer[2] << 16) |
         ((uint32_t)buffer[3] << 24);
}

static uint32_t checksumBytes(const uint8_t *buffer, uint32_t length) {
  uint32_t checksum = 0;

  for (uint32_t i = 0; i < length; i++) {
    checksum += buffer[i];
  }

  return checksum;
}

static bool wifiConfigured() {
  return strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0;
}

static void connectWifiIfNeeded() {
  if (!wifiConfigured() || (WiFi.status() == WL_CONNECTED)) {
    return;
  }

  Serial.print("ESP32: connecting WiFi ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while ((WiFi.status() != WL_CONNECTED) && ((millis() - start) < 10000UL)) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("ESP32: WiFi connected ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("ESP32: WiFi connect timeout, PCM/AUD TCP disabled");
  }
}

static bool writeAll(WiFiClient &client, const uint8_t *buffer, uint32_t length);
static bool writeAllUart(const uint8_t *buffer, uint32_t length);

static bool forwardPcmToPc() {
  if (!wifiConfigured()) {
    Serial.println("ESP32: WiFi not configured, skip TCP forward");
    return false;
  }

  connectWifiIfNeeded();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ESP32: WiFi offline, skip TCP forward");
    return false;
  }

  WiFiClient client;
  if (!client.connect(PC_HOST, PC_PORT)) {
    Serial.println("ESP32: TCP connect failed");
    return false;
  }

  uint8_t header[24];
  memcpy(header, PCM_MAGIC, 4);
  memcpy(header + 4, pcmHeader, PCM_HEADER_REMAINING_BYTES);
  if (!writeAll(client, header, sizeof(header)) ||
      !writeAll(client, pcmPayload, pcmPayloadBytes)) {
    Serial.println("ESP32: TCP write failed");
    client.stop();
    return false;
  }
  client.stop();

  Serial.print("ESP32: TCP forwarded bytes=");
  Serial.println(pcmPayloadBytes);
  return true;
}

static bool writeAll(WiFiClient &client, const uint8_t *buffer, uint32_t length) {
  uint32_t offset = 0;

  while (offset < length) {
    size_t written = client.write(&buffer[offset], (size_t)(length - offset));
    if (written == 0) {
      return false;
    }
    offset += (uint32_t)written;
  }

  return true;
}

static bool writeAllUart(const uint8_t *buffer, uint32_t length) {
  uint32_t offset = 0;
  uint32_t lastProgress = millis();

  while (offset < length) {
    uint32_t remaining = length - offset;
    size_t request = (remaining > AUD_UART_CHUNK_BYTES) ? AUD_UART_CHUNK_BYTES : (size_t)remaining;
    size_t written = Serial2.write(&buffer[offset], request);

    if (written > 0) {
      offset += (uint32_t)written;
      lastProgress = millis();
    } else if ((millis() - lastProgress) > 1000UL) {
      return false;
    } else {
      delay(1);
    }
  }

  return true;
}

static void resetPcmRx() {
  rxMode = RX_LINE;
  magicIndex = 0;
  pcmHeaderIndex = 0;
  pcmPayloadIndex = 0;
  pcmSampleRate = 0;
  pcmSampleCount = 0;
  pcmPayloadBytes = 0;
  pcmSeq = 0;
  pcmExpectedChecksum = 0;
}

static void processStm32Line() {
  if (stm32Line.length() == 0) {
    return;
  }

  Serial.print("STM32: ");
  Serial.println(stm32Line);

  if (stm32Line == "PING") {
    Serial2.print("PONG\r\n");
    Serial.println("ESP32: PONG");
  }

  stm32Line = "";
}

static void appendLineByte(uint8_t byte) {
  if ((byte == '\r') || (byte == '\n')) {
    processStm32Line();
  } else if (stm32Line.length() < 128) {
    stm32Line += (char)byte;
  } else {
    stm32Line = "";
    Serial.println("ESP32: line buffer overflow, reset");
  }
}

static void flushMagicPrefixToLine() {
  for (uint8_t i = 0; i < magicIndex; i++) {
    appendLineByte((uint8_t)PCM_MAGIC[i]);
  }
  magicIndex = 0;
}

static void startPcmHeader() {
  stm32Line = "";
  magicIndex = 0;
  pcmHeaderIndex = 0;
  rxMode = RX_PCM_HEADER;
  Serial.println("ESP32: PCM header start");
}

static void finishPcmFrame() {
  uint32_t actualChecksum = checksumBytes(pcmPayload, pcmPayloadBytes);

  Serial.print("ESP32: PCM seq=");
  Serial.print(pcmSeq);
  Serial.print(" rate=");
  Serial.print(pcmSampleRate);
  Serial.print(" samples=");
  Serial.print(pcmSampleCount);
  Serial.print(" bytes=");
  Serial.print(pcmPayloadBytes);
  Serial.print(" checksum=");
  Serial.print(actualChecksum);
  Serial.print("/");
  Serial.println(pcmExpectedChecksum);

  if (actualChecksum == pcmExpectedChecksum) {
    Serial.println("ESP32: PCM checksum OK");
    forwardPcmToPc();
  } else {
    Serial.println("ESP32: PCM checksum FAIL");
  }

  resetPcmRx();
}

static void handleLineModeByte(uint8_t byte) {
  if ((stm32Line.length() == 0) || (magicIndex > 0)) {
    if (byte == (uint8_t)PCM_MAGIC[magicIndex]) {
      magicIndex++;
      if (magicIndex == 4) {
        startPcmHeader();
      }
      return;
    }

    if (magicIndex > 0) {
      flushMagicPrefixToLine();
    }
  }

  appendLineByte(byte);
}

static void handlePcmHeaderByte(uint8_t byte) {
  pcmHeader[pcmHeaderIndex++] = byte;

  if (pcmHeaderIndex < PCM_HEADER_REMAINING_BYTES) {
    return;
  }

  pcmSampleRate = readLe32(&pcmHeader[0]);
  pcmSampleCount = readLe32(&pcmHeader[4]);
  pcmPayloadBytes = readLe32(&pcmHeader[8]);
  pcmSeq = readLe32(&pcmHeader[12]);
  pcmExpectedChecksum = readLe32(&pcmHeader[16]);
  pcmPayloadIndex = 0;

  Serial.print("ESP32: PCM payload start bytes=");
  Serial.println(pcmPayloadBytes);

  if ((pcmPayloadBytes == 0) || (pcmPayloadBytes > PCM_PAYLOAD_CAPACITY)) {
    Serial.println("ESP32: PCM payload too large, dropping frame");
    rxMode = RX_PCM_DROP;
  } else {
    rxMode = RX_PCM_PAYLOAD;
  }
}

static void handlePcmPayloadByte(uint8_t byte) {
  pcmPayload[pcmPayloadIndex++] = byte;

  if (pcmPayloadIndex >= pcmPayloadBytes) {
    finishPcmFrame();
  }
}

static void handlePcmDropByte() {
  pcmPayloadIndex++;
  if (pcmPayloadIndex >= pcmPayloadBytes) {
    Serial.println("ESP32: PCM dropped");
    resetPcmRx();
  }
}

static void handleStm32Byte(uint8_t byte) {
  switch (rxMode) {
    case RX_LINE:
      handleLineModeByte(byte);
      break;
    case RX_PCM_HEADER:
      handlePcmHeaderByte(byte);
      break;
    case RX_PCM_PAYLOAD:
      handlePcmPayloadByte(byte);
      break;
    case RX_PCM_DROP:
      handlePcmDropByte();
      break;
  }
}

static void resetAudTcp() {
  audTcpMode = AUD_TCP_IDLE;
  audHeaderIndex = 0;
  audSampleRate = 0;
  audSampleCount = 0;
  audPayloadBytes = 0;
  audSeq = 0;
  audChecksum = 0;
  audPayloadForwarded = 0;
  audNextAckAt = AUD_ACK_BYTES;
}

static void startAudServerIfNeeded() {
  if (!wifiConfigured() || (WiFi.status() != WL_CONNECTED) || audServerStarted) {
    return;
  }

  audServer.begin();
  audServer.setNoDelay(true);
  audServerStarted = true;
  Serial.print("ESP32: AUD1 TCP server listening on ");
  Serial.println(AUD_LISTEN_PORT);
}

static void closeAudClient(const char *reason) {
  if (reason != NULL) {
    Serial.print("ESP32: AUD client closed: ");
    Serial.println(reason);
  }
  if (audClient) {
    audClient.stop();
  }
  resetAudTcp();
}

static bool sendAudAck(const char *tag, uint32_t value) {
  if (!audClient || !audClient.connected()) {
    return false;
  }
  audClient.print(tag);
  audClient.print(" ");
  audClient.print(value);
  audClient.print("\n");
  return true;
}

static bool parseAudHeader() {
  if (memcmp(audHeader, AUD_MAGIC, 4) != 0) {
    sendAudAck("AUDERR", 1);
    return false;
  }

  audSampleRate = readLe32(&audHeader[4]);
  audSampleCount = readLe32(&audHeader[8]);
  audPayloadBytes = readLe32(&audHeader[12]);
  audSeq = readLe32(&audHeader[16]);
  audChecksum = readLe32(&audHeader[20]);
  audPayloadForwarded = 0;
  audNextAckAt = AUD_ACK_BYTES;

  if ((audSampleRate != AUD_SAMPLE_RATE) ||
      (audSampleCount == 0) ||
      (audSampleCount > (0xFFFFFFFFU / 2U)) ||
      (audPayloadBytes == 0) ||
      (audPayloadBytes != (audSampleCount * 2U))) {
    sendAudAck("AUDERR", 2);
    return false;
  }

  if (!writeAllUart(audHeader, sizeof(audHeader))) {
    sendAudAck("AUDERR", 3);
    return false;
  }

  Serial.print("ESP32: AUD1 start seq=");
  Serial.print(audSeq);
  Serial.print(" bytes=");
  Serial.print(audPayloadBytes);
  Serial.print(" checksum=");
  Serial.println(audChecksum);
  sendAudAck("AUDHOK", audSeq);
  audTcpMode = AUD_TCP_PAYLOAD;
  return true;
}

static void maybeSendAudProgressAck() {
  while ((audPayloadForwarded >= audNextAckAt) && (audNextAckAt < audPayloadBytes)) {
    sendAudAck("AUDACK", audNextAckAt);
    audNextAckAt += AUD_ACK_BYTES;
  }

  if (audPayloadForwarded >= audPayloadBytes) {
    sendAudAck("AUDACK", audPayloadForwarded);
    sendAudAck("AUDDONE", audSeq);
    Serial.print("ESP32: AUD1 forwarded seq=");
    Serial.print(audSeq);
    Serial.print(" bytes=");
    Serial.println(audPayloadForwarded);
    closeAudClient(NULL);
  }
}

static void handleAudTcpClient() {
  if (!audServerStarted) {
    return;
  }

  if (!audClient || !audClient.connected()) {
    WiFiClient next = audServer.available();
    if (next) {
      if (audClient) {
        audClient.stop();
      }
      audClient = next;
      audClient.setNoDelay(true);
      resetAudTcp();
      audTcpMode = AUD_TCP_HEADER;
      Serial.println("ESP32: AUD1 client connected");
    }
    return;
  }

  while (audClient.connected() && (audClient.available() > 0)) {
    if (audTcpMode == AUD_TCP_HEADER) {
      while ((audClient.available() > 0) && (audHeaderIndex < AUD_HEADER_BYTES)) {
        int byteValue = audClient.read();
        if (byteValue < 0) {
          return;
        }
        audHeader[audHeaderIndex++] = (uint8_t)byteValue;
      }

      if (audHeaderIndex >= AUD_HEADER_BYTES) {
        if (!parseAudHeader()) {
          closeAudClient("bad AUD1 header");
          return;
        }
      }
    } else if (audTcpMode == AUD_TCP_PAYLOAD) {
      uint8_t chunk[AUD_UART_CHUNK_BYTES];
      uint32_t remaining = audPayloadBytes - audPayloadForwarded;
      size_t toRead = (remaining > sizeof(chunk)) ? sizeof(chunk) : (size_t)remaining;
      int available = audClient.available();
      if ((uint32_t)available < toRead) {
        toRead = (size_t)available;
      }
      size_t got = audClient.readBytes(chunk, toRead);
      if (got == 0) {
        return;
      }

      if (!writeAllUart(chunk, (uint32_t)got)) {
        sendAudAck("AUDERR", 4);
        closeAudClient("UART write failed");
        return;
      }

      audPayloadForwarded += (uint32_t)got;
      maybeSendAudProgressAck();
    } else {
      return;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(UART_BAUD, SERIAL_8N1, RX2_PIN, TX2_PIN);

  if (DEBUG_PWM_ENABLE) {
    analogWrite(DEBUG_PWM_PIN, DEBUG_PWM_DUTY);
  }

  Serial.println("ESP32 UART bridge test ready");
  Serial.println("Waiting for STM32 PCM1 frame or PC AUD1 client...");

  if (wifiConfigured()) {
    connectWifiIfNeeded();
    startAudServerIfNeeded();
  } else {
    Serial.println("ESP32: edit WIFI_SSID/WIFI_PASSWORD/PC_HOST to enable TCP");
  }
}

void loop() {
  startAudServerIfNeeded();
  handleAudTcpClient();

  while (Serial2.available() > 0) {
    handleStm32Byte((uint8_t)Serial2.read());
  }
}
