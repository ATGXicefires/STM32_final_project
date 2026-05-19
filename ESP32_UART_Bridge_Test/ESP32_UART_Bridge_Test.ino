/*
 * ESP32 UART bridge smoke test for STM32F407 USART1.
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

static const int RX2_PIN = 16;
static const int TX2_PIN = 17;
static const uint32_t UART_BAUD = 115200;

static String stm32Line;

void setup() {
  Serial.begin(115200);
  Serial2.begin(UART_BAUD, SERIAL_8N1, RX2_PIN, TX2_PIN);

  Serial.println("ESP32 UART bridge test ready");
  Serial.println("Waiting for STM32 PING...");
}

void loop() {
  while (Serial2.available() > 0) {
    char ch = (char)Serial2.read();

    if ((ch == '\r') || (ch == '\n')) {
      if (stm32Line.length() > 0) {
        Serial.print("STM32: ");
        Serial.println(stm32Line);

        if (stm32Line == "PING") {
          Serial2.print("PONG\r\n");
          Serial.println("ESP32: PONG");
        }

        stm32Line = "";
      }
    } else if (stm32Line.length() < 64) {
      stm32Line += ch;
    } else {
      stm32Line = "";
      Serial.println("Line buffer overflow, reset");
    }
  }
}
