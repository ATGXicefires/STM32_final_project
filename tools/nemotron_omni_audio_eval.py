#!/usr/bin/env python3
"""Benchmark nemotron-3-nano-omni audio transcription against the NIM cloud.

Sends one audio file to NVIDIA NIM's omni model via the OpenAI-compatible
endpoint and prints the transcription plus end-to-end latency. Used to gauge
whether the model's native audio input can replace local faster-whisper ASR.
See docs/nvidia_nim_model_notes.md for the decision context.
"""

from __future__ import annotations

import argparse
import base64
import os
import re
import sys
import time
from pathlib import Path

# Add parent directory to sys.path to resolve configuration
sys.path.append(str(Path(__file__).resolve().parent.parent))
from tools import config

try:
    from openai import OpenAI
except ImportError:
    OpenAI = None

OMNI_MODEL = "nvidia/nemotron-3-nano-omni-30b-a3b-reasoning"

# Nemotron reasoning models gate their chain-of-thought on this system line;
# "off" keeps the reply to just the transcription instead of thinking tokens.
SYSTEM_PROMPT = "detailed thinking off"

DEFAULT_PROMPT = "將輸入音訊翻譯成繁體中文"

# Model accepts WAV / MP3 / FLAC; map extension to the data-URI mime subtype.
AUDIO_MIME = {".wav": "audio/wav", ".mp3": "audio/mpeg", ".flac": "audio/flac"}

_THINK_BLOCK = re.compile(r"<think>.*?</think>", re.DOTALL)


def build_audio_data_uri(path: Path) -> str:
    """Reads an audio file and wraps it as a base64 data URI for audio_url."""
    suffix = path.suffix.lower()
    mime = AUDIO_MIME.get(suffix)
    if mime is None:
        raise ValueError(f"unsupported audio format '{suffix}'; expected one of {sorted(AUDIO_MIME)}")
    b64 = base64.b64encode(path.read_bytes()).decode()
    return f"data:{mime};base64,{b64}"


def strip_thinking(text: str) -> str:
    """Removes any <think>...</think> reasoning span and surrounding whitespace."""
    return _THINK_BLOCK.sub("", text).strip()


def transcribe_audio_omni(
    client: OpenAI,
    audio_path: Path,
    prompt: str,
    model: str,
    max_tokens: int,
) -> tuple[str, float, object | None]:
    """Sends the audio to the omni model and returns (text, elapsed_s, usage)."""
    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {
            "role": "user",
            "content": [
                {"type": "text", "text": prompt},
                {"type": "audio_url", "audio_url": {"url": build_audio_data_uri(audio_path)}},
            ],
        },
    ]

    start = time.monotonic()
    completion = client.chat.completions.create(
        model=model,
        messages=messages,
        temperature=0.0,
        max_tokens=max_tokens,
        stream=False,
    )
    elapsed = time.monotonic() - start

    message = completion.choices[0].message
    # A reasoning model may leave content empty and put text in reasoning_content.
    text = message.content or getattr(message, "reasoning_content", "") or ""
    return strip_thinking(text), elapsed, getattr(completion, "usage", None)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--audio",
        type=Path,
        default=config.AUDIO_TEST_DIR / "test.wav",
        help="path to the audio file (WAV/MP3/FLAC) to transcribe",
    )
    parser.add_argument("--prompt", default=DEFAULT_PROMPT, help="instruction sent alongside the audio")
    parser.add_argument("--model", default=OMNI_MODEL, help="NIM model id to query")
    parser.add_argument("--max-tokens", type=int, default=512, help="max tokens in the reply")
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    if OpenAI is None:
        print("[OMNI] openai package is missing. Please run 'pip install openai'.")
        sys.exit(1)

    if not args.audio.is_file():
        print(f"[OMNI] audio file not found: {args.audio}")
        sys.exit(1)

    key = os.environ.get("NVIDIA_API_KEY") or config.NIM_API_KEY
    if not key or key == "your_nvidia_api_key_here":
        print(
            "[OMNI] NVIDIA API Key is missing. Please set NVIDIA_API_KEY in a .env file "
            "in the project root directory, or set the environment variable."
        )
        sys.exit(1)

    # Audio uploads are larger and slower than text, so widen nim_llm's 30s timeout.
    client = OpenAI(base_url=config.NIM_BASE_URL, api_key=key, timeout=60.0)

    size_kb = args.audio.stat().st_size / 1024
    print(f"[OMNI] audio : {args.audio} ({size_kb:.1f} KB)")
    print(f"[OMNI] model : {args.model}")
    print("[OMNI] sending to NIM cloud...")

    try:
        text, elapsed, usage = transcribe_audio_omni(
            client, args.audio, args.prompt, args.model, args.max_tokens
        )
    except Exception as e:
        print(f"[OMNI] request failed: {e}")
        sys.exit(1)

    print(f"[OMNI] latency: {elapsed:.2f} s")
    if usage is not None:
        print(f"[OMNI] tokens : prompt={usage.prompt_tokens} completion={usage.completion_tokens}")
    print(f"[OMNI] result : {text!r}")


if __name__ == "__main__":
    main()
