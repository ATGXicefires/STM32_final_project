"""Local ASR (Speech-to-Text) module using faster-whisper.

Provides GPU-accelerated audio transcription interface for the voice assistant.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

# Add parent directory to sys.path to resolve configuration
sys.path.append(str(Path(__file__).resolve().parent.parent))
from tools import config

try:
    from faster_whisper import WhisperModel
except ImportError:
    WhisperModel = None


class LocalASREngine:
    """Wrapper class around faster-whisper model for local audio transcription."""

    def __init__(
        self,
        model_size: str | None = None,
        device: str | None = None,
        compute_type: str | None = None,
    ) -> None:
        if WhisperModel is None:
            raise ImportError("faster-whisper package is missing. Cannot initialize ASR engine.")

        self.model_size = model_size or config.ASR_MODEL_SIZE
        self.device = device or config.ASR_DEVICE
        self.compute_type = compute_type or config.ASR_COMPUTE_TYPE

        print(f"[ASR] Loading Whisper model '{self.model_size}' on '{self.device}'...")

        try:
            self.model = WhisperModel(
                self.model_size,
                device=self.device,
                compute_type=self.compute_type,
            )
        except Exception as e:
            print(f"[ASR] Failed to load on '{self.device}': {e}")
            if self.device != "cpu":
                print("[ASR] Falling back to 'cpu' with 'int8' compute...")
                try:
                    self.model = WhisperModel(
                        self.model_size,
                        device="cpu",
                        compute_type="int8",
                    )
                    self.device = "cpu"
                    self.compute_type = "int8"
                except Exception as cpu_err:
                    print(f"[ASR] Failed to load on CPU as well: {cpu_err}")
                    raise cpu_err from e
            else:
                raise e

        print(f"[ASR] Model loaded successfully on '{self.device}'")

    def transcribe(
        self,
        wav_path: Path | str,
        language: str | None = None,
        initial_prompt: str | None = None,
    ) -> tuple[str, float]:
        """Transcribes a 16kHz mono WAV file to text.

        Returns:
            Tuple of (transcribed_text, elapsed_time_seconds)
        """
        lang = language or config.ASR_LANGUAGE
        prompt = initial_prompt or config.ASR_INITIAL_PROMPT
        resolved_path = str(Path(wav_path).resolve())

        start_time = time.monotonic()

        # transcribe returns a generator (segments) and transcription info
        segments, _ = self.model.transcribe(
            resolved_path,
            language=lang,
            initial_prompt=prompt,
            beam_size=5,
        )

        # Force iteration over generator to perform actual transcription
        text_list = [segment.text for segment in segments]
        full_text = "".join(text_list).strip()
        elapsed = time.monotonic() - start_time

        return full_text, elapsed


_engine: LocalASREngine | None = None


def get_asr_engine() -> LocalASREngine:
    """Retrieves or instantiates the global ASR engine singleton."""
    global _engine
    if _engine is None:
        _engine = LocalASREngine()
    return _engine


def transcribe_audio(
    wav_path: Path | str,
    language: str | None = None,
    initial_prompt: str | None = None,
) -> tuple[str, float]:
    """Helper function to perform transcription using the shared singleton engine."""
    engine = get_asr_engine()
    return engine.transcribe(wav_path, language, initial_prompt)
