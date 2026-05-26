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


def _insert_pauses_after_particle_wa(text: str) -> str:
    """Adds a 、after every topic-marker は so GPT-SoVITS doesn't glue it to the next word.

    Without this, sequences like 「今日はいい」 are synthesized as one breathless unit
    and the prosody around the particle gets mangled.
    """
    # pyopenjtalk warms up on first call (~100 ms); only pay the cost when actually needed
    import pyopenjtalk

    features = pyopenjtalk.run_frontend(text)
    out: list[str] = []
    for i, f in enumerate(features):
        surface = f.get("string", "")
        out.append(surface)
        if (
            surface == "は"
            and f.get("pos") == "助詞"
            and f.get("pos_group1") == "係助詞"
            and i + 1 < len(features)
            and features[i + 1].get("pos") != "記号"
        ):
            out.append("、")
    return "".join(out)


class SoVITSClient:
    """Client for querying the local GPT-SoVITS V2 API server."""

    def __init__(
        self,
        api_url: str | None = None,
        ref_audio: str | None = None,
        prompt_text: str | None = None,
        prompt_lang: str | None = None,
        text_lang: str | None = None,
        text_split_method: str = "cut0",
        speed_factor: float = 1.0,
    ) -> None:
        self.api_url = api_url or config.TTS_API_URL
        self.ref_audio = ref_audio or config.TTS_REF_AUDIO
        self.prompt_text = prompt_text or config.TTS_PROMPT_TEXT
        self.prompt_lang = prompt_lang or config.TTS_PROMPT_LANG
        self.text_lang = text_lang or config.TTS_TEXT_LANG
        # cut0 = no segmentation; cut5 (the API default) inserts pauses at every punctuation,
        # which sounds disjointed for short LLM outputs.
        self.text_split_method = text_split_method
        self.speed_factor = speed_factor

    def synthesize(self, text: str, output_path: Path | str) -> bool:
        """Synthesizes text into a WAV file using the local GPT-SoVITS server.

        Args:
            text: The target text to synthesize.
            output_path: Path where the synthesized WAV will be written.

        Returns:
            True if synthesis succeeded, False otherwise.
        """
        if self.text_lang == "ja":
            text = _insert_pauses_after_particle_wa(text)

        payload = {
            "text": text,
            "text_lang": self.text_lang,
            "ref_audio_path": self.ref_audio,
            "prompt_text": self.prompt_text,
            "prompt_lang": self.prompt_lang,
            "text_split_method": self.text_split_method,
            "speed_factor": self.speed_factor,
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


def create_japanese_client() -> SoVITSClient:
    """Builds a fresh client wired with the Koharu Japanese reference voice.

    Not a singleton — keeps the assistant (zh) and translator (ja) clients isolated.
    """
    return SoVITSClient(
        ref_audio=config.TTS_REF_AUDIO_JA,
        prompt_text=config.TTS_PROMPT_TEXT_JA,
        prompt_lang=config.TTS_PROMPT_LANG_JA,
        text_lang=config.TTS_TEXT_LANG_JA,
    )
