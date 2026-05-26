# GPT-SoVITS V2 本地服務設定

Stage 9 TTS 由 GPT-SoVITS V2 本地 API 提供。下面記錄這次裝在本機（Windows 11 + RTX 5060 Laptop, 8GB）的步驟與遇到的坑，方便重建環境。

## 目錄佈局

```
STM32_final_project/
├─ koharu_GPTSoVITS模型/         # 使用者放的自訓模型 + 日文參考音
│  ├─ Koharu-e10.ckpt
│  ├─ Koharu_e15_s630.pth
│  └─ 参考音频/                  # 7 段 4~7s 日文 wav + 文本标注.txt
└─ GPT-SoVITS/                   # git clone --depth 1 RVC-Boss/GPT-SoVITS
   ├─ api_v2.py
   ├─ GPT_weights_v2/
   │  └─ Koharu-e10.ckpt         # 從 koharu_GPTSoVITS模型 複製
   ├─ SoVITS_weights_v2/
   │  └─ Koharu_e15_s630.pth     # 同上
   └─ GPT_SoVITS/
      ├─ configs/
      │  └─ tts_infer_koharu.yaml  # custom + v2 兩個 block 都指向 Koharu
      └─ pretrained_models/
         ├─ chinese-roberta-wwm-ext-large/
         ├─ chinese-hubert-base/
         ├─ gsv-v2final-pretrained/    # s1bert25hz-5kh + s2G2333k + s2D2333k
         └─ fast_langdetect/           # 空目錄即可，否則 ja/zh 偵測會炸
```

## 安裝步驟（一次性）

1. Clone repo（淺 clone 即可）：
   ```bash
   git clone --depth 1 https://github.com/RVC-Boss/GPT-SoVITS.git GPT-SoVITS
   ```

2. 在專案 `.venv` 裝 PyTorch CUDA 12.8（RTX 50 / Blackwell 必須）：
   ```bash
   .venv/Scripts/python.exe -m pip install --index-url https://download.pytorch.org/whl/cu128 torch torchaudio
   ```

3. 裝 GPT-SoVITS 主要 deps（已驗證在 `.venv` 與 faster-whisper 共存無衝突）：
   ```bash
   .venv/Scripts/python.exe -m pip install \
       scipy librosa==0.10.2 numba pytorch-lightning ffmpeg-python tqdm \
       cn2an pypinyin "transformers>=4.43,<=4.50" "peft<0.18.0" PyYAML \
       jieba_fast jieba split-lang "fast_langdetect>=0.3.1" wordsegment \
       rotary_embedding_torch "fastapi[standard]>=0.115.2" x_transformers \
       "torchmetrics<=1.5" "pydantic<=2.10.6" av tensorboard sentencepiece \
       chardet psutil pyopenjtalk g2p_en funasr==1.0.27 modelscope ToJyutping \
       g2pk2 ko_pron opencc onnxruntime-gpu "gradio<5" imageio-ffmpeg
   ```

4. 下載官方預訓練模型（v2 base，約 1.2 GB）：
   ```python
   from huggingface_hub import snapshot_download
   snapshot_download(
       repo_id="lj1995/GPT-SoVITS",
       local_dir="GPT-SoVITS/GPT_SoVITS/pretrained_models",
       allow_patterns=[
           "chinese-roberta-wwm-ext-large/*",
           "chinese-hubert-base/*",
           "gsv-v2final-pretrained/*",
       ],
   )
   ```

5. 複製 Koharu 自訓模型：
   ```bash
   mkdir -p GPT-SoVITS/GPT_weights_v2 GPT-SoVITS/SoVITS_weights_v2
   cp koharu_GPTSoVITS模型/Koharu-e10.ckpt   GPT-SoVITS/GPT_weights_v2/
   cp koharu_GPTSoVITS模型/Koharu_e15_s630.pth GPT-SoVITS/SoVITS_weights_v2/
   mkdir -p GPT-SoVITS/GPT_SoVITS/pretrained_models/fast_langdetect
   ```

6. 寫 `GPT-SoVITS/GPT_SoVITS/configs/tts_infer_koharu.yaml`（`custom:` 跟 `v2:` 兩塊都指向 Koharu 權重，`device: cuda`、`is_half: true`）。

## 啟動

```powershell
cd GPT-SoVITS
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"
..\.venv\Scripts\python.exe api_v2.py -a 127.0.0.1 -p 9880 -c GPT_SoVITS\configs\tts_infer_koharu.yaml
```

等 log 出現 `Uvicorn running on http://127.0.0.1:9880` 即可。

## 客製化過的兩處

1. **`GPT_SoVITS/TTS_infer_pack/TTS.py:773`**：原本 `torchaudio.load()` 在 torchaudio 2.9+ 會強制走 torchcodec，需要系統有 ffmpeg shared DLLs。Windows 上沒有，改成 `soundfile.read()` 讀 WAV，不需要任何系統依賴。
2. **`tts_infer_koharu.yaml`**：自建 config，`custom` 與 `v2` 兩塊都導向 Koharu 自訓模型。api_v2 啟動後有時會 rewrite `custom.version` 為 v1，但實際載入時印的是 `version: v2`，目前不影響推理。

## 已知坑

| 症狀 | 原因 | 解法 |
| :--- | :--- | :--- |
| `TorchCodec is required for load_with_torchcodec` | torchaudio 2.9+ 強制走 torchcodec，Windows 沒有 ffmpeg shared DLLs | 已 patch TTS.py 改用 soundfile |
| `Could not load libtorchcodec` | 同上，torchcodec 找不到 ffmpeg DLL | 同上 |
| `fast-langdetect: Cache directory not found` | 期望 `pretrained_models/fast_langdetect/` 目錄存在 | 建空目錄即可，模型會自動下載 |
| `'cp950' codec can't encode character` | Windows 預設 stdout 編碼非 UTF-8，無法輸出日文 | 啟動時設 `PYTHONUTF8=1` `PYTHONIOENCODING=utf-8` |
| VITS 載入時 `_IncompatibleKeys missing_keys=['enc_q...']` | `enc_q` 是 posterior encoder，只訓練用 | 推理不需要，正常警告 |

## 客戶端用法

`tools/tts_sovits.py` 提供：
- `synthesize_speech(text, out_path)`：用 `config.TTS_REF_AUDIO` 中文參考音（給 `assistant_server.py` 用）
- `create_japanese_client()`：用 `config.TTS_REF_AUDIO_JA`（Koharu Growup_4.wav）的日文 client（給 `translator_server.py` 用）

輸出 32 kHz mono WAV。`aud1_tcp_sender.py:load_wav_as_pcm()` 已內建線性重採樣，會自動轉成 16 kHz 餵給 STM32。

`SoVITSClient` 預設 `text_split_method="cut0"`（不切段）。API 預設值 `cut5` 會在每個逗號/句號處切段、分別合成再串接，短文本（兩三句）會明顯感覺到段落間有額外靜音、聽起來「斷斷續續」。實測 8s 日文：cut5 6.5s/合成 4.3s、cut0 5.8s/合成 1.8s。長段落（>100 字）若需要切段，建構 client 時傳 `text_split_method="cut3"`（依全形句號分段）。

當 `text_lang == "ja"`，`SoVITSClient.synthesize()` 會先用 `pyopenjtalk.run_frontend()` 拆詞，把每個係助詞「は」(pos='助詞', pos_group1='係助詞') 後面（若不是緊接標點）自動補一個全形逗號「、」。沒這個前處理時，「今日はいい」會被模型一口氣連讀、prosody 嚴重扭曲。pyopenjtalk 能正確區分名詞「葉」(ha) 與助詞「は」(wa)、也不會誤把感嘆詞「こんにちは」內部的 は 拆出來，所以這個 rewrite 不會有過度插入逗號的問題。
