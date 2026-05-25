# Project Process

這份文件是專案的主流程入口。進度只看 [progress.md](progress.md)，燒錄細節看 [docs/flash_usb_dfu.md](docs/flash_usb_dfu.md)，麥克風排查紀錄看 [docs/stage6_microphone_debug.md](docs/stage6_microphone_debug.md)，Stage 7 capture/playback 排查看 [docs/stage7_loopback_debug.md](docs/stage7_loopback_debug.md)，Stage 8 音訊串流看 [docs/stage8_audio_streaming.md](docs/stage8_audio_streaming.md)。

## Daily Development Flow

(給 AI 參考 : STM32CubeIDE 裝在 D槽)
1. 修改 STM32 firmware 或 ESP32 bridge。
2. 在 STM32CubeIDE 按 `Ctrl+B` 編譯 STM32 專案。
3. 確認產物存在：`NIM_Assistant_F407/Debug/NIM_Assistant_F407.elf`。
4. 依照 USB DFU 流程燒錄 STM32。
5. BOOT0 接回 GND，按 Reset。
6. 透過 USB CDC 或 USB-TTL 看 log，確認當前 Stage 的驗收條件。
7. 把測試結果更新到 [progress.md](progress.md)。

## STM32 Flash Flow

目前這塊 STM32F407 板子的 SWD 通道不可用，所以不要用 CubeIDE 的 Debug/Run 燒錄。使用 USB DFU：

1. BOOT0 接 3V3。
2. 按 Reset。
3. 用 USB 線接板子自己的 USB 孔。
4. 執行 `NIM_Assistant_F407/flash_usb.bat`。
5. 燒錄成功後 BOOT0 接回 GND。
6. 再按 Reset，程式從 Flash 啟動。

若 USB DFU 沒出現，先檢查 BOOT0 是否真的接到 3V3、USB 線是否接在板子 USB 孔、是否有按 Reset。

## Stage Roadmap

### Completed

- Stage 1: GPIO / LED
- Stage 2: Button input
- Stage 3: USART1 USB-TTL
- Stage 4: ESP32 UART PING/PONG
- Stage 5: MAX98357A I2S playback
- Stage 6: ICS43434 I2S microphone input
- Stage 7: Audio capture/playback validation
  - K0 plays the embedded Koharu login `audio_clip` generated from `audio_test/test.wav` after decoding `audio_test/BA_V_Koharu_Login_1.ogg`.
  - K1 records 0.5 seconds of microphone audio into RAM, then plays the captured buffer through MAX98357A. **語音已可辨識。**
   - 錄音信號處理：invalid rejection (`MIC_INVALID_MAGNITUDE 500000`) → DC removal → gain (`RECORD_GAIN 12`) → IIR LPF (alpha≈1/8) → noise gate (`RECORD_NOISE_GATE 80`)。
  - Invalid samples 使用 filter decay 而非硬切 0。
  - STM32 使用 I2S2 full-duplex DMA circular buffer 維持 BCLK/WS 與音訊收送。
  - 錄音完成時輸出 `inv`、left/right avg/peak、`ovr` 診斷與前 16 筆 PCM dump。
  - `LOOPBACK_SPEAKER_ENABLE 0U`，不把 MIC 即時送到 MAX98357A；live loopback 非必要產品功能，只保留為診斷/延後項目。
  - `RECORD_TEST_TONE` 可切換為 400Hz 三角波測試。

- Stage 8: ESP32 audio streaming
    - `PCM1`：K1 按住期間以 0.5 秒分段將錄音以 PCM1 封包（含魔術字、序號、長度與校驗和）傳送，並由 ESP32 TCP 持久連線轉發至 PC 拼接。
    - `AUD1`：PC 使用 Sliding Window 流量控制（24 KB）將 WAV 串流經 ESP32 TCP 伺服器傳送，STM32 搭配 64 KB Ring Buffer 與淡出機制，流暢播放長音樂。
    - 串流穩定性通過驗收，爆音問題已由淡出修正解決，ESP32 32 KB 緩衝區防溢出亦驗證成功。

### Current

- Stage 9: ASR -> NIM -> TTS -> playback
  - 建立 PC 端整合服務，接收 STM32 K1 錄音的 `PCM1` WAV 檔。
  - 將 WAV 輸入 ASR（如 Whisper）轉為文字。
  - 輸入 NVIDIA NIM 進行大語言模型對話。
  - 大模型輸出文字輸入 TTS 產生 16 kHz Mono WAV。
  - 將 TTS 語音透過 `AUD1` 主動推送回 STM32 播放，完成閉環對話。

### Remaining

- Stage 10: OLED and UI status
- Stage 11: Stability test and final report

## Current Execution Plan

### Phase A: Stage 8 Stabilization

Goal: prove that the audio transport is stable enough before adding ASR/NIM/TTS.

1. Baseline hardware check:
   - K0 plays the embedded Koharu login clip.
   - K1 records 0.5 seconds, then plays the captured buffer locally.
   - K1 also emits a valid `PCM1` frame that PC saves as `stage8_received.wav`.
2. AUD1 long-play check:
   - Send 5 second, 10 second, and 30 second WAV files through `AUD1`.
   - Capture STM32 USB CDC logs during playback.
   - Accept only if playback has no sustained `underrun` or `overflow`.
3. AUD1 reset check:
   - Send three short WAV files consecutively.
   - Confirm each new frame starts cleanly and previous ring-buffer state does not leak into the next playback.
4. Pop/noise diagnosis:
   - If pop/noise appears with `underrun` or `AUD level` near zero, tune PC pacing, prebuffer, Wi-Fi stability, or ESP32 UART scheduling.
   - If pop/noise appears without `underrun` or `overflow`, inspect hardware power, common ground, speaker wiring, ESP32 GPIO4 PWM, and breadboard contact.

### Phase B: Stage 9 Prototype

Goal: reuse the verified Stage 8 transport before building more UI.

1. Keep STM32 firmware unchanged unless Stage 8 exposes a transport bug.
2. Build a PC local server that receives the existing `PCM1` WAV path.
3. Run ASR on the received 0.5 second recording.
4. Send a fixed test response through NIM or a stub while validating the transport.
5. Convert the response to a short 16 kHz mono WAV and play it back through existing `AUD1`.

### Phase C: Stage 9 Integration

Goal: turn the prototype into the actual assistant loop.

1. Replace the stub with real NIM prompt/response handling.
2. Add TTS generation and normalize output to the `AUD1` format.
3. Log per-step latency: `PCM1 receive`, ASR, NIM, TTS, `AUD1 playback`.
4. Add basic retry/failure messages for network, ASR, NIM, and TTS failures.

### Phase D: Stage 10-11 Finish

Goal: make the demo explainable and repeatable.

1. Add OLED/UI status only after Stage 9 audio loop works.
2. Display compact states such as idle, recording, sending, thinking, speaking, and error.
3. Run repeated demo cycles and one longer stability session.
4. Freeze final wiring, commands, known limitations, and demo script for the report.

## Stage 8 Test Checklist

1. Start the PC receiver:
   - `.\.venv\Scripts\python.exe tools\pcm_tcp_receiver.py --host 0.0.0.0 --port 5000 --output stage8_received.wav`
2. Press K1 and verify:
   - Local playback is recognizable.
   - ESP32 forwards one `PCM1` frame.
   - `stage8_received.wav` is 16 kHz mono, 0.5 seconds, 8000 samples.
3. Send AUD1 playback with the sender's built-in default WAV/host/port:
   - `.\.venv\Scripts\python.exe tools\aud1_tcp_sender.py`
   - If the ESP32 IP is different from the built-in default, add `--host <ESP32_IP>`.
4. During AUD1 playback, watch STM32 USB CDC:
   - `AUD level:<n>`
   - `underrun:<n>`
   - `overflow:<n>`
   - `AUD RX payload done ... OK`
   - `AUD playback done ...`
5. Record whether any audible pop/noise aligns with `underrun`, `overflow`, or low ring level.

## Stage 7 Test Checklist

1. 接線確認：
   - ICS43434 `SCK` -> `PB13`
   - ICS43434 `WS` -> `PB12`
   - ICS43434 `SD/DOUT` -> `PB14`
   - ICS43434 `VDD` -> `3.3V`
   - ICS43434 `GND` -> `GND`
   - MAX98357A `BCLK` -> `PB13`
   - MAX98357A `LRC/WS` -> `PB12`
   - MAX98357A `DIN` -> `PB15`
   - MAX98357A `GND` -> common ground
2. 開機後確認 USB CDC 出現 `Mic record ready: K1 records 0.5s then plays back`。
3. 按 K0，確認從 MAX98357A/喇叭聽到測試音效。
4. 按 K1，對麥克風說話或拍手。
5. 等待 `Mic record done: playback start ...` 與 `REC[0..15]: ...`。
6. 聽回放是否有可辨識的人聲/敲擊聲。
7. 錄音或回放中再按 K1，確認取消功能。

## Rules For Updating Docs

- `README.md` 只放專案入口與快速資訊。
- `process.md` 放開發流程與 Stage 驗收流程。
- `progress.md` 放目前狀態，不放長篇排查日記。
- `docs/flash_usb_dfu.md` 放燒錄與 SWD/DFU 問題。
- `docs/stage6_microphone_debug.md` 放 Stage 6 技術排查細節。
- `docs/stage7_loopback_debug.md` 放 Stage 7 capture/playback、接線、K0/K1、OVR 與 live loopback 是否需要的結論。
- `docs/stage8_audio_streaming.md` 放 Stage 8 PCM1/AUD1、TCP bridge、測試命令與穩定化紀錄。
