#!/usr/bin/env python3
"""Translator Server (zh → ja) for Stage 9 pipeline verification.

Receives PCM1 audio from the STM32 + ESP32 bridge, runs ASR (Chinese),
LLM translation into Japanese, GPT-SoVITS V2 Japanese synthesis, then
streams the result back via AUD1. Logs per-stage latency.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

# Add parent directory to sys.path to resolve configuration
sys.path.append(str(Path(__file__).resolve().parent.parent))
from tools import asr_local, config, nim_llm, pcm1_server, tts_sovits
from tools.aud1_tcp_sender import send_aud1


def run_translation_pipeline(
    wav_path: Path,
    response_wav_path: Path,
    llm_engine: nim_llm.NIMLLMEngine,
    tts_client: tts_sovits.SoVITSClient,
) -> None:
    """Executes the verification pipeline: ASR -> LLM (zh→ja) -> TTS (ja) -> AUD1."""
    print("\n" + "=" * 50)
    print("STARTING TRANSLATION PIPELINE (zh → ja)")
    print("=" * 50)

    pipeline_start = time.monotonic()
    asr_elapsed = 0.0
    llm_elapsed = 0.0
    tts_elapsed = 0.0
    aud1_elapsed = 0.0

    try:
        print("[ASR] Running transcription...")
        text, asr_elapsed = asr_local.transcribe_audio(wav_path)
        print(f"[ASR] Finished in {asr_elapsed:.2f}s. Result: '{text}'")
        if not text.strip():
            print("[ASR] No speech detected or transcribed. Skipping pipeline.")
            return
    except Exception as e:
        print(f"[ERROR] ASR transcription failed: {e}")
        return

    try:
        # Reset history every utterance — translation should not be biased by prior turns
        llm_engine.reset_session()
        print("[LLM] Translating to Japanese via NIM cloud...")
        llm_start = time.monotonic()
        reply = llm_engine.get_response(text)
        llm_elapsed = time.monotonic() - llm_start
        print(f"[LLM] Finished in {llm_elapsed:.2f}s. Reply: '{reply}'")
    except Exception as e:
        print(f"[ERROR] LLM translation failed: {e}")
        return

    try:
        print("[TTS] Synthesizing Japanese speech via GPT-SoVITS V2...")
        tts_start = time.monotonic()
        success = tts_client.synthesize(reply, response_wav_path)
        tts_elapsed = time.monotonic() - tts_start
        if not success:
            print("[ERROR] TTS synthesis failed.")
            return
        print(f"[TTS] Finished in {tts_elapsed:.2f}s.")
    except Exception as e:
        print(f"[ERROR] TTS client error: {e}")
        return

    try:
        print("[AUD1] Streaming synthesized audio back to STM32...")
        aud1_start = time.monotonic()
        send_aud1(
            host=config.ESP32_HOST,
            port=config.AUD1_PORT,
            wav_path=response_wav_path,
            seq=config.AUD1_SEQ,
            prebuffer_bytes=config.AUD1_PREBUFFER_BYTES,
            chunk_bytes=config.AUD1_CHUNK_BYTES,
            window_bytes=config.AUD1_WINDOW_BYTES,
            volume_scale=config.AUD1_VOLUME_SCALE,
        )
        aud1_elapsed = time.monotonic() - aud1_start
        print(f"[AUD1] Streaming playback completed in {aud1_elapsed:.2f}s.")
    except Exception as e:
        print(f"[ERROR] AUD1 playback streaming failed: {e}")

    total_elapsed = time.monotonic() - pipeline_start
    print(
        f"[TIMING] ASR={asr_elapsed:.2f}s LLM={llm_elapsed:.2f}s "
        f"TTS={tts_elapsed:.2f}s AUD1={aud1_elapsed:.2f}s "
        f"total={total_elapsed:.2f}s"
    )

    print("=" * 50)
    print("TRANSLATION PIPELINE FINISHED. READY FOR NEXT RECORDING.")
    print("=" * 50 + "\n")


def main() -> None:
    # Set Python I/O encoding to UTF-8
    sys.stdout.reconfigure(encoding="utf-8")

    response_wav = config.BASE_DIR / "response_ja.wav"

    print("Initializing Engines...")
    # Warm up local Whisper model
    asr_local.get_asr_engine()
    llm_engine = nim_llm.create_translator_engine()
    tts_client = tts_sovits.create_japanese_client()
    print("Initialization complete.")

    pcm1_server.serve(
        lambda wav: run_translation_pipeline(wav, response_wav, llm_engine, tts_client),
        received_wav=config.BASE_DIR / "received.wav",
    )


if __name__ == "__main__":
    main()
