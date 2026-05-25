# Stage 8 Audio Streaming Status

Last updated: 2026-05-25

## Current Status

Stage 8 is complete.

- `PCM1` path: K1 hold-to-record streaming. While K1 is held, STM32 continuously fills 0.5-second double-buffers (8000 samples each) and enqueues each completed buffer to a 2-slot PCM TX queue over USART1. ESP32 receives each PCM1 frame and forwards it to the PC TCP receiver over a **persistent TCP session** (one connection per K1 session, not per frame). On K1 release, any partial buffer (actual recorded samples, no zero padding) is also queued and sent. PC side concatenates all frames into a single WAV.
- `AUD1` path: PC sender converts a WAV file to 16 kHz mono signed 16-bit PCM, sends one fixed-length `AUD1` frame to ESP32 TCP port `5001`, ESP32 ACKs and forwards chunks to STM32 over `Serial2`, and STM32 plays through MAX98357A from a 64 KB ring buffer.

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

## PC Tooling Notes

- Use the project `.venv` for Stage 8 test tools.
- In the Codex sandbox, `.venv\Scripts\python.exe` may require elevated execution; if it works after elevation, do not rebuild the venv just because the sandbox launch failed.
- `tools/aud1_tcp_sender.py` uses only stdlib (`wave`, `struct`, `math`, `select`) — no `audioop` dependency. It runs on Python 3.9+ including Python 3.13+.
- `tools\pcm_tcp_receiver.py` can run on newer Python, but keep both receiver and sender on the same `.venv` during acceptance tests to avoid environment drift.

## Protocols

### PCM1: STM32 To PC

Header is 24 bytes:

- `magic[4] = "PCM1"`
- `sample_rate u32 = 16000`
- `sample_count u32` — 8000 for full 0.5-second chunks; smaller for the final partial chunk on K1 release
- `payload_bytes u32 = sample_count * 2`
- `seq u32`
- `checksum u32`

Payload is little-endian signed 16-bit mono PCM. One recording session (K1 hold to K1 release) produces N full frames followed by one partial frame. The PC receiver concatenates all payloads into a single WAV whose total length matches the K1 hold duration.

### AUD1: PC To STM32 Playback

Header is 24 bytes:

- `magic[4] = "AUD1"`
- `sample_rate u32 = 16000`
- `sample_count u32`
- `payload_bytes u32 = sample_count * 2`
- `seq u32`
- `checksum u32`

`AUD1` v1 is fixed-length only. `sample_count` is the only length source. There is no unknown-length streaming mode and no end-of-stream frame in the STM32 protocol. `seq` is for log/debug correlation only; STM32 does not reorder or retransmit.

ESP32 sends simple ASCII ACK lines back to the PC TCP sender:

- `AUDHOK <seq>` after the header is accepted and forwarded to STM32 UART.
- `AUDACK <bytes>` as payload bytes are forwarded to STM32 UART.
- `AUDDONE <seq>` after ESP32 has forwarded the full payload to STM32 UART and closed the TCP client.
- `AUDERR <code>` when ESP32 rejects the header or cannot continue forwarding.

`tools\aud1_tcp_sender.py` processes these ACKs with a **sliding window** flow control model. It waits for `AUDHOK` before sending payload, then sends chunks freely while keeping in-flight unacknowledged bytes below `--window-bytes` (default 24 KB). `AUDACK` lines are drained non-blockingly via `select`; the sender only blocks when the window is full. It waits for `AUDDONE` to confirm full delivery, and treats `AUDERR` as failure.

## Playback Buffering

- STM32 USART1 RX uses DMA circular buffering.
- STM32 AUD1 playback uses a 64 KB ring buffer.
- Playback starts after 8 KB prebuffer or after a short payload is fully received.
- ESP32 UART2 RX buffer is 32 KB (`Serial2.setRxBufferSize(32768)`) to absorb the 16 KB PCM1 payload while a TCP connection is being established for the previous frame.
- ESP32 forwards TCP data to UART in 256-byte UART writes.
- PC sender uses a sliding window (default 24 KB) to keep STM32's ring buffer filled. The first 8 KB (prebuffer) is sent in a burst before rate-limiting begins; subsequent chunks pace at real-time 16 kHz 16-bit mono (32 KB/s). The 24 KB window provides approximately 750 ms of Wi-Fi jitter protection at any point in the stream.
- Sender pacing defaults can be tuned with `--prebuffer-bytes`, `--chunk-bytes`, and `--window-bytes`.
- STM32 writes AUD1 payload into the ring buffer as complete 16-bit sample pairs.
- On underrun, STM32 decays the last sample toward zero instead of hard-switching to zero, reducing audible pops from discontinuities.

## Test Commands

Start the PC receiver for STM32 recordings:

```powershell
.\.venv\Scripts\python.exe tools\pcm_tcp_receiver.py
```

Send the built-in default WAV file (`audio_test/test.wav`) to the default ESP32 IP (`172.20.10.3`) for playback:

```powershell
.\.venv\Scripts\python.exe tools\aud1_tcp_sender.py
```

The sender currently has default playback settings built in: default WAV path (`audio_test/test.wav`), ESP32 host `172.20.10.3`, TCP port `5001`, sequence `1`, 8 KB prebuffer, 1024-byte chunks, and a 24 KB sliding window.

Only pass extra arguments when overriding the built-in defaults, for example:

```powershell
.\.venv\Scripts\python.exe tools\aud1_tcp_sender.py --host 192.168.1.100 --seq 2
.\.venv\Scripts\python.exe tools\aud1_tcp_sender.py "audio_test\other.wav" --host 192.168.1.100
```

When a WAV path is provided, the sender accepts mono or stereo WAV and converts to 16 kHz mono signed 16-bit PCM before sending.

Useful sender controls:

- `--seq <n>` changes the debug sequence number shown in ESP32 and STM32 logs.
- `--prebuffer-bytes <n>` changes how much payload is sent in a burst before real-time pacing starts (default: 8192).
- `--chunk-bytes <n>` changes the payload bytes per send call (default: 1024).
- `--window-bytes <n>` changes the sliding window size — max in-flight unacknowledged bytes (default: 24576; must not exceed STM32 ring buffer 65536).

## Debug Reference

STM32 USB CDC log fields for AUD1 playback:

- `AUD level:<n>`: ring buffer fill level in bytes.
- `underrun:<n>`: increments when I2S needs a sample but the ring is empty.
- `overflow:<n>`: increments when incoming UART data cannot fit in the ring.

Interpretation:

- Pop with `underrun` or `AUD level` near zero → buffer starvation; check Wi-Fi jitter, TCP pacing, or ESP32 UART scheduling.
- Pop with `overflow` → PC/ESP32 sending faster than STM32 consumes.
- Pop without underrun/overflow → hardware suspect: MAX98357A power, speaker wiring, shared GND, breadboard contact.

## Acceptance (Completed)

- K0 built-in clip plays. ✓
- K1 hold-to-record streaming delivers all PCM1 frames with correct checksums; `pcm_tcp_receiver.py` saves a WAV whose length matches the recording duration. ✓
- A 5-30 second `AUD1` WAV plays without sustained underrun or overflow. ✓
- Three consecutive short `AUD1` files reset state and play normally. ✓
- ESP32 PCM1 UART RX overrun resolved (32 KB RX buffer). ✓
- `audioop` dependency removed; `aud1_tcp_sender.py` runs on Python 3.13+. ✓
- ESP32 PCM1 TCP uses a persistent session per K1 recording (no per-frame handshake overhead). ✓
- STM32 K1 rapid press: in-flight PCM packet drained before TX state reset (no WAV truncation). ✓
- AUD1 sender uses sliding window (24 KB default) for 750 ms Wi-Fi jitter protection. ✓
