# Stage 9 Translator 全鏈路測試流程

`tools/translator_server.py` 是 Stage 9 的「中→日翻譯」驗證入口，跑通整條 PCM1 → ASR → LLM → TTS → AUD1 串。翻譯版的 LLM 輸出可預期、長度穩定，比起自由對話更適合做延遲基準與斷句驗證。

GPT-SoVITS V2 本地服務的安裝細節見 [gpt_sovits_setup.md](gpt_sovits_setup.md)，本文只講「裝完之後怎麼測」。

---

## 訊號流

```
[STM32] K1 按住錄音
    │ I2S DMA 從 ICS43434 拉 16 kHz mono PCM
    │ DC removal → gain → LPF → noise gate
    │ 每 0.5s 一個 chunk 包成 PCM1 frame
    ↓
[USART1, 921600 8N1]
    ↓
[ESP32] Serial2 → TCP 5000 (PCM1 接力)
    ↓
[PC : tools/translator_server.py] listen on :5000
    │ ① 收齊一輪 PCM1 frame → 拼成 received.wav
    │ ② [ASR] faster-whisper large-v3-turbo (CUDA fp16, lang=zh, beam=1, VAD) → 中文
    │ ③ [LLM] NVIDIA NIM google/gemma-4-31b-it
    │       system prompt = TRANSLATION_SYSTEM_PROMPT (中→日)
    │       reset_session() 每輪 → 不被歷史污染
    │       → 日文
    │ ④ [TTS] tts_sovits.SoVITSClient (Japanese)
    │       a. _insert_pauses_after_particle_wa() 把助詞「は」後補逗號
    │       b. POST http://127.0.0.1:9880/tts
    │          ref_audio = Koharu CH0205_Growup_4.wav
    │          text_split_method = cut0 (不切段)
    │       ← 32 kHz mono WAV → response_ja.wav
    │ ⑤ [AUD1] aud1_tcp_sender.send_aud1()
    │       load_wav_as_pcm 自動 32k→16k 重採樣
    │       TCP → ESP32 :5001
    ↓
[GPT-SoVITS api_v2.py, 127.0.0.1:9880] (同台 PC 獨立 process)
    ↑ 經 HTTP 回吐 wav bytes

[ESP32] TCP 5001 收 AUD1 frame
    │ prebuffer 8 KB → 切 1 KB chunk → USART1 推給 STM32
    │ window 24 KB in-flight (避免 STM32 64 KB ring buffer 爆)
    ↓
[USART1]
    ↓
[STM32] AUD ring buffer (64 KB) → I2S2 DMA → MAX98357A → 喇叭
```

---

## 前置檢查

1. `.env` 有 `NVIDIA_API_KEY=...`（在專案根目錄，不是 `tools/` 下）。
2. 確認 ESP32 連到網路後拿到的 IP 跟 `tools/config.py:ESP32_HOST` 一致；不一致就改 config 或固定 IP。
3. STM32 + ESP32 兩端韌體都是 Stage 8 之後的版本，不用改任何東西。

## 啟動順序

需要兩個 terminal，**TTS server 一定要先起**，否則 translator 啟動時的 TTS 預熱（dummy 合成）會卡 timeout。

### Terminal 1：GPT-SoVITS V2

```powershell
cd GPT-SoVITS
$env:PYTHONUTF8 = "1"          # 否則 cp950 不能輸出日文
$env:PYTHONIOENCODING = "utf-8"
..\.venv\Scripts\python.exe api_v2.py -a 127.0.0.1 -p 9880 -c GPT_SoVITS\configs\tts_infer_koharu.yaml
```

就緒指標：log 出現 `Uvicorn running on http://127.0.0.1:9880`（首次 10–15s，GPU warmup + BERT/HuBERT loading）。

### Terminal 2：Translator server

```powershell
.\.venv\Scripts\python.exe tools\translator_server.py
```

就緒指標：log 依序出現
```
Initializing Engines...
Warming up TTS...
[TTS] Saved synthesized audio to: ...warmup.wav ...
Initialization complete.
Translator server listening on port 5000 (PCM1)...
```
（首次跑會自動下載 `large-v3-turbo` ct2 權重到 `models/`；faster-whisper warmup 約 5–10s，TTS 預熱再 +1 句合成時間。）

## 操作

1. 按住 STM32 的 K1。
2. 對麥克風講一句中文（建議短句，例：「今天天氣很好」、「我想吃拉麵」、「謝謝你」）。
3. 放開 K1。
4. 看 Terminal 2 的 log：

```
Connected from <ip> (New recording session)
Recording session finished. Received N bytes of PCM.
[ASR] Finished in X.XXs. Result: '今天天氣很好'
[LLM] Finished in X.XXs. Reply: '今日はいい天気ですね'
[TTS] Querying GPT-SoVITS V2 API for text: '今日は、いい天気ですね' ...
         ↑「は」後面自動補了逗號
[TTS] Finished in X.XXs.
[AUD1] Streaming playback completed in X.XXs.
[TIMING] ASR=... LLM=... TTS=... AUD1=... total=...
```

5. STM32 喇叭吐出日文。

## 不接 STM32 的純 PC 鏈路檢查

只想驗證 ASR + LLM + TTS（跳過 PCM1 / AUD1 兩端 TCP）：

```powershell
$env:PYTHONUTF8 = "1"
.\.venv\Scripts\python.exe -c @"
from pathlib import Path
from tools import asr_local, nim_llm, tts_sovits
text, _ = asr_local.transcribe_audio(Path('received.wav'))
print('ASR:', text)
engine = nim_llm.create_translator_engine()
reply = engine.get_response(text)
print('LLM:', reply)
client = tts_sovits.create_japanese_client()
client.synthesize(reply, 'response_ja.wav')
"@
```

用 Stage 8 留下的 `received.wav` 當輸入；輸出 `response_ja.wav` 可直接用任何播放器播。

## 延遲基準（RTX 5060 Laptop, fp16）

下表是 **Phase 1 延遲優化「之前」** 的實測（whisper large-v3, beam=5, 無 VAD, 無 TTS 預熱），一句短中文（~10 字）→ 一句日文（~12 字）：

| 階段 | 時間 |
| :--- | :--- |
| ASR (whisper large-v3) | 0.4–0.8s |
| LLM (NIM cloud, 含網路 RTT) | 1.5–3.0s |
| TTS (GPT-SoVITS V2, cut0) | 1.5–2.5s |
| AUD1 串流 (~5s 音檔) | 約等同音檔長度 |
| **總計（不含 AUD1 串流時間）** | **3.5–6s** |

> ⚠️ **此表為優化前數據，尚未以 Phase 1 設定重新量測。** 套用 turbo + beam=1 + VAD + TTS 預熱後，請用下節步驟跑數輪，把新數據填回這裡。

## 延遲優化現況（Phase 0–2）

不改協定、不動韌體的純 PC 端優化，目標壓低「放開 K1 到聽到第一個字」的等待。

**Phase 0 量測（已落地）**：`assistant_server` 與 `translator_server` 的 pipeline 結尾都會印
`[TIMING] ASR=.. LLM=.. TTS=.. AUD1=.. total=..`。跑數輪取平均當基準。

**Phase 1 設定級快贏（已落地，待實機量測）**：
- ASR：`config.ASR_MODEL_SIZE = large-v3-turbo`、`asr_local.transcribe` 用 `beam_size=1` + `vad_filter=True` + `condition_on_previous_text=False`。
  - 驗證：同一段錄音比對 `[TIMING] ASR=` 與轉錄文字；若中文辨識明顯退化，把 `config.py` 改回 `large-v3` 即可回退。
- TTS 預熱：兩端 server 啟動送一句 dummy 合成吃掉首輪冷啟（產物 `warmup.wav`，已 gitignore）。
  - 驗證：比較第一輪與第二輪的 `[TIMING] TTS=`，差距應收斂。
- LLM 串流前置：`nim_llm.get_response_stream()` 已備好（`stream=True`），目前 pipeline 尚未啟用，留給 Phase 2。
- Session 結束提早判定：`pcm1_server.py` 的 frame timeout `5.0` → `1.0` 秒，不再等 ESP32 的 3 秒閒置斷線才開跑 pipeline（詳見 [code_review_stage9.md](code_review_stage9.md) #1）。
  - 驗證：實測「放開 K1 → `STARTING DIALOGUE PIPELINE` 印出」的牆鐘時間，預期 ~3s → ~1s；若 Wi-Fi 卡頓造成錄音被提前截斷，可調回較大值。
- **顯式 EOS frame（已落地，需重燒兩端韌體）**：K1 放開時 STM32 多送一個 zero-payload PCM1 frame（`payload_bytes = 0`）當 end-of-session 標記，ESP32 轉發後立即關閉 TCP session，PC 收到後立刻開跑 pipeline——上一條的 1.0s timeout 等待整段消失（協定細節見 [stage8_audio_streaming.md](stage8_audio_streaming.md)）。
  - 相容性：舊韌體沒有 EOS frame 仍靠 1.0s timeout fallback 正常運作，只是慢 ~1 秒。
  - 驗證：Terminal 2 log 出現 `End-of-session frame received (seq=N)`、ESP32 serial log 出現 `PCM EOS, closing session`；「放開 K1 → `STARTING ... PIPELINE`」應 <0.2s。

**Phase 2 句子級串流（尚未實作）**：規劃把「LLM 全文 → TTS 全文 → 播放」改為「LLM 邊吐邊切句 → 逐句 TTS → 逐句 AUD1」三段重疊，大幅縮短 time-to-first-audio。
- 最大風險：連續多個 AUD1 frame 之間，STM32 64 KB ring buffer 可能 underrun 造成句間 gap/爆音，需實機驗（對照 STM32 log 的 `AUD level` / `underrun` / `overflow`）。
- 預計加 `config.PIPELINE_STREAMING` 開關可一鍵退回整段模式做 A/B。
- **待 Phase 0+1 取得實機數據後再決定是否投入。**

## 常見故障

| 現象 | 原因 | 處置 |
| :--- | :--- | :--- |
| `[TTS]` 卡 30s 後 timeout | GPT-SoVITS server 沒起 / 還在 warmup | Terminal 1 等到 `Uvicorn running on` 再試 |
| LLM 回 401 / API key 錯 | `.env` 沒設 `NVIDIA_API_KEY` | 補上後重啟 translator |
| AUD1 階段卡住 / timeout | ESP32 IP 變了 / `config.ESP32_HOST` 過期 | 改 config 或 ping 確認 |
| TTS 回 400，body 提到 `cp950` | GPT-SoVITS server 沒帶 `PYTHONUTF8=1` 啟動 | 用上面 Terminal 1 那串環境變數重啟 |
| TTS 回 400，body 提到 `TorchCodec` / `libtorchcodec` | `GPT_SoVITS/TTS_infer_pack/TTS.py` 的 soundfile patch 沒套上 | 見 [gpt_sovits_setup.md](gpt_sovits_setup.md) 客製化段落 |
| 日文聽起來「斷斷續續」 | `text_split_method` 沒設 cut0 | 看 `SoVITSClient.__init__` 預設值是否被改過 |
| 「今日はいい」連讀糊在一起 | 助詞「は」後處理失效 | 確認 `_insert_pauses_after_particle_wa` 仍會被呼叫（看 log 印出來的 text 是否含「、」） |

## 音量調整

喇叭太大聲改 `tools/config.py:AUD1_VOLUME_SCALE`（線性縮放，預設 0.6 ≈ -4.4 dB）。0.4–0.7 是常用範圍；數值會在 AUD1 送出前對 int16 PCM 做縮放，clipping 由 `load_wav_as_pcm` 內建保護。CLI 用 `aud1_tcp_sender.py` 直接送檔可加 `--volume 0.5` override。

## 切回對話模式

需要從翻譯切回自由對話：把 Terminal 2 換成 `tools/assistant_server.py` 即可，兩支共用同一個 GPT-SoVITS server（但 assistant 用中文參考音 `audio_test/test.wav`，translator 用 Koharu 日文參考音；client 端會自動帶對的參數）。
