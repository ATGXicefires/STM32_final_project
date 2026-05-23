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
  - 錄音信號處理：DC removal → invalid rejection (`MIC_INVALID_MAGNITUDE 500000`) → IIR LPF (alpha≈1/8) → noise gate (`RECORD_NOISE_GATE 80`) → gain (`RECORD_GAIN 12`)。
  - Invalid samples 使用 filter decay 而非硬切 0。
  - STM32 使用 I2S2 full-duplex DMA circular buffer 維持 BCLK/WS 與音訊收送。
  - 錄音完成時輸出 `inv`、left/right avg/peak、`ovr` 診斷與前 16 筆 PCM dump。
  - `LOOPBACK_SPEAKER_ENABLE 0U`，不把 MIC 即時送到 MAX98357A；live loopback 非必要產品功能，只保留為診斷/延後項目。
  - `RECORD_TEST_TONE` 可切換為 400Hz 三角波測試。

### Current

- Stage 8: ESP32 audio streaming
  - `PCM1`：K1 錄完 0.5 秒後，把 `record_buffer` 以固定長度封包從 STM32 經 ESP32 TCP forward 到 PC。
  - `AUD1`：PC sender 把 16 kHz mono signed 16-bit WAV 轉成固定長度 PCM frame，經 ESP32 TCP server 轉 USART1 給 STM32 播放。
  - USART1 / ESP32 Serial2 目前為 `921600 8N1`，STM32 USART1 RX 使用 DMA circular buffer。
  - STM32 播放端使用 64 KB ring buffer，8 KB prebuffer，I2S DMA callback 從 ring 取樣；空 buffer 以短衰減補樣並記錄 underrun。
  - 長音樂已可正常播放；目前主要工作是追偶發爆音是否對應 `underrun`、`overflow`、Wi-Fi jitter 或硬體雜訊。

### Remaining

- Stage 9: ASR -> NIM -> TTS -> playback
- Stage 10: OLED and UI status
- Stage 11: Stability test and final report

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
