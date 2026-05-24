# STM32 Final Project Progress

Last updated: 2026-05-24

這份文件只記錄目前狀態與下一步。長篇排查紀錄已移到 [docs/stage6_microphone_debug.md](docs/stage6_microphone_debug.md) 與 [docs/stage7_loopback_debug.md](docs/stage7_loopback_debug.md)，燒錄流程已移到 [docs/flash_usb_dfu.md](docs/flash_usb_dfu.md)。

## Stage Status

| Stage | Status | Result |
| :--- | :--- | :--- |
| 1. GPIO / LED | Done | D2=`PA6`、D3=`PA7`，active-low，交替閃爍正常 |
| 2. Button input | Done | K0=`PE4`、K1=`PE3`，pull-up，按下為 low |
| 3. USART1 USB-TTL | Done | `PA9/PA10` echo 測試通過；Stage 8 音訊橋接目前使用 `921600 8N1` |
| 4. ESP32 UART PING/PONG | Done | STM32 sends `PING`，ESP32 replies `PONG` |
| 5. MAX98357A I2S playback | Done | I2S2 TX 播放 beep 與 embedded WAV clip 通過 |
| 6. ICS43434 I2S mic input | Done | 麥克風 I2S 資料會隨聲音變化；Stage 7 診斷後目前以 left channel 為有效資料來源 |
| 7. Audio capture/playback validation | Done | K0 播放通過；K1 先錄 0.5 秒再回放，語音可辨識；I2S DMA 已驗證，OVR 不再是 Stage 8 blocker；live loopback 非必要並維持關閉 |
| 8. ESP32 audio streaming | Done | K1 hold-to-record PCM1 串流回 PC 完成；`AUD1` 長音樂播放可用；PCM1 多 chunk 佇列、ESP32 32KB UART RX buffer 均已驗證 |
| 9. ASR -> NIM -> TTS -> playback | Current | PC server、ASR/TTS 串接與回放 |
| 10. OLED / UI | Todo | 顯示音量、連線、狀態與對話文字 |
| 11. Final validation | Todo | 長時間穩定度測試與期末報告 |

## Current Firmware Behavior

- 保留 GPIO/Button 測試行為，K0 會觸發播放內建測試音效。
- 透過 USART1 與 USB CDC 輸出 debug log。
- USART1 / ESP32 Serial2 已升到 `921600 8N1`；週期性 PING/PONG 已關閉，避免干擾音訊 log。
- K1 hold-to-record 串流錄音：按住 K1 期間持續將 0.5 秒 chunk（8000 samples）透過 USART1 以 PCM1 格式送往 ESP32，放開時送出尾端不足 0.5 秒的 partial chunk。STM32 使用 2-slot PCM TX queue，確保佇列不會因為 TX 忙碌而靜默丟棄 chunk。
- 錄音信號處理鏈：invalid sample rejection（`MIC_INVALID_MAGNITUDE 500000`）→ DC removal → gain（`RECORD_GAIN 12`）→ IIR LPF（alpha≈1/8）→ noise gate（`RECORD_NOISE_GATE 80`）。
- Invalid sample 使用 filter decay 而非硬切 0，避免突然靜音造成 pop。
- I2S2 full-duplex DMA circular buffer 會持續維持 RX/TX 音訊資料流與 BCLK/WS。
- K0 播放 Koharu login 測試語音：`audio_test/BA_V_Koharu_Login_1.ogg` 已解碼成 `audio_test/test.wav`，再轉成內建 `audio_clip`，用來驗證 MAX98357A 與喇叭輸出路徑。
- 麥克風目前主要在 left channel 有有效資料。
- 錄完時 USB CDC 會輸出 `Mic record done: playback start inv ... Lavg ... Lpk ... Ravg ... Rpk ... ovr ...` 與前 16 筆 PCM 值的 dump。
- live speaker loopback 已關閉：`LOOPBACK_SPEAKER_ENABLE 0U`，避免尖叫、爆音與聲學回授；它不是最終語音助理流程的必要功能。
- 內建 `RECORD_TEST_TONE` 開關可切換為 400Hz 三角波測試音，驗證 playback path。
- `PCM1`：K1 放開後，每個 PCM1 frame 攜帶實際錄音樣本數（最後 chunk 為變動長度），PC 端 `pcm_tcp_receiver.py` 將所有 frame 串接成一個 WAV 檔，總長度等同按鍵時間。
- `AUD1`：PC sender 送固定長度 16 kHz mono signed 16-bit PCM frame 到 ESP32 TCP port `5001`，ESP32 分 chunk 轉 USART1，STM32 以 64 KB ring buffer 串到 I2S DMA 播放。
- AUD1 v1 只支援固定 `sample_count`；`seq` 只做 log/debug 關聯，不做 reorder 或 retransmit。
- 長音樂片段已可正常播放；目前偶發爆音的首要觀察點是 STM32 log 的 `AUD level`、`underrun`、`overflow`。

## Hardware Notes

### STM32F407VET6

- D1 是 power LED，常亮是正常現象。
- D2=`PA6`、D3=`PA7`，都是 active-low。
- K0=`PE4`、K1=`PE3`，都是 pull-up input。
- 目前這塊板子的 SWD 連不上，正式燒錄流程改走 USB DFU。

### USART1 / ESP32

- STM32 USART1：`PA9` TX、`PA10` RX；Stage 8 audio bridge 使用 `921600 8N1`。
- ESP32 bridge firmware 使用 `Serial2`，同樣為 `921600 8N1`。
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

Stage 8 完成。STM32 K1 hold-to-record PCM1 串流、ESP32 UART RX overrun 修正（32 KB buffer）、STM32 PCM TX 2-slot queue 均已驗證可用。`AUD1` 長音樂播放正常。

下一步為 Stage 9：PC 端 server 銜接 ASR/NIM/TTS，完成語音助理完整對話流程。

PC 端測試工具以專案 `.venv` 為準；不要用系統 Python 3.14 直接跑 `tools/aud1_tcp_sender.py`，因為該腳本目前依賴已移除的 `audioop` 標準庫。

## Next Work

1. Stage 9 prototype：PC local server 監聽 `PCM1` WAV，接 ASR，把一段固定 TTS WAV 用 `AUD1` 回放，驗證全鏈路基本通。
2. Stage 9 NIM integration：ASR 輸出接 NIM LLM，LLM 輸出接 TTS，TTS 輸出以 `AUD1` 回放。
3. Stage 9 latency / error handling：量測端到端延遲，加 timeout 與失敗重試。
4. Stage 10 OLED / UI：顯示音量、連線、狀態與對話文字。
5. Stage 11 final validation：長時間穩定度測試與期末報告。
