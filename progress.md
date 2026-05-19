# STM32 Final Project Progress

Date: 2026-05-19

## Hardware

- Main board: STM32F407VET6 black board.
- D1 is the power LED and stays on by design.
- D2 and D3 are user-controllable LEDs.
- Current STM32 flashing flow: build `NIM_Assistant_F407/Debug/NIM_Assistant_F407.elf`, then manually flash with USB DFU by following `walkthrough2.md`.

## Completed Checkpoints

### Stage 1: GPIO / LED

- D2 is mapped to `PA6`.
- D3 is mapped to `PA7`.
- D2/D3 are active-low LEDs:
  - `GPIO_PIN_RESET` turns the LED on.
  - `GPIO_PIN_SET` turns the LED off.
- Verified behavior:
  - D2/D3 alternate blink when no button is pressed.

### Stage 2: Button Input

- K0 is mapped to `PE4`.
- K1 is mapped to `PE3`.
- Inputs use pull-up logic; pressed state is read as low.
- Verified behavior:
  - K0 turns D2 on and D3 off.
  - K1 turns D2 off and D3 on.
  - K0 + K1 turns both D2 and D3 on.

### Stage 3: USART1 USB-TTL

- USART1 is configured as `115200 8N1`.
- `PA9` is USART1 TX.
- `PA10` is USART1 RX.
- USB-TTL echo test passed after swapping the USB-TTL TX/RX wiring.
- Practical note: the USB-TTL adapter labels can be misleading. Use the wiring that passes echo testing.
- Verified behavior:
  - Tera Term on the USB-TTL COM port can send characters to STM32.
  - STM32 replies with `RX: <char>`.
  - STM32 USB CDC also reports the received characters and button events.

## Current Firmware Behavior

- Preserves GPIO/Button test behavior.
- Sends debug text over USART1 and USB CDC.
- Accepts single-byte USART1 input and echoes `RX: <char>`.
- Sends `PING` over USART1 once per second for the ESP32 UART bridge test.
- Reports `ESP32 PONG OK` when a full `PONG` line is received.

## Next Checkpoint

Stage 4 will validate STM32 USART1 to ESP32 UART communication:

- STM32 sends `PING`.
- ESP32 replies `PONG`.
- STM32 reports `ESP32 PONG OK` through USB CDC.

