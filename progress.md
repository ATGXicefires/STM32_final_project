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

### Stage 4: ESP32 UART PING/PONG

- STM32 USART1 remains `PA9` TX and `PA10` RX at `115200 8N1`.
- ESP32 test firmware uses `Serial2` at `115200 8N1`.
- Verified behavior:
  - STM32 sends `PING`.
  - ESP32 replies `PONG`.
  - STM32 reports `ESP32 PONG OK` through USB CDC.

### Stage 5: MAX98357A I2S Beep

- STM32 I2S2 is configured as master transmit, `16 kHz`, `16-bit`, Philips I2S.
- MAX98357A wiring target:
  - `PB13` -> `BCLK`
  - `PB12` -> `LRC / WS`
  - `PB15` -> `DIN`
  - `GND` <-> common ground
  - `VIN` -> 3.3V or 5V according to the MAX98357A module label
- MCLK is disabled because MAX98357A does not require it.
- Firmware sends a short blocking I2S beep every 3 seconds and reports `I2S beep sent` through USB CDC.
- Compile-only verification passed and produced `NIM_Assistant_F407/Debug/NIM_Assistant_F407.elf`.
- Hardware audio output still needs manual flashing and speaker test.

## Current Firmware Behavior

- Preserves GPIO/Button test behavior.
- Sends debug text over USART1 and USB CDC.
- Accepts single-byte USART1 input and echoes `RX: <char>`.
- Sends `PING` over USART1 once per second for the ESP32 UART bridge test.
- Reports `ESP32 PONG OK` when a full `PONG` line is received.
- Sends a short I2S2 beep buffer every 3 seconds for MAX98357A testing.
- Reports `I2S beep sent` over USB CDC after each successful beep transmit.

## Next Checkpoint

Stage 5 hardware validation will check MAX98357A audio output:

- Manually flash the new `.elf` by following `walkthrough2.md`.
- Open the STM32 USB CDC COM port.
- Confirm `I2S beep sent` appears.
- Confirm the speaker connected to MAX98357A outputs a short beep every few seconds.
