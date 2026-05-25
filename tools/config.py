"""Project-wide configuration constants for the STM32 voice assistant.

Loads secrets from a .env file in the project root, then exposes all
network, ASR, TTS, LLM, and AUD1 settings as module-level constants.
"""

from __future__ import annotations

import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
TOOLS_DIR = BASE_DIR / "tools"
AUDIO_TEST_DIR = BASE_DIR / "audio_test"
MODELS_DIR = BASE_DIR / "models"

# Set Hugging Face cache directory to project local models directory
os.environ["HF_HOME"] = str(MODELS_DIR)


def load_env() -> None:
    env_path = BASE_DIR / ".env"
    if env_path.exists():
        with env_path.open("r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    key, val = line.split("=", 1)
                    # Strip whitespace and quotes
                    val = val.strip().strip("'\"")
                    os.environ[key.strip()] = val


load_env()

ESP32_HOST = "172.20.10.3"
AUD1_PORT = 5001
PCM1_PORT = 5000

ASR_MODEL_SIZE = "large-v3"
ASR_DEVICE = "cuda"
ASR_COMPUTE_TYPE = "float16"
ASR_LANGUAGE = "zh"
ASR_INITIAL_PROMPT = "以下是繁體中文的語音助理對話。"

TTS_API_URL = "http://127.0.0.1:9880/tts"
TTS_REF_AUDIO = str(AUDIO_TEST_DIR / "test.wav")
TTS_PROMPT_TEXT = "你好。"
TTS_PROMPT_LANG = "zh"
TTS_TEXT_LANG = "zh"

NIM_BASE_URL = "https://integrate.api.nvidia.com/v1"
NIM_MODEL = "google/gemma-4-31b-it"
NIM_API_KEY = os.environ.get("NVIDIA_API_KEY", "")

AUD1_SEQ = 99
AUD1_PREBUFFER_BYTES = 8192
AUD1_CHUNK_BYTES = 1024
AUD1_WINDOW_BYTES = 24576  # 24 KB in-flight limit; fills STM32's 64 KB ring buffer

SYSTEM_PROMPT = (
    "你是一個部署在嵌入式系統（STM32）上的智慧語音助理，名叫 NIM-Assistant。\n"
    "你的回答必須符合以下規則：\n"
    "1. 語言一律使用繁體中文（台灣習慣用語）。\n"
    "2. 回答要極度精簡，通常不超過兩句話或 50 個字，因為語音播報時間有限。\n"
    "3. 語氣要親切、口語化且實用。\n"
    "4. 避免使用 markdown 格式（如星號、井號、列表），直接輸出純文字。"
)
