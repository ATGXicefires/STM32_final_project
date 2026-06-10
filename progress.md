# STM32 Final Project Progress

Last updated: 2026-06-10

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
| 9. ASR -> NIM -> TTS -> playback | In Progress | PC server 骨架完成；GPT-SoVITS V2 本地 API 已可用（Koharu 自訓模型，日文 5s/句）；`translator_server.py` 全鏈路（PCM1→ASR→LLM(中→日)→TTS(ja)→AUD1）已接好，待實機測試 |
| 10. OLED / UI | Todo | 顯示音量、連線、狀態與對話文字 |
| 11. Final validation | In Progress | 長時間穩定度測試待做；期末 PPT 初稿完成（`report/期末報告_NIM_Assistant.pptx`，由 `report/build_ppt.js` 生成，report/ 不進版控） |

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

Stage 9 PC 端 server 骨架 + GPT-SoVITS V2 本地 TTS 服務都已就緒。

- `tools/assistant_server.py`：PCM1 → ASR → NIM → TTS(zh) → AUD1 對話模式。
- `tools/translator_server.py`：PCM1 → ASR → NIM(中→日) → TTS(ja, Koharu) → AUD1 翻譯驗證模式，方便比對 LLM 輸出穩定性與延遲。
- GPT-SoVITS V2 跑在 `127.0.0.1:9880`，配置在 `GPT-SoVITS/GPT_SoVITS/configs/tts_infer_koharu.yaml`，使用自訓 Koharu 模型（`GPT-SoVITS/GPT_weights_v2/Koharu-e10.ckpt` + `GPT-SoVITS/SoVITS_weights_v2/Koharu_e15_s630.pth`）。
- 一次性 8 秒日文合成端到端約 5 秒（RTX 5060 Laptop，fp16）。輸出 32 kHz mono；AUD1 sender 端 (`aud1_tcp_sender.py`) 已內建 32k→16k 重採樣。

PC 端工具以專案 `.venv` 為準；GPT-SoVITS 與 client 同享一個 venv（PyTorch 2.11 + CUDA 12.8）。GPT-SoVITS 安裝細節見 [docs/gpt_sovits_setup.md](docs/gpt_sovits_setup.md)；翻譯模式全鏈路測試步驟見 [docs/stage9_translator_test.md](docs/stage9_translator_test.md)。Stage 8 獨立測試工具（`aud1_tcp_sender.py`、`pcm_tcp_receiver.py`）仍只需標準庫。

## Next Work

1. Stage 9 實機測試：照 [docs/stage9_translator_test.md](docs/stage9_translator_test.md) 啟動兩端 server，按 K1 錄音驗證全鏈路。
2. Stage 9 latency / error handling：LLM(30s timeout + retry)、TTS(retry) 已加；延遲優化 Phase 0+1 已落地（見下節），**baseline 與優化後數據待實機量測**。
3. Stage 9 延遲優化 Phase 2（句子級串流 pipeline）：待 Phase 0+1 取得實機數據後再決定是否投入；需實機驗 AUD1 句間銜接。
4. Stage 10 OLED / UI：顯示音量、連線、狀態與對話文字。
5. Stage 11 final validation：長時間穩定度測試與期末報告。

### PC 端工具整理（已完成）

- 兩個 server 的 PCM1 收音迴圈抽成共用 [tools/pcm1_server.py](tools/pcm1_server.py)（`serve(on_recording, received_wav, port)` 回呼），移除 `translator_server` 對 `assistant_server` 的反向依賴。
- `ESP32_HOST` / `AUD1_PORT` / `PCM1_PORT` 改由 `.env` 讀取（見 [.env.example](.env.example)），換網路不用改 code；標準庫獨立工具 `aud1_tcp_sender.py` / `pcm_tcp_receiver.py` 維持不依賴 config。
- 新增 [tests/test_protocol.py](tests/test_protocol.py)：PCM1/AUD1 協定純函式測試，`.venv\Scripts\activate; pytest -q`（8 passed）。

### 延遲優化 Phase 0 + 1（已落地，待實機量測）

不改協定、不動韌體，純 PC 端調整，目標壓低 time-to-first-audio：

- **Phase 0 量測**：`assistant_server.run_pipeline` 補上與 `translator_server` 一致的 `[TIMING] ASR/LLM/TTS/AUD1/total` 逐段計時。實機跑數輪後把 baseline 數據填回本節。
- **Phase 1.1 ASR 加速**：`ASR_MODEL_SIZE` 由 `large-v3` 改 `large-v3-turbo`（首次執行會自動下載 turbo ct2 權重）；`asr_local.transcribe` 改 `beam_size=1` + `vad_filter=True` + `condition_on_previous_text=False`。
- **Phase 1.2 TTS 預熱**：兩個 server 啟動時送一句 dummy 合成（assistant=「你好。」、translator=「テスト。」）吃掉首輪 GPU/pyopenjtalk 冷啟；失敗不影響啟動。產物 `warmup.wav` 已 gitignore。
- **Phase 1.3 LLM 串流前置**：`nim_llm` 新增 `get_response_stream()`（`stream=True`，逐 delta yield，結束更新 history），保留原 `get_response`；新增離線 mock 測試 [tests/test_nim_llm_stream.py](tests/test_nim_llm_stream.py)。Phase 2 才會實際使用串流。
- 測試現況：`pytest -q` 10 passed。
- **待辦**：實機量 Phase 0 baseline 與 Phase 1 優化後數據（ASR turbo 準度/速度、首輪 TTS 冷啟改善），確認後再決定是否做 Phase 2 句子級串流。
