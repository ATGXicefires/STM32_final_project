# Stage 8 Audio Streaming Status

Last updated: 2026-05-23

## Current Status

Stage 8 is functional and in stabilization.

- `PCM1` path: STM32 records 0.5 seconds into `record_buffer`, sends a fixed-length PCM frame over USART1, ESP32 receives it, then forwards it to the PC TCP receiver.
- `AUD1` path: PC sends a fixed-length WAV/PCM frame to ESP32 TCP port `5001`, ESP32 forwards chunks to STM32 over `Serial2`, and STM32 plays through MAX98357A from a 64 KB ring buffer.
- Long music playback works. Rare pop/noise events can still happen during longer runs, so the current debug target is to correlate those events with `underrun`, `overflow`, Wi-Fi jitter, or hardware noise.

## Hardware Wiring

STM32 audio wiring remains the Stage 7 known-good wiring:

- MAX98357A `BCLK` -> STM32 `PB13`
- MAX98357A `LRC/WS` -> STM32 `PB12`
- MAX98357A `DIN` -> STM32 `PB15`
- ICS43434 `SCK` -> STM32 `PB13`
- ICS43434 `WS` -> STM32 `PB12`
- ICS43434 `SD/DOUT` -> STM32 `PB14`
- All boards must share GND.

ESP32 bridge wiring:

- STM32 `PA9` USART1 TX -> ESP32 GPIO16 RX2
- STM32 `PA10` USART1 RX <- ESP32 GPIO17 TX2
- STM32 GND <-> ESP32 GND

## UART / Network Settings

- STM32 USART1 and ESP32 `Serial2`: `921600 8N1`
- Periodic PING/PONG is disabled in STM32 firmware to keep audio logs readable.
- ESP32 Wi-Fi secrets live in `ESP32_UART_Bridge_Test/wifi_config.h`, which is ignored by Git.
- `ESP32_UART_Bridge_Test/wifi_config.example.h` documents the expected local settings.
- ESP32 GPIO4 debug PWM is disabled by default (`DEBUG_PWM_ENABLE = false`) because it is not required for the bridge and can be a hardware-noise suspect during long playback.

## Protocols

### PCM1: STM32 To PC

Header is 24 bytes:

- `magic[4] = "PCM1"`
- `sample_rate u32 = 16000`
- `sample_count u32 = 8000`
- `payload_bytes u32 = 16000`
- `seq u32`
- `checksum u32`

Payload is little-endian signed 16-bit mono PCM.

### AUD1: PC To STM32 Playback

Header is 24 bytes:

- `magic[4] = "AUD1"`
- `sample_rate u32 = 16000`
- `sample_count u32`
- `payload_bytes u32 = sample_count * 2`
- `seq u32`
- `checksum u32`

`AUD1` v1 is fixed-length only. `sample_count` is the only length source. There is no unknown-length streaming mode and no end-of-stream frame. `seq` is for log/debug correlation only; STM32 does not reorder or retransmit.

## Playback Buffering

- STM32 USART1 RX uses DMA circular buffering.
- STM32 AUD1 playback uses a 64 KB ring buffer.
- Playback starts after 8 KB prebuffer or after a short payload is fully received.
- ESP32 forwards TCP data to UART in 256-byte UART writes.
- PC sender sends an 8 KB prebuffer, then 1024-byte ACK-paced chunks.
- STM32 writes AUD1 payload into the ring buffer as complete 16-bit sample pairs.
- On underrun, STM32 decays the last sample toward zero instead of hard-switching to zero, reducing audible pops from discontinuities.

## Test Commands

Start the PC receiver for STM32 recordings:

```powershell
.\.venv\Scripts\python.exe tools\pcm_tcp_receiver.py --host 0.0.0.0 --port 5000 --output stage8_received.wav
```

Send a WAV file to ESP32 for playback:

```powershell
.\.venv\Scripts\python.exe tools\aud1_tcp_sender.py "audio_test\your_16k_or_resampleable.wav" --host <ESP32_IP> --port 5001
```

The sender accepts mono or stereo WAV and converts to 16 kHz mono signed 16-bit PCM before sending.

## Current Debug Notes

If a rare pop/noise event happens during long playback, first check STM32 USB CDC logs:

- `AUD level:<n>`: ring buffer fill level in bytes.
- `underrun:<n>`: increments when I2S needs a sample but the ring is empty.
- `overflow:<n>`: increments when incoming UART data cannot fit in the ring.

Interpretation:

- Pop with `underrun` increment or `AUD level` near zero points to buffer starvation, Wi-Fi jitter, TCP pacing, or ESP32 UART scheduling.
- Pop with `overflow` points to PC/ESP32 sending faster than STM32 consumes.
- Pop without underrun/overflow points back to hardware: MAX98357A power, speaker wiring, shared ground, ESP32 GPIO4 PWM noise, or breadboard contact.

## Acceptance Before Stage 9

- K0 built-in clip still plays.
- K1 recording still plays back locally and still emits a valid `PCM1` frame.
- A 5-30 second `AUD1` WAV plays without sustained underrun or overflow.
- Three consecutive short `AUD1` files reset state and play normally.
