# Stage 7 Audio Capture/Playback Debug Record

Last updated: 2026-05-23

## Current Status

- K0 playback works.
  - `audio_test/BA_V_Koharu_Login_1.ogg` was decoded into `audio_test/test.wav`, then converted into `Core/Inc/audio_clip.h`.
  - Pressing K0 prints `K0 pressed: Audio clip playback`.
  - MAX98357A and speaker can play the embedded clip normally.
- K1 record-then-playback **works with recognizable speech**.
  - Records 0.5 seconds of mic audio into RAM, then plays through MAX98357A.
  - Signal processing chain: DC removal → invalid sample rejection → IIR LPF (alpha≈1/8) → noise gate (±80) → gain (12x).
  - Invalid samples use filter decay instead of hard zero to avoid pops.
  - Background noise is still present but speech/knocking is clearly recognizable.
  - A `RECORD_TEST_TONE` switch confirms the playback path works (400Hz triangle wave plays cleanly).
- Live speaker loopback remains disabled (`LOOPBACK_SPEAKER_ENABLE 0U`) intentionally.
  - The final assistant pipeline does not require real-time mic-to-speaker loopback.
  - Live loopback remains diagnostic/deferred because it can cause feedback, burst noise, and unnecessary safety risk.
- Current firmware has been moved from manual I2S polling to I2S2 full-duplex DMA circular buffers.
  - RX: `I2S2_EXT_RX` on `DMA1_Stream3`, channel 3, circular halfword buffer.
  - TX: `SPI2_TX` on `DMA1_Stream4`, channel 0, circular halfword buffer.
  - The DMA buffer is 512 halfwords, or 128 stereo frames at 24-bit I2S format.
  - HAL's 24-bit I2S DMA API doubles its `Size` internally, so firmware passes `256` to cover the 512-halfword buffer exactly.
- DMA hardware validation has been completed enough to move on.
  - Repeated K1 record/playback no longer has OVR as the blocking issue.
  - The accepted Stage 7 behavior is buffered capture first, playback after the 0.5 second buffer fills.
- Current record/playback constants in `Core/Src/main.c`:
  - `RECORD_SAMPLE_COUNT 8000U` (0.5 seconds at 16 kHz)
  - `RECORD_GAIN 12`
  - `MIC_INVALID_MAGNITUDE 500000U`
  - `RECORD_NOISE_GATE 80`

## Hardware State

- STM32F407 I2S2 pins:
  - `PB13` = I2S2_CK / BCLK
  - `PB12` = I2S2_WS / LRCLK
  - `PB14` = I2S2ext_SD / microphone SD/DOUT
  - `PB15` = I2S2_SD / MAX98357A DIN
- MAX98357A:
  - K0 playback proves the output path works.
  - The amplifier was first powered from the shared 3.3V breadboard rail.
  - It was later moved to independent 5V power.
  - The sharp loopback/noise behavior remained, so amplifier power is probably not the primary cause.
- Microphone:
  - MIC VDD is still expected to be stable 3.3V.
  - MIC, STM32, and MAX98357A must share GND.
  - Add a local `0.1uF` capacitor from MIC VDD to GND if not already present.
  - A `100kΩ` pull-down from MIC `SD/DOUT` to GND has been tested. It remains useful for inactive-channel stability even though DMA/OVR is no longer the main blocker.

## Wiring Table

### I2S Shared Clock Lines

The STM32 is the I2S master. Both the microphone and amplifier use the same BCLK and LRCLK/WS.

| Signal | STM32F407 pin | ICS43434 / INMP441 MIC | MAX98357A |
| :--- | :--- | :--- | :--- |
| BCLK / SCK | `PB13` | `SCK` | `BCLK` |
| LRCLK / WS / LRC | `PB12` | `WS` | `LRC` / `WS` |
| MIC data into STM32 | `PB14` / I2S2ext_SD | `SD` / `DOUT` | - |
| Audio data to amp | `PB15` / I2S2_SD | - | `DIN` |
| Ground | `GND` | `GND` | `GND` |

### Microphone Wiring

| ICS43434 / INMP441 pin | Connect to | Note |
| :--- | :--- | :--- |
| `VDD` | STM32 `3.3V` | Use 3.3V only. |
| `GND` | Common `GND` | Must share ground with STM32 and MAX98357A. |
| `SCK` | STM32 `PB13` | I2S bit clock from STM32. |
| `WS` | STM32 `PB12` | I2S word select / LRCLK from STM32. |
| `SD` / `DOUT` | STM32 `PB14` | I2S microphone data into STM32. |
| `L/R` / `LR` / `SELECT` | `GND` for left, `3.3V` for right | Do not leave floating. Test both if channel behavior is unclear. |
| `SD` / `DOUT` | `100kΩ` to `GND` | Pull-down tested; still useful, but it did not fully remove occasional full-scale spikes. |
| `VDD` to `GND` | `0.1uF` capacitor | Place close to the mic module. |

### MAX98357A Wiring

| MAX98357A pin | Connect to | Note |
| :--- | :--- | :--- |
| `VIN` / `VCC` | `5V` if module supports it | Current setup uses independent 5V, which is preferred for speaker power. |
| `GND` | Common `GND` | Must share ground with STM32 and MIC. |
| `BCLK` | STM32 `PB13` | Shared I2S bit clock. |
| `LRC` / `WS` | STM32 `PB12` | Shared I2S word select. |
| `DIN` | STM32 `PB15` | I2S audio output from STM32. |
| `GAIN` / `SD` | Leave as module default or wire per module docs | Floating/default behavior depends on the breakout; K0 playback currently works. |
| `SPK+` / `SPK-` | Speaker terminals | Do not connect speaker directly to STM32. |
| `VIN` to `GND` | `100uF` + `0.1uF` capacitors | Recommended near the amp module to reduce supply dips/noise. |

### Current Known-Good Output Test Wiring

K0 playback confirms this path is working:

```text
STM32 PB13 -> MAX98357A BCLK
STM32 PB12 -> MAX98357A LRC/WS
STM32 PB15 -> MAX98357A DIN
MAX98357A VIN -> independent 5V
MAX98357A GND -> common GND
Speaker -> MAX98357A SPK+/SPK-
```

### Current Microphone Debug Wiring To Verify

```text
STM32 PB13 -> MIC SCK
STM32 PB12 -> MIC WS
STM32 PB14 -> MIC SD/DOUT
STM32 3.3V -> MIC VDD
Common GND -> MIC GND
MIC L/R -> GND or 3.3V, depending on channel test
MIC SD/DOUT -> 100kΩ -> GND
MIC VDD -> 0.1uF -> GND
```

## Important Datasheet Note

For ICS43434 / INMP441-style I2S microphones:

- `L/R` or `LR/SELECT` low = microphone outputs on the left channel.
- `L/R` or `LR/SELECT` high = microphone outputs on the right channel.
- SD is tri-stated when the microphone is not actively driving its selected channel.
- Datasheets recommend a `100kΩ` pull-down on SD to discharge the line while tri-stated.

Reference:

- ICS-43434 datasheet, pin function descriptions: https://www.alldatasheet.com/html-pdf/1137970/TDK/ICS-43434/703/10/ICS-43434.html
- INMP441 datasheet summary: https://www.mouser.com/datasheet/2/400/INMP441-1112508.pdf

## Firmware Changes Tried

### Current Software Fixes Applied

The current diagnostic firmware also includes software fixes from the 2026-05-22 code review:

- `MIC_LOOPBACK_CHANNEL` is now used for output generation.
  - Non-target channels are still counted in diagnostics.
  - Non-target channels no longer update `output_sample` or `output_filter`.
- The output filter path was simplified.
  - Target-channel output handling is now one block.
  - `dc_estimate` is a single target-channel state instead of a two-element array.
  - `dc_estimate` and `output_filter` reset when loopback is off or OVR is cleared.
- I2S2ext OVR is checked and cleared inside the audio service path.
  - On OVR, firmware reads/clears the extension data/status registers through `__HAL_I2SEXT_CLEAR_OVRFLAG`.
  - The high/low word state machine is reset for both channels after OVR.
- RX status is read once per RX event.
  - The same `SR` snapshot is used for both `RXNE` and `CHSIDE`.
- K1 no longer toggles live speaker loopback.
  - Pressing K1 starts a 0.5 second mic recording.
  - Invalid full-scale samples are stored as silence and counted in `inv`.
  - After the buffer fills, firmware starts playback automatically.
  - Pressing K1 during recording or playback cancels the record/playback state.
- The service now drains up to 16 RXNE events per call.
  - This reduces the chance that the 20ms polling loop or USB log reporting leaves stale RX data behind.
- Record-complete logs include invalid sample and OVR count:

```text
Mic record done: playback start inv:<...> Lavg:<...> Lpk:<...> Ravg:<...> Rpk:<...> ovr:<...>
```

Interpretation:

- `ovr` is a diagnostic field for confirming the DMA path remains healthy.
- `inv` should be low; a high value means full-scale invalid samples are still being muted.
- Live speaker output is still disabled by `LOOPBACK_SPEAKER_ENABLE 0U`.
- The old disabled `#if 0` one-shot mic monitor was removed from `main.c` to avoid stale right-channel comments.
- The diagnostic message buffer was increased to 192 bytes.
- Current constants:

```c
#define MIC_LOOPBACK_CHANNEL 0U
#define LOOPBACK_GAIN 32
#define LOOPBACK_OUTPUT_LIMIT 900
#define MIC_INVALID_MAGNITUDE 500000U
#define LOOPBACK_SPEAKER_ENABLE 0U
#define RECORD_SAMPLE_COUNT 8000U
#define RECORD_GAIN 12
#define RECORD_NOISE_GATE 80
#define RECORD_TEST_TONE 0U
```

Notes:

- `MIC_LOOPBACK_CHANNEL 0U` matches the observed left-channel microphone response.
- `MIC_INVALID_MAGNITUDE 500000U` rejects the large false peaks that previously reached `8388608`.
- Speaker output remains disabled even though gain/output constants are present.

### 1. K0 Audio Clip Playback

Goal: verify MAX98357A and speaker before debugging microphone loopback.

Changes:

- Converted the decoded Koharu login `audio_test/test.wav` to `Core/Inc/audio_clip.h`.
- Added K0-triggered playback.
- Fixed I2S 24-bit stereo output so one mono sample is sent to both left and right channels before advancing the sample index.

Result:

- K0 playback is normal.
- This confirms the I2S TX path, MAX98357A, and speaker are usable.

### 2. Initial K1 Loopback

Goal: route microphone samples to MAX98357A.

Changes:

- K1 toggled `Audio loopback ON/OFF`.
- The code read I2S2ext RX samples and wrote them to I2S2 TX.
- Initial scaling was conservative.

Result:

- No useful audible microphone output.
- Logs showed sample counts near 16 kHz, so the loop was running.

### 3. Output Channel Fix For MAX98357A

Issue:

- MAX98357A can average or select channels depending on the module configuration.
- Earlier code could send data in a way that advanced the sample index too quickly or only effectively drove one side.

Change:

- The same mono sample is now sent to both left and right I2S output channels.

Result:

- K0 playback became normal and stable.
- K1 still had microphone-side problems.

### 4. Gain Increase

Goal: check whether loopback was silent only because microphone level was too low.

Changes tried:

- Increased loopback gain.
- Added DC removal.
- Added output limiting.

Result:

- Some versions caused full-scale output and sharp noise.
- Logs frequently showed `out:32767` or `out:32768`, meaning the firmware was clipping.
- This was not a good final loopback path.

### 5. Left/Right Channel Experiments

Goal: determine whether MIC data is on left or right channel.

Observations:

- Right-channel-only versions often showed `rpk` near zero or only occasional small values.
- Left-channel versions showed large values, often including `8388608`.
- `8388608 = 2^23`, which looks like a 24-bit full-scale or invalid value, not normal microphone audio.

Interpretation:

- Channel selection is still not fully settled from software logs alone.
- Left channel may contain the selected microphone channel, but it is polluted by invalid full-scale/floating samples.
- Right channel sometimes reports spikes, but not enough continuous audio to use as loopback.

### 6. Invalid Sample Rejection

Goal: avoid sending obvious invalid full-scale samples to the speaker.

Change:

- Added a threshold:
  - Initially `MIC_INVALID_MAGNITUDE 7000000U`
  - Later tightened to `MIC_INVALID_MAGNITUDE 5000000U`
  - Current record/playback firmware uses `MIC_INVALID_MAGNITUDE 500000U`
- Samples above this magnitude are ignored for output.

Result:

- Diagnostic output became safer, but K1 loopback was still not useful.
- Speaker loopback was then disabled for safety.

### 7. Low-Volume Live Loopback Trial

Goal: test whether live loopback could be enabled safely after microphone values became more reasonable.

Change:

```c
#define LOOPBACK_GAIN 32
#define LOOPBACK_OUTPUT_LIMIT 900
#define LOOPBACK_SPEAKER_ENABLE 1U
```

Result:

- The speaker still produced burst noise / explosive feedback.
- Logs showed `out:900`, meaning output was continuously hitting the low safety limit.
- Live loopback was disabled again:

```c
#define LOOPBACK_SPEAKER_ENABLE 0U
```

Conclusion:

- Polling live monitor is not a reliable acceptance test for Stage 7.
- The next safer test should be short record-then-playback, not live microphone-to-speaker monitoring.

### 8. Current Record-Then-Playback Mode

Current K1 behavior:

- K1 starts a short microphone recording.
- Recording length is `RECORD_SAMPLE_COUNT 8000U`, about 0.5 seconds at 16 kHz.
- After the buffer fills, firmware plays the captured samples through MAX98357A.
- Pressing K1 during recording or playback cancels it.
- Direct live speaker loopback remains disabled:
  - `LOOPBACK_SPEAKER_ENABLE 0U`
- When recording finishes, the firmware prints a short summary:

```text
Mic record done: playback start inv:<...> Lavg:<...> Lpk:<...> Ravg:<...> Rpk:<...> ovr:<...>
```

Meaning:

- `Lavg` / `Ravg`: average magnitude of valid samples during the recording.
- `Lpk` / `Rpk`: peak magnitude of valid samples during the recording.
- `ovr`: I2S2ext overrun events cleared during the recording.
- `inv`: invalid full-scale target-channel samples muted during recording.

## Key Log Findings

### Speaker path is good

K0 playback works consistently.

Conclusion:

- MAX98357A DIN/BCLK/LRC and speaker wiring are likely correct.
- The problem is not primarily the speaker output path.

### Microphone path is partially usable but not ready for live loopback

Repeated logs showed patterns such as:

```text
lpk:8388608
raw:8388608
out:6000
```

Interpretation:

- `8388608` is suspicious because it is exactly `2^23`.
- It is likely a full-scale invalid value, floating SD data, or frame-alignment/channel interpretation problem.
- Sending this to the speaker caused sharp noise or feedback.

Later, after pull-down and software OVR fixes, useful microphone behavior was observed:

```text
Mic L avg:4k-5k pk/raw:47k-50k
Mic L pk/raw:300k-4M during speech or knocking
Mic R avg:0 pk:0 raw:0
```

Interpretation:

- The left channel is currently the useful microphone channel.
- The right channel is mostly inactive.
- Full-scale spikes still occur intermittently, so live speaker loopback remains unsafe.

### Power was improved but did not solve it

MAX98357A was moved to independent 5V power.

Result:

- K0 still works.
- K1 microphone/loopback behavior still shows invalid samples and noise symptoms.

Conclusion:

- Power may still affect noise, but the main remaining issue is likely microphone SD/channel/data validity.

## Final Stage 7 Working Theory

The output path is functional (K0 proves it, test tone confirms record_buffer playback path).

The microphone I2S data can still contain noise spikes mixed with valid audio, but the DMA capture path and record-then-playback behavior are good enough for the next project stage. The signal processing chain (DC removal + invalid rejection + IIR LPF + noise gate) is sufficient to produce recognizable speech in buffered record-then-playback mode.

Remaining quality limits:

1. Breadboard wiring and power supply noise may still affect the mic signal.
2. MIC power decoupling may still be insufficient.
3. The IIR LPF attenuates high-frequency speech content along with noise.
4. Live speaker loopback remains unsafe/unnecessary compared with buffered capture/playback.

## Stage 8 Handoff

Stage 7 is complete for the project goal. The active work has moved to [stage8_audio_streaming.md](stage8_audio_streaming.md).

Current Stage 8 result:

- `PCM1` sends the existing 0.5 second `record_buffer` from STM32 to ESP32, then onward to PC.
- `AUD1` sends fixed-length 16 kHz mono signed 16-bit PCM from PC to ESP32, then to STM32 for MAX98357A playback.
- Long music playback is functional, with rare pop/noise events still under stability testing.

## Optional Audio Quality Tuning

Adjust `MIC_INVALID_MAGNITUDE`, `RECORD_GAIN`, `RECORD_NOISE_GATE`, and LPF alpha only if the transferred audio quality is not good enough for ASR testing.

## Hardware Improvements As Needed

- Ensure `0.1uF` decoupling capacitor close to MIC VDD.
- Keep `100kΩ` pull-down on MIC SD/DOUT.
- Consider moving from breadboard to soldered connections.

## Do Not Do Yet

- Do not enable live speaker loopback as the main test path; it is not required for the final assistant pipeline.
- Do not assume MAX98357A is broken while K0 playback works.
- Do not start ASR/NIM/TTS integration until Stage 8 long-play stability is acceptable.

## Current Conclusion

Stage 7 audio capture/playback validation is complete for the project goal. K0 proves the output path, and K1 proves buffered microphone capture followed by playback. Stage 8 now owns UART/TCP audio transfer and playback stability.
