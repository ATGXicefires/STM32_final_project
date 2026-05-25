# Python Coding Style

以 `tools/pcm_tcp_receiver.py` 與 `tools/aud1_tcp_sender.py` 為規範基準。

---

## 檔案結構順序

```
1. #!/usr/bin/env python3          ← 僅供可直接執行的腳本
2. """Module docstring."""         ← 首行摘要，空行，詳細說明
3. from __future__ import annotations
4. 標準庫 imports（字母序）
5. 第三方套件 imports
6. 本地模組 imports（若需要 sys.path 調整，附說明 comment）
7. 常數（ALL_CAPS）
8. Helper functions / private classes（_Prefix）
9. parse_args() -> argparse.Namespace   ← 僅供 CLI 腳本
10. main() -> None
11. if __name__ == "__main__": main()
```

---

## 各規則說明

### Shebang
- 可執行腳本（`assistant_server.py`、`pcm_tcp_receiver.py`、`aud1_tcp_sender.py`、`wav_to_stm32_pcm.py`）加 `#!/usr/bin/env python3`
- 純 library 模組（`asr_local.py`、`tts_sovits.py`、`nim_llm.py`、`config.py`）不加

### Module Docstring
- 所有檔案都要有，包含 library 模組
- 格式：首行摘要 → 空行 → 補充說明（可選）

```python
"""GPT-SoVITS V2 TTS client module.

Sends HTTP requests to local GPT-SoVITS V2 API to generate responses.
"""
```

### Imports
- `from __future__ import annotations` 必須是第一個 import
- 標準庫按字母排序
- 需要 `sys.path.append()` workaround 時，附 comment 說明原因，緊接在 `import sys` 之後

### 型別標注
- 使用現代語法：`str | None`、`list[bytes]`、`tuple[str, float]`
- 不用舊式：`Optional[str]`、`List[bytes]`、`Tuple[str, float]`
- 所有函式都要標注參數與回傳值型別

### 常數
- 全大寫：`HEADER_SIZE = 24`、`MAGIC = b"PCM1"`
- 定義在 imports 之後、函式之前
- 有非明顯意義時加 inline comment：`# 24 KB in-flight limit; fills STM32's 64 KB ring buffer`

### 類別與函式命名
- 公開類別：`ClassName`
- 私有類別（僅模組內使用）：`_ClassName`
- 函式：`snake_case`

### Singleton 模式
```python
_engine: MyEngine | None = None

def get_engine() -> MyEngine:
    global _engine
    if _engine is None:
        _engine = MyEngine()
    return _engine
```

### argparse（僅 CLI 腳本）
- 獨立的 `parse_args() -> argparse.Namespace` 函式
- `main()` 只呼叫 `parse_args()` 並傳入 worker 函式

### Exception 處理
- 捕捉具體型別，不用裸 `Exception`
- Socket：`(ConnectionError, OSError)`
- HTTP：`(urllib.error.URLError, OSError)`
- 第三方 library 初始化可接受較廣的範圍，但須有明確的 fallback 邏輯

---

## Comment 規則

**原則：只寫 WHY，不寫 WHAT。**

### 保留（WHY 型）
只在以下情況加 comment：
- **非明顯 API 行為**：`# transcribe returns a generator — iterate to trigger actual transcription`
- **技術限制或 workaround**：`# Add parent directory to sys.path to resolve configuration`
- **硬體/業務上下文**：`# Connection closed or timed out → K1 released, recording finished`
- **常數用途不直覺**：`# 24 KB in-flight limit; fills STM32's 64 KB ring buffer`
- **非明顯初始化行為**：`# Warm up local Whisper model`（預熱動作與函式名稱語意有差距）

### 刪除（WHAT 型）
下列這些在參考檔案中完全不存在：

| 反模式 | 原因 |
|--------|------|
| `# Paths`、`# Network Settings` 等 section 分隔 | 常數名稱本身已說明分類 |
| `# Singleton engine instance` | 變數宣告本身即說明 |
| `# Initialize NIM LLM`、`# Local file paths` | 程式碼自說明 |
| `# 1. ASR`、`# 2. LLM` 等步驟編號 | print 日誌已說明各步驟 |
| `# Execute pipeline`、`# Use pacing settings from config.py` | 函式呼叫與參數名稱自說明 |
| 函式前的重複說明：`# Helper function to load ...` | docstring 已涵蓋 |
