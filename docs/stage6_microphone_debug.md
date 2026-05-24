# Stage 6 ICS43434 Microphone Debug Record

## 2026-05-22 Correction

Stage 7 diagnostics supersede the original channel conclusion in this file.

- Datasheets for ICS43434 / INMP441-style microphones specify `L/R=GND` as left channel and `L/R=3.3V` as right channel.
- Later Stage 7 logs showed useful microphone response mainly on the left channel.
- Earlier statements below saying `SELECT=GND` maps to right channel are kept as historical debug notes, not the current wiring rule.
- Current Stage 7 status and wiring notes are documented in [stage7_loopback_debug.md](stage7_loopback_debug.md).

## Goal

驗證 STM32F407 是否能透過 I2S2 full-duplex 讀到 I2S MEMS microphone 資料，為 Stage 7 audio loopback 做準備。

This file is historical debug evidence. The current project state is Stage 8 audio streaming stabilization; do not use this file as the live roadmap.

## Hardware

- Microphone module label: ICS43434，雖然購買時標示 INMP441。
- STM32 I2S2 pins:
  - `PB13` = I2S2_CK / BCLK / microphone SCK
  - `PB12` = I2S2_WS / LRCLK / microphone WS
  - `PB14` = I2S2ext_SD / microphone SD/DOUT
  - `PB15` = I2S2_SD / MAX98357A DIN
- I2S settings:
  - Full-duplex mode
  - Philips I2S
  - 24-bit data format during mic diagnosis
  - 16 kHz audio frequency
  - MCLK disabled

## Main Findings

### 1. STM32 I2S master TX must be continuously fed

STM32 I2S master TX only keeps BCLK/WS running while TX data is being written. If firmware stops writing to `SPI_DR`, the clock stops, the microphone loses clock, and later reads only see startup transient or no fresh data.

Fix:

- Add a service function that writes zero to I2S TX whenever TXE is set.
- Call it in the main loop and during short waits.
- Kick-start I2S by writing the first zero after enabling I2S.

### 2. Historical finding: SELECT=GND appeared to output on right channel

At this point in the Stage 6 investigation, SELECT tied to GND appeared to produce valid data on right channel (`CHSIDE=1`). Later Stage 7 diagnostics corrected this interpretation; use the Stage 7 document for current channel handling.

Historical action taken at that time:

- Read right-channel frames only.
- Treat `CHSIDE=1` as valid microphone sample timing.

Current status: this was later superseded. Stage 7 diagnostics showed the useful microphone data is on the left channel for the current wiring.

## Investigation Log

### Step 1: HAL transmit/receive test

Changed manual register polling to `HAL_I2SEx_TransmitReceive(...)`.

Result:

- HAL returned OK.
- Some non-zero values existed in `rx_buf`, but not in the expected early indices.

Finding:

- I2S peripheral was functional, but buffer/channel interpretation was wrong.

### Step 2: Full rx_buf dump

Printed all 64 `uint16_t` entries.

Result:

- Non-zero values appeared in a repeated stereo-frame pattern.
- Early assumption was that the microphone might be on left channel.

Finding:

- Frame alignment and channel selection needed more careful handling.

### Step 3: Startup transient problem

Extracted channel samples after skipping initial frames.

Result:

- Values were non-zero but identical across repeated reads.

Finding:

- The data was deterministic startup transient, not live audio.

### Step 4: Clearing OVR by disabling I2S

Disabled I2S, flushed DR/SR, reset HAL state, then tried to receive again.

Result:

- Same constant values.

Finding:

- Disabling I2S stopped BCLK/WS, so every read restarted the microphone clock and reproduced the same transient.

### Step 5: Manual RXNE polling

Cleared overrun without disabling I2S, then manually polled RXNE.

Result:

- Values cycled through a small number of fixed states.

Finding:

- Polling began at different phases inside the stereo frame.

### Step 6: CHSIDE synchronization

Used `I2S_FLAG_CHSIDE` to align reads to a channel boundary.

Result:

- Frame phase became stable, but data still did not react to sound.

Finding:

- Channel alignment was fixed, but clock generation was still not continuous.

### Step 7: Large warmup skip

Polled thousands of RXNE events and discarded early samples.

Result:

- Timeout occurred when no TX writes were feeding the I2S master.

Finding:

- No continuous TX feed means no continuous clock, so RXNE stops.

### Step 8: Continuous TX feed

Added continuous writes to I2S TX.

Result:

- Fresh RXNE events appeared consistently.
- Data was initially all zero.

Finding:

- Clock problem was solved; remaining issue was channel selection or hardware output.

### Step 9: Hardware probing and dual-channel report

Probed and tested:

- `PB13` BCLK had clock.
- `PB12` WS had clock.
- `PB14` floating produced random values.
- `PB14` tied high produced near-max values.
- `PB14` connected to mic initially looked like zero on left-channel reads.
- Reporting both channels showed right-channel values reacted to sound.

Historical finding:

- At the time, ICS43434 mic data appeared to be on right channel (`CHSIDE=1`) for the tested wiring.
- Stage 7 later corrected the active-channel interpretation; current firmware diagnostics use left channel for `L/R=GND`.

## Resolution

Stage 6 is considered complete because:

- I2S clocks are continuous.
- STM32 receives fresh microphone data.
- Microphone sample values react to ambient sound.
- Active-channel handling was later refined during Stage 7 diagnostics.

This Stage 6 note is closed. Stage 7 has already moved the firmware to I2S2 full-duplex DMA circular buffers, and current work is tracked in [stage8_audio_streaming.md](stage8_audio_streaming.md).
