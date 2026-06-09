# Stage 9 程式碼審查報告（2026-06-10）

針對 Stage 9 當前狀態（commit `cf56786`）的全專案審查：`tools/` Python pipeline、`ESP32_UART_Bridge_Test.ino`、`NIM_Assistant_F407/Core/Src/main.c`、`tests/`，並對照 [stage9_translator_test.md](stage9_translator_test.md) 的延遲優化計畫。

**修復狀態（2026-06-10）：** Python 端五項已依下方優先序修畢，`pytest tests/`（16 項）全數通過：

- #1 快贏：`pcm1_server.py` session timeout `5.0` → `1.0` 秒（牆鐘實測待硬體驗收）。
- #2：`nim_llm.py` history 裁切到最後 `HISTORY_MAX_TURNS = 10` 輪，`get_response` / `get_response_stream` 皆套用，補 `tests/test_nim_llm_history.py`。
- #3：`tts_sovits.py` 分離 `HTTPError` 並印出 error body、4xx 不重試、移除死碼分支，補 `tests/test_tts_sovits_errors.py`。
- #5：`aud1_tcp_sender.py` CLI `--host` / `--port` 預設改用 `config.ESP32_HOST` / `config.AUD1_PORT`。
- #6：`pcm1_server.py` checksum 失敗改照收該 frame 並印警告。

韌體類（#1 乾淨解 `PCMEND`、#4 barge-in、#8）依建議**未動**，留待另一輪集中處理與實機驗證。

整體評價：架構乾淨——PCM1/AUD1 協定兩端對稱、STM32 的 IRQ 保護正確、PC 端該有的 retry/timeout 都有。以下多為邊角案例與量測盲區，依嚴重度排序。

---

## 高優先

### 1.「放開 K1 → pipeline 啟動」有 ~3 秒的隱形延遲地板（量測盲區）

**現象鏈路：**

- STM32 放開 K1 送出最後一個 PCM1 frame 後，協定上**沒有任何結束訊號**。
- PC 端 `pcm1_server.py` 的 `serve()` 靠「TCP 連線關閉」判定錄音結束（`conn.settimeout(5.0)` + `read_exact` 收到 EOF/timeout 即 break）。
- 但連線是 ESP32 端關的：`ESP32_UART_Bridge_Test.ino` 的 `PCM_SESSION_TIMEOUT_MS = 3000`，要在最後一筆 forward 後**閒置 3 秒**才 `pcmClient.stop()`。

**結果：** 每輪對話固定多等 ~3 秒才開始跑 ASR。而 `[TIMING]` 是從 pipeline 開始計時，**完全量不到這 3 秒**——Phase 1 在 ASR 端省下的零點幾秒，被這裡整碗吃掉。

**建議修法（兩個層級）：**

| 層級 | 做法 | 效果 | 代價 |
| :--- | :--- | :--- | :--- |
| 零韌體快贏 | `pcm1_server.py` 的 `conn.settimeout(5.0)` 改 ~1.0 秒，把「收不到下一個 frame」直接當 session 結束。frame 間隔穩定 0.5 秒，1 秒有餘裕；ESP32 下次錄音 `ensurePcmClient()` 會自動重連 | 立省 ~2 秒 | Wi-Fi 卡頓 >1 秒會誤判提前結束（可實測調整） |
| 乾淨解 | STM32 `Test_StopMicStreamRecord` 在最後 frame 後送一行 `PCMEND\r\n`；ESP32 `processStm32Line()` 看到就 `pcmClient.stop()` | 延遲趨近 0 | 動到韌體，需重燒錄實機驗證（與 Phase 1「不動韌體」原則衝突，可排 Phase 2） |

另建議：延遲量測應補一個「放開 K1 → `STARTING DIALOGUE PIPELINE` 印出」的牆鐘時間，才看得到這段。

### 2. LLM 對話歷史無上限成長（`tools/nim_llm.py`）

`NIMLLMEngine.history` 每輪 append、永不修剪（`get_response` 與 `get_response_stream` 皆同）。assistant 長 session 下 token 數線性成長 → LLM 延遲與費用逐輪上升，最終撞模型 context 上限。

**建議：** append 後裁切到最後 N 輪（例如 10 輪 = 20 則訊息）。translator 模式每句 `reset_session()` 不受影響。

### 3. TTS 的 4xx 錯誤 body 被丟掉、且盲目重試（`tools/tts_sovits.py`）

`urllib.request.urlopen` 對 4xx/5xx 會 raise `HTTPError`（為 `URLError` 子類），因此：

- `response.status != 200` 分支是**死碼**，永遠不會執行。
- 400 錯誤被 `except (urllib.error.URLError, OSError)` 當連線錯誤捕捉 → 只印 `HTTP Error 400` 就重試一次。但 [stage9_translator_test.md](stage9_translator_test.md) 故障表的 cp950 / TorchCodec 診斷全靠 400 的 body，目前程式根本印不出來（只能去翻 GPT-SoVITS server console）。

**建議：** 單獨 `except urllib.error.HTTPError as e:` 印 `e.read()` 內容；4xx 不重試（重送同樣的壞請求沒有意義）；移除死碼分支。

### 4. STM32 播放中按 K1（barge-in）會讓 UART 解析器失步（`NIM_Assistant_F407/Core/Src/main.c`）

`Test_StartMicStreamRecord` 呼叫 `Test_ResetAudStream` 把 `aud_payload_bytes` / `aud_payload_received` 清零，但 `Test_ProcessUartRxByte` 內的 `static parse_mode` 仍停在 `UART_PARSE_AUD_PAYLOAD`。後果：

1. 下一個 byte 進來，`aud_payload_received(1) >= aud_payload_bytes(0)` 成立 → 立刻誤判 payload 結束、退回 LINE 模式。
2. 此時 PC 端 `send_aud1` 仍在送完整檔（ESP32 照常 UART forward + ACK），**剩餘的整段 AUD1 payload 全被當文字行亂解析**，期間可能誤觸發假 magic 比對。
3. PC pipeline 也被佔住到整個音檔送完為止。

**建議：** 要嘛在 `aud_rx_active` 期間按 K1 時，把剩餘 payload 轉進 `UART_PARSE_AUD_DROP` 吃完再開錄音；要嘛播放中直接忽略 K1。`parse_mode` 是函式內 static，修正需把狀態提出來或加一個 abort 介面。

---

## 中優先

### 5. `aud1_tcp_sender.py` CLI 預設值沒吃 `.env`

上次 refactor 把 `ESP32_HOST` 移進 `.env`，但這支 CLI 的 `DEFAULT_ESP32_HOST = "172.20.10.3"` / `DEFAULT_ESP32_PORT = 5001` 仍是寫死的，與 `config.ESP32_HOST` 脫鉤。IP 換了之後，照 docs 直接跑 CLI 會連錯。建議 argparse default 改用 `config.ESP32_HOST` / `config.AUD1_PORT`。

### 6. PCM1 checksum 失敗靜默丟 0.5 秒音訊

- PC 端 `pcm1_server.py`：checksum mismatch → `continue` 跳過該 frame，WAV 中間直接缺 0.5 秒 → ASR 拿到斷裂語音，品質劣化卻無感。
- ESP32 端 `finishPcmFrame()`：checksum FAIL 整包丟、PC 端完全不知道有這包。

TCP 段（ESP32→PC）本身保證完整，mismatch 幾乎只會來自 UART 段損壞。**建議：** 至少把該 frame 照收（有雜訊的資料勝過無聲斷層），或累計 mismatch 計數印出來讓問題可見。

### 7. 純 Python 逐 sample 處理可換 numpy（`aud1_tcp_sender.py::load_wav_as_pcm`）

對 GPT-SoVITS 輸出（32 kHz，5 秒 ≈ 16 萬 samples）做 list comprehension 解包、立體聲混音、線性重採樣、音量縮放，每輪 pipeline 都重跑一次，約百 ms 級。faster-whisper 已帶 numpy 依賴，向量化可壓到 ms 級。改動小、優先度不高。

### 8. STM32 韌體小問題

- `pcm_tx_dropped`：queue 滿丟資料時有累計，但**從未輸出到任何 log**，發生了無從得知。建議附在 `PCM TX done` 訊息。
- record-done 訊息 `"Mic record done: playback start ..."`：`record_playing` 在現行流程根本不會被設，「playback start」是舊階段遺留，訊息誤導。
- 全檔 `Test_` 前綴是測試階段遺留，現在已是正式韌體（純命名問題，改不改皆可）。

---

## 低優先 / 風格

- **`run_pipeline` 幾乎複製**：`assistant_server.py` 與 `translator_server.py` 的 pipeline 函式約 90% 相同（差別只在 `reset_session()` 與印出的名稱）。可抽共用函式，但依 CLAUDE.md「單次使用不過度抽象」原則列為可選。
- **magic 偵測邊角案例**（STM32 與 ESP32 同款 state machine）：資料開頭字元重複時（如 `AAUD1`、`PPCM1`），flush 前綴後不會用當前 byte 重啟 magic 比對，該 frame 會漏接。實務上 frame 前必有換行、機率極低，知道即可。
- **`pcm1_server.serve` 的 KeyboardInterrupt**：只有 `accept()` 階段有接，pipeline 執行中按 Ctrl+C 會直接 traceback 噴出。
- **`config.py` 在 import 時設 `HF_HOME`**：成立的前提是「`config` 比 `faster_whisper` 先被 import」，目前各模組 import 順序剛好對，但這個隱性順序很脆弱——之後挪動 import 就會悄悄改抓使用者全域 cache。
- **`get_response_stream` 目前無人呼叫**：已知是 Phase 2 句子級串流的預備（docs 有記載），非問題，僅確認。

---

## 若日後要修的建議優先序

1. `pcm1_server.py`：session 結束 timeout `5.0` → `1.0` 秒（#1 快贏，純 PC 端、一行）。
2. `nim_llm.py`：history 裁切到最後 10 輪（#2）。
3. `tts_sovits.py`：分離 `HTTPError`、印 error body、4xx 不重試、移除死碼（#3）。
4. `aud1_tcp_sender.py`：CLI 預設改用 `config.ESP32_HOST` / `config.AUD1_PORT`（#5）。
5. （可選）`pcm1_server.py` checksum 失敗改照收 + 警告（#6）。
6. 韌體類（#1 乾淨解 `PCMEND`、#4 barge-in、#8）需重燒錄與實機驗證，建議另開一輪集中處理。

Python 項目修完後在 `.venv` 跑 `pytest tests/`，並對 history 裁切與 TTS 錯誤處理各補單元測試；#1 以「放開 K1 → pipeline 開始」牆鐘時間實測驗收（預期 ~3s → ~1s）。
