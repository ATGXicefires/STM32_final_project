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
- Hardware beep test passed at 3.3V with speaker connected.

### Stage 5 Follow-up: Embedded WAV Clip

- Source WAV: `D:\AI_Voices\koharu_GPTSoVITS模型\参考音频\CH0205_Lobby_5.wav`.
- Source format: mono, 16-bit PCM, 44.1 kHz, about 4.108 seconds.
- Converted firmware clip:
  - `NIM_Assistant_F407/Core/Inc/audio_clip.h`
  - 16 kHz mono, 16-bit PCM
  - 35% gain to reduce loudness
- Firmware plays the converted clip through I2S2 once when K1 is pressed.
- Playback is currently blocking; button and UART processing pause during the roughly 4-second clip.
- Compile-only verification passed and produced `NIM_Assistant_F407/Debug/NIM_Assistant_F407.elf`.

## Current Firmware Behavior

- Preserves GPIO/Button test behavior.
- Sends debug text over USART1 and USB CDC.
- Accepts single-byte USART1 input and echoes `RX: <char>`.
- Sends `PING` over USART1 once per second for the ESP32 UART bridge test.
- Reports `ESP32 PONG OK` when a full `PONG` line is received.
- K1 toggles microphone monitor ON/OFF.
- I2S2 master TX continuously feeds DR=0 via busy-wait in main loop to keep
  BCLK/WS alive for the microphone.
- CDC_Transmit_FS has retry logic (up to 3 retries with 1ms delay) when BUSY.
- UART RX buffer overflow is now logged.

## Next Checkpoint

Stage 6 microphone diagnosis must be resolved at the hardware level before
proceeding to audio loopback.

## Current Investigation: I2S Microphone Input

### Stage 6: INMP441 / ICS43434 Mic Level Test

- Goal: verify I2S microphone input before attempting MAX98357A loopback.
- Microphone module label observed: `ICS43434` even though the package was
  marked as `INMP441`.
- Expected compatibility:
  - Both INMP441 and ICS43434 are I2S MEMS microphones.
  - The test can still use STM32 I2S clock + word select + data input.
- STM32 I2S2 configuration:
  - `PB13` = I2S2_CK / BCLK / microphone `SCK`
  - `PB12` = I2S2_WS / LRCLK / microphone `WS`
  - `PB14` = I2S2ext_SD / microphone `SD` or `DOUT` (AF6_I2S2ext)
  - `PB15` = I2S2_SD / master TX out (for MAX98357A speaker)
  - I2S2 full-duplex mode enabled
  - 24-bit I2S data format, Philips standard
  - 16 kHz audio frequency
  - MCLK disabled
- Microphone wiring used for test:
  - `VDD` -> `3.3V`
  - `GND` -> `GND`
  - `SCK` -> `PB13`
  - `WS` -> `PB12`
  - `SD` / `DOUT` -> `PB14`
  - `L/R` tested with both `GND` and `3.3V`
- MAX98357A is not required for this microphone-only test and can remain
  disconnected.

### Software Investigation Steps (2026-05-20)

The following firmware changes were applied iteratively to diagnose the mic
input failure. Each step eliminated a potential software cause.

#### Step 1: Switch from manual register polling to HAL API

- **Change**: replaced hand-written SPI DR/SR register polling with
  `HAL_I2SEx_TransmitReceive(&hi2s2, txBuf, rxBuf, 32, 500)`.
- **Result**: HAL returned `HAL_OK` (`state:1 code:0`), confirming the I2S
  peripheral state machine is functional.
- **Finding**: `rx_buf` first 4 words were `00000000`, but `level:49 peak:564`
  indicated non-zero data existed in later indices.

#### Step 2: Full rx_buf hex dump

- **Change**: printed all 64 `uint16_t` entries as hex (8 per line).
- **Result**: non-zero values appeared at indices 25–61. Pattern:
  - Every 4th `uint16_t` pair (left channel) had data.
  - Alternating pairs (right channel) were all zero.
  - First non-zero index: 25.
- **Finding**: microphone data appeared on the **left channel** (consistent
  with INMP441 behavior with L/R=GND, not ICS43434).

#### Step 3: Left-channel extraction with startup skip

- **Change**: extracted only left-channel samples (`rx_buf[i*4]` and
  `rx_buf[i*4+1]`), skipped first 16 stereo frames.
- **Result**: `avg:2412536 peak:4492256 first:220272 last:-2633792`, but
  values were **exactly the same every reading**. No reaction to sound.
- **Finding**: data was the deterministic I2S startup transient, not live
  audio.

#### Step 4: OVR (overrun) clearing with I2S disable

- **Change**: added `__HAL_I2S_DISABLE` / `__HAL_I2SEXT_DISABLE`, flushed
  stale DR/SR, reset HAL state, then called `HAL_I2SEx_TransmitReceive`.
- **Result**: still the same constant values.
- **Finding**: disabling I2S stops BCLK → mic loses clocks → always re-enters
  startup transient.

#### Step 5: OVR clearing without disable + manual RXNE polling

- **Change**: cleared OVR by reading ext DR+SR only (no disable), then
  manually polled RXNE for fresh data.
- **Result**: values cycled through 4 fixed states.
- **Finding**: frame alignment issue — starting poll at different phases
  within the 4-word stereo frame.

#### Step 6: CHSIDE frame synchronization

- **Change**: used `I2S_FLAG_CHSIDE` bit to sync to Left channel boundary
  before recording data.
- **Result**: no more 4-state cycling, but values still constant.
- **Finding**: frame alignment fixed, but stale data problem remained.

#### Step 7: Large warmup skip (4096 RXNE, skip first 3072)

- **Change**: polled 4096 RXNE events (~128ms), discarded first 3072 (~96ms
  warmup), analyzed only the last 1024.
- **Result**: `MIC RX timeout at n=0`.
- **Finding**: clearing OVR consumed the last available DR value, and without
  continuous TX feeding, no new clocks were generated → RXNE never set.

#### Step 8: Root cause identified — I2S master TX clock generation

- **Discovery**: STM32 I2S master TX only generates BCLK/WS when data is
  written to `SPI_DR`. Without continuous TX feeding, clocks stop after each
  frame, and the microphone never receives sustained clocking.
- **This explained all previous symptoms**:
  - `HAL_I2SEx_TransmitReceive` temporarily started clocks (by writing DR),
    producing the startup transient, then clocks stopped.
  - Each call re-started clocks → same startup transient every time.
  - Manual polling without TX writes → no clocks → timeout.

#### Step 9: Continuous TX feeding

- **Change**:
  - Added `Test_FeedI2STx()`: checks TXE and writes `DR=0` when empty.
  - At init: wrote first zero to DR immediately after `__HAL_I2S_ENABLE` to
    kick-start clock generation.
  - Replaced `HAL_Delay(20)` with busy-wait loop that calls
    `Test_FeedI2STx()` continuously during the 20ms wait.
- **Result**: `MIC avg:0 peak:0 first:0 last:0 n:128`.
- **Finding**: I2S clocks are now running continuously. The ext RX receives
  fresh data (RXNE fires, no timeout). **But all received data is zero.**

#### Step 10: Hardware probing and dual-channel test

- **Hardware probing results**:
  - PB13 (SCK): confirmed clock present (~1.6V DC average) ✅
  - PB12 (WS): confirmed WS signal present (~1.6V DC average) ✅
  - PB14 disconnected (floating): random noise values → PB14 I2S ext ✅
  - PB14 connected to 3.3V: near-max values → PB14 I2S ext ✅
  - PB14 connected to mic: all zeros → mic SD not driving data
  - Two different ICS43434 modules tested: same result → not a defective module
- **Software change**: modified firmware to read and report **both Left and
  Right channels** independently.
- **Result**: **Right channel (CHSIDE=1) has mic data!** Left channel is zero.
  Values react to sound: quiet ~1000, loud sounds up to ~8,000,000.
- **Root cause**: ICS43434 with SELECT=GND outputs on the **Right channel**
  (CHSIDE=1), not the Left channel as assumed.

### Resolution Summary

Two independent root causes were discovered and fixed:

1. **I2S master TX must continuously feed DR to generate BCLK/WS.**
   STM32 I2S master only generates clocks when `SPI_DR` has data to send.
   Without continuous TX feeding, clocks stop between frames, causing the
   microphone to repeatedly enter its startup transient. Fixed by adding
   `Test_FeedI2STx()` in the main loop busy-wait.

2. **ICS43434 with SELECT=GND outputs on Right channel (CHSIDE=1).**
   Despite being sold as INMP441, the actual IC is ICS43434, which maps
   SELECT=GND to the Right I2S channel (opposite of our initial assumption).
   Fixed by reading CHSIDE=1 data instead of CHSIDE=0.

### Stage 6 Completion

- ✅ Microphone receives valid I2S data from ICS43434.
- ✅ Sound level monitoring works: K1 toggles ON/OFF, USB CDC prints
  `MIC avg:<level> peak:<peak> n:<sample_count>`.
- ✅ Values react to ambient sound (quiet ~1000, loud ~8,000,000+).
- ✅ Continuous I2S clocking via main-loop TX feeding.

## Next Checkpoints

### Stage 7: Audio Loopback (Mic → Speaker)

- Goal: pipe ICS43434 mic input directly to MAX98357A speaker output via I2S2.
- Requires reconnecting MAX98357A and forwarding right-channel RX samples to
  TX DR in real-time.
- Current blocking poll-based approach may need to transition to DMA for
  latency and reliability.

### Stage 8: ESP32 Audio Streaming

- Goal: stream mic audio over UART to ESP32, then TCP to PC server.
- Requires defining a packetization protocol for raw PCM data.

### Stage 9: Full Pipeline (ASR → NIM → TTS → Playback)

- Goal: end-to-end voice assistant with NVIDIA NIM cloud inference.
