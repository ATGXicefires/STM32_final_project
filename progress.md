# STM32 Final Project Progress

Last updated: 2026-05-23

這份文件只記錄目前狀態與下一步。長篇排查紀錄已移到 [docs/stage6_microphone_debug.md](docs/stage6_microphone_debug.md) 與 [docs/stage7_loopback_debug.md](docs/stage7_loopback_debug.md)，燒錄流程已移到 [docs/flash_usb_dfu.md](docs/flash_usb_dfu.md)。

## Stage Status

| Stage | Status | Result |
| :--- | :--- | :--- |
| 1. GPIO / LED | Done | D2=`PA6`、D3=`PA7`，active-low，交替閃爍正常 |
| 2. Button input | Done | K0=`PE4`、K1=`PE3`，pull-up，按下為 low |
| 3. USART1 USB-TTL | Done | `PA9/PA10`，115200 8N1，echo 測試通過 |
| 4. ESP32 UART PING/PONG | Done | STM32 sends `PING`，ESP32 replies `PONG` |
| 5. MAX98357A I2S playback | Done | I2S2 TX 播放 beep 與 embedded WAV clip 通過 |
| 6. ICS43434 I2S mic input | Done | 麥克風 I2S 資料會隨聲音變化；Stage 7 診斷後目前以 left channel 為有效資料來源 |
| 7. Audio loopback | In progress | K0 播放通過；K1 record-then-playback 已可辨識語音，含 IIR LPF + noise gate；live loopback 暫停 |
| 8. ESP32 audio streaming | Todo | 定義 UART audio packet，轉 TCP 到 PC |
| 9. ASR -> NIM -> TTS -> playback | Todo | PC server、ASR/TTS 串接與回放 |
| 10. OLED / UI | Todo | 顯示音量、連線、狀態與對話文字 |
| 11. Final validation | Todo | 長時間穩定度測試與期末報告 |

## Current Firmware Behavior

- 保留 GPIO/Button 測試行為，K0 會觸發播放內建測試音效。
- 透過 USART1 與 USB CDC 輸出 debug log。
- USART1 目前用於接收 ESP32 的 `PONG` 回覆。
- 每秒送出 `PING` 給 ESP32，收到 `PONG` 時輸出 `ESP32 PONG OK`。
- K1 啟動 0.5 秒麥克風錄音（`RECORD_SAMPLE_COUNT 8000`），錄到 RAM buffer 後播放，**已可辨識語音**。
- 錄音信號處理鏈：DC removal → invalid sample rejection（`MIC_INVALID_MAGNITUDE 500000`）→ IIR LPF（alpha≈1/8）→ noise gate（`RECORD_NOISE_GATE 80`）→ gain（`RECORD_GAIN 12`）。
- Invalid sample 使用 filter decay 而非硬切 0，避免突然靜音造成 pop。
- I2S2 master TX 會持續餵 `SPI_DR`，維持 BCLK/WS，避免麥克風失去 clock。
- K0 播放 Koharu login 測試語音：`audio_test/BA_V_Koharu_Login_1.ogg` 已解碼成 `audio_test/test.wav`，再轉成內建 `audio_clip`，用來驗證 MAX98357A 與喇叭輸出路徑。
- 麥克風目前主要在 left channel 有有效資料。
- 錄完時 USB CDC 會輸出 `Mic record done: playback start inv ... Lavg ... Lpk ... Ravg ... Rpk ... ovr ...` 與前 16 筆 PCM 值的 dump。
- live speaker loopback 已關閉：`LOOPBACK_SPEAKER_ENABLE 0U`，避免尖叫、爆音與聲學回授。
- 內建 `RECORD_TEST_TONE` 開關可切換為 400Hz 三角波測試音，驗證 playback path。

## Hardware Notes

### STM32F407VET6

- D1 是 power LED，常亮是正常現象。
- D2=`PA6`、D3=`PA7`，都是 active-low。
- K0=`PE4`、K1=`PE3`，都是 pull-up input。
- 目前這塊板子的 SWD 連不上，正式燒錄流程改走 USB DFU。

### USART1 / ESP32

- STM32 USART1：`PA9` TX、`PA10` RX，115200 8N1。
- ESP32 測試 firmware 使用 `Serial2`，115200 8N1。
- USB-TTL 標籤可能不準，實務上以 echo 測試成功的 TX/RX 交叉接法為準。

### I2S2 Audio

- `PB13` = I2S2_CK / BCLK。
- `PB12` = I2S2_WS / LRCLK。
- `PB14` = I2S2ext_SD / microphone SD/DOUT。
- `PB15` = I2S2_SD / MAX98357A DIN。
- MAX98357A 不需要 MCLK。
- ICS43434 / INMP441 datasheet 標示 `L/R=GND` 為 left channel、`L/R=3.3V` 為 right channel；目前實測有效資料主要在 left channel。
- MIC `SD/DOUT` 已建議加 `100kΩ` pull-down 到 GND，降低 inactive channel 浮動。

## Current Checkpoint

Stage 7 record-then-playback 已可辨識語音。K0 證明輸出路徑正常；K1 錄音經過 LPF、noise gate 與 invalid decay 處理後，回放可聽出人聲與環境音，但仍有背景雜訊。firmware 已切到 I2S2 full-duplex DMA circular buffer，下一步是在硬體上確認 K1 重複錄音時 `ovr:0` 且不再出現整段純雜訊。

## Next Work

1. 微調錄音參數（LPF alpha、noise gate threshold、invalid magnitude）以改善信噪比。
2. 實測 I2S RX/TX DMA circular buffer，確認 `ovr:0`、K0 播放正常、K1 錄音不再隨機變純雜訊。
3. 等 MIC 資料穩定後，定義 STM32 → ESP32 的 raw PCM packet 格式。
4. 實作 PC TCP server，準備 ASR/NIM/TTS 全鏈路測試。
