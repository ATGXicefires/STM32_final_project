# NVIDIA NIM 模型選型筆記

> 記錄日期：2026-06-05
> 來源：查詢 `integrate.api.nvidia.com/v1/models` 模型清單 + 聯網查證 2026 各家 benchmark。
> 用途：本專案 PC 端 `assistant_server.py` / `translator_server.py` 走 ASR → NVIDIA NIM LLM → TTS，
> 這份筆記是給未來換模型 / 升級 pipeline 的參考。目前 LLM 用 `google/gemma-4-31b-it`。

## 一、純文字硬實力（開源權重，2026/06 排名）

NIM 清單全是開源權重模型；閉源前沿不在此清單、無法調用，故不列入比較。
AA 智力指數與 BenchLM 總分是兩把不同的尺，分數不可跨榜直接比，因此分兩欄列：

| 模型（NIM 調用 id） | AA 智力指數 v4.0 | BenchLM 總分 | 備註 |
|---|---|---|---|
| `moonshotai/kimi-k2.6` | 54 | 84 | **AA 開源第一**，中國旗艦 |
| `deepseek-ai/deepseek-v4-pro` | 52 | 87 | **BenchLM 開源第一**，逼近閉源前沿 |
| `z-ai/glm-5.1` | 51 | 83 | 754B 巨獸 |
| `qwen/qwen3.5-397b-a17b` | 未收錄 | 79 | 最強開源之一 |
| `nvidia/nemotron-3-ultra-550b-a55b` | 48 | 尚未收錄 | 美國開源智力最高，且最快 |
| `google/gemma-4-31b-it`（本專案目前用） | 39 | 65 | 小尺寸、夠用、便宜 |
| `nvidia/nemotron-3-super-120b-a12b` | 36 | — | Nemotron 中階 |
| `openai/gpt-oss-120b` | 33 | — | OpenAI 開源權重 |
| `mistralai/mistral-large-3-675b-instruct-2512` | — | — | 歐洲代表，落後中美旗艦 |

> 兩榜口徑不同：AA 指數上 kimi(54) > deepseek(52) > glm(51)；BenchLM 上 deepseek(87) > kimi(84) > glm(83) > qwen(79)。「—」表該榜尚未公布分數。
> 整體仍是中國開源旗艦（deepseek / kimi / glm / qwen）領先，美國（nemotron）、歐洲（mistral）開源落後一截。

### `nemotron-3-ultra-550b-a55b` 的定位
- 不是最聰明，但是**美國開源最快**的前沿模型：420 tok/s，比 GLM-5.1 快 5.9×、比 Kimi K2.6 快 4.8×、比 Qwen3.5 快 1.6×。
- **1M token 上下文**，Ruler@1M 拿 95%。
- 架構：Mamba-Transformer 混合 MoE（550B 總參數 / 55B 活躍）。
- 主打長時間運行的 agent。2026/06/04 發布。

## 二、多模態（看圖 / 影片 / 聲音）

| 類別 | 推薦（NIM 調用 id） | 說明 |
|---|---|---|
| **全模態（omni）** | **`nvidia/nemotron-3-nano-omni-30b-a3b-reasoning`** | 文字+圖+影片+**音訊**統一模型，單 GPU 可跑 |
| 視覺強（大） | `meta/llama-4-maverick-17b-128e-instruct` | 原生多模態 MoE，圖文理解強 |
| 視覺（中小） | `nvidia/nemotron-nano-12b-v2-vl`、`google/gemma-3-12b-it` | 單卡可跑的視覺語言模型 |
| OCR / 文件解析 | `deepseek-ai/deepseek-v4-flash`、`nvidia/nemotron-parse` | OCR 與文件結構解析，便宜 |

<!-- ## 三、★ 對本專案最關鍵的發現：`nemotron-3-nano-omni` 能原生吃音訊 -->

> **調用名稱（全稱）：`nvidia/nemotron-3-nano-omni-30b-a3b-reasoning`**

這顆模型跟本專案的 ASR→LLM 兩段式 pipeline 直接相關，**有機會合併成一段**：

- **原生支援音訊輸入**：文字/圖/影片/音訊四模態進同一個 30B 模型（3B 活躍 MoE）。
- **音訊編碼器用 Parakeet-TDT-0.6B-v2**（NVIDIA 自家 ASR 架構）：即時轉錄、帶時間對齊，不必整段 buffer 完才處理。
- 能做：語音轉錄、多語者/口音/吵雜背景的長音訊理解、聽完後摘要問答。
- 訓練資料：text+audio ~2.59 億筆、text+video+audio ~870 萬筆。
- **限制：只能輸入音訊，不能輸出音訊**，一律輸出文字 → 所以**不能取代 TTS（GPT-SoVITS 那段仍需保留）**。

### 中文與音訊規格（2026/06 查證）
- **語言**：支援 英 / **中** / 日 / 法 / 西 / 義 / 德，但**主力優化英文**，中文、日文屬次要涵蓋（夠用，但中文辨識率實測前別假設一定贏 faster-whisper）。
- **音訊輸入規格**：WAV / MP3、**最長 1 小時**、取樣率 **8kHz 以上**（ICS43434 收音常用 16kHz，OK）。
- **語音理解**：可處理多語者、口音、吵雜背景的長音訊。

### 換用前必驗的兩個風險（中文場景）
1. **中文 ASR 準確度** vs. 現在的 faster-whisper —— Whisper 中文調得不錯，Nemotron 中文非主力，需 A/B 實測。
2. **雲端往返延遲** —— 本地 ASR 改成把音訊丟上 NIM 雲端，網路來回可能反而比「本地 ASR + 雲端 LLM」慢。
   > 建議：同一段中文錄音分別走「faster-whisper 本地」與「nemotron-omni 雲端」，比**端到端延遲**與**中文辨識正確率**兩個數字再決定。
   > 本專案需求是「速度 > 智力」，所以延遲是主要決策依據。

> **2026-06-07 初測結論：暫不採用。** 實測 omni 的中文轉錄／翻譯效果不理想，pipeline 維持現有「faster-whisper 本地 ASR + 雲端 LLM」兩段式，不改走 omni 單段。此路線擱置，如日後模型版本更新可再評估。

### 對 pipeline 的潛在影響
- 目前：PCM1 → `faster-whisper`（本地 ASR）→ NIM LLM（gemma-4-31b）→ GPT-SoVITS → AUD1。
- 可能簡化：PCM1 → **NIM `nemotron-3-nano-omni`（音訊直接進，同時做理解＋回應）** → GPT-SoVITS → AUD1，
  省掉本地 faster-whisper 那一段（但要評估雲端往返延遲 vs. 本地 ASR）。
- TTS 段（GPT-SoVITS / Koharu）不受影響，仍需保留。

## 四、依用途快速推薦（皆在 NIM 清單內）

- 要最聰明 → `deepseek-ai/deepseek-v4-pro`（BenchLM 第一）或 `moonshotai/kimi-k2.6`（AA 第一）
- agent / 長任務 / 自架省算力 → `nvidia/nemotron-3-ultra-550b-a55b`
- 寫程式 → `qwen/qwen3-coder-480b-a35b-instruct` 或 `deepseek-ai/deepseek-v4-pro`
- 看圖看影片聽聲音（開源） → `nvidia/nemotron-3-nano-omni-30b-a3b-reasoning`
- 小尺寸便宜（本專案現況） → `google/gemma-4-31b-it`

## 來源
- Artificial Analysis — Nemotron 3 Ultra：https://artificialanalysis.ai/models/nvidia-nemotron-3-ultra-550b-a55b
- ChatForest — Nemotron 3 Ultra（美國最快前沿，仍落後中國）：https://chatforest.com/builders-log/nvidia-nemotron-3-ultra-550b-moe-open-weights-computex-2026/
- MarkTechPost — Nemotron 3 Ultra 發布：https://www.marktechpost.com/2026/06/04/nvidia-ai-releases-nemotron-3-ultra-an-open-550b-mixture-of-experts-hybrid-mamba-transformer-for-long-running-agents/
- LLM Leaderboard（Artificial Analysis）：https://artificialanalysis.ai/leaderboards/models
- BenchLM — 2026 最強開源 LLM：https://benchlm.ai/blog/posts/best-open-source-llm
- HuggingFace — Nemotron 3 Nano Omni（文件/音訊/影片）：https://huggingface.co/blog/nvidia/nemotron-3-nano-omni-multimodal-intelligence
- NVIDIA Developer Blog — Nemotron 3 Nano Omni：https://developer.nvidia.com/blog/nvidia-nemotron-3-nano-omni-powers-multimodal-agent-reasoning-in-a-single-efficient-open-model/
- NVIDIA Build 模型卡：https://build.nvidia.com/nvidia/nemotron-3-nano-omni-30b-a3b-reasoning/modelcard
- BuildFastWithAI — Nemotron 3 Nano Omni 評測：https://www.buildfastwithai.com/blogs/nvidia-nemotron-3-nano-omni-2026
