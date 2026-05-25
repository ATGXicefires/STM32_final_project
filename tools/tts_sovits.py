"""GPT-SoVITS V2 TTS client module.

Sends HTTP requests to local GPT-SoVITS V2 API to generate responses.
"""

from __future__ import annotations

import json
import sys
import urllib.error
import urllib.request
from pathlib import Path

# Add parent directory to sys.path to resolve configuration
sys.path.append(str(Path(__file__).resolve().parent.parent))
from tools import config


class SoVITSClient:
    """Client for querying the local GPT-SoVITS V2 API server."""

    def __init__(self, api_url: str | None = None) -> None:
        self.api_url = api_url or config.TTS_API_URL
        self.ref_audio = config.TTS_REF_AUDIO
        self.prompt_text = config.TTS_PROMPT_TEXT
        self.prompt_lang = config.TTS_PROMPT_LANG
        self.text_lang = config.TTS_TEXT_LANG

    def synthesize(self, text: str, output_path: Path | str) -> bool:
        """Synthesizes text into a WAV file using the local GPT-SoVITS server.

        Args:
            text: The target text to synthesize.
            output_path: Path where the synthesized WAV will be written.

        Returns:
            True if synthesis succeeded, False otherwise.
        """
        payload = {
            "text": text,
            "text_lang": self.text_lang,
            "ref_audio_path": self.ref_audio,
            "prompt_text": self.prompt_text,
            "prompt_lang": self.prompt_lang,
        }

        headers = {"Content-Type": "application/json"}
        req_data = json.dumps(payload).encode("utf-8")

        print(f"[TTS] Querying GPT-SoVITS V2 API for text: '{text}'...")

        try:
            req = urllib.request.Request(
                self.api_url, data=req_data, headers=headers, method="POST"
            )
            with urllib.request.urlopen(req, timeout=30.0) as response:
                if response.status != 200:
                    print(f"[ERROR] TTS API server returned status: {response.status}")
                    return False

                audio_data = response.read()

            out_file = Path(output_path)
            out_file.parent.mkdir(parents=True, exist_ok=True)
            out_file.write_bytes(audio_data)

            print(f"[TTS] Saved synthesized audio to: {out_file} ({len(audio_data)} bytes)")
            return True

        except (urllib.error.URLError, OSError) as e:
            print(f"[ERROR] Failed to connect or receive from TTS server: {e}")
            return False


_client: SoVITSClient | None = None


def get_tts_client() -> SoVITSClient:
    """Retrieves or instantiates the global TTS client singleton."""
    global _client
    if _client is None:
        _client = SoVITSClient()
    return _client


def synthesize_speech(text: str, output_path: Path | str) -> bool:
    """Helper function to perform synthesis using the shared singleton client."""
    client = get_tts_client()
    return client.synthesize(text, output_path)
