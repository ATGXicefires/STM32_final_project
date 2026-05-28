#!/usr/bin/env python3
"""Assistant Server Coordinator.

Integrates PCM1 audio capture, ASR, LLM, TTS, and AUD1 playback
to form a complete dialogue loop with the STM32 voice assistant.
"""

from __future__ import annotations

import sys
from pathlib import Path

# Add parent directory to sys.path to resolve configuration
sys.path.append(str(Path(__file__).resolve().parent.parent))
from tools import asr_local, config, nim_llm, pcm1_server, tts_sovits
from tools.aud1_tcp_sender import send_aud1


def run_pipeline(
    wav_path: Path,
    response_wav_path: Path,
    llm_engine: nim_llm.NIMLLMEngine,
) -> None:
    """Executes the dialogue pipeline: ASR -> LLM -> TTS -> Playback."""
    print("\n" + "=" * 50)
    print("STARTING DIALOGUE PIPELINE")
    print("=" * 50)

    try:
        print("[ASR] Running transcription...")
        text, elapsed = asr_local.transcribe_audio(wav_path)
        print(f"[ASR] Finished in {elapsed:.2f}s. Result: '{text}'")
        if not text.strip():
            print("[ASR] No speech detected or transcribed. Skipping pipeline.")
            return
    except Exception as e:
        print(f"[ERROR] ASR transcription failed: {e}")
        return

    try:
        print("[LLM] Generating response from NIM cloud...")
        reply = llm_engine.get_response(text)
        print(f"[LLM] Reply: '{reply}'")
    except Exception as e:
        print(f"[ERROR] LLM response generation failed: {e}")
        return

    try:
        print("[TTS] Synthesizing speech via GPT-SoVITS V2...")
        success = tts_sovits.synthesize_speech(reply, response_wav_path)
        if not success:
            print("[ERROR] TTS synthesis failed.")
            return
    except Exception as e:
        print(f"[ERROR] TTS client error: {e}")
        return

    try:
        print("[AUD1] Streaming synthesized audio back to STM32...")
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
        print("[AUD1] Streaming playback completed successfully.")
    except Exception as e:
        print(f"[ERROR] AUD1 playback streaming failed: {e}")

    print("=" * 50)
    print("DIALOGUE PIPELINE FINISHED. READY FOR NEXT RECORDING.")
    print("=" * 50 + "\n")


def main() -> None:
    # Set Python I/O encoding to UTF-8
    sys.stdout.reconfigure(encoding="utf-8")

    response_wav = config.BASE_DIR / "response.wav"

    print("Initializing Engines...")
    # Warm up local Whisper model
    asr_local.get_asr_engine()
    llm_engine = nim_llm.get_llm_engine()
    print("Initialization complete.")

    pcm1_server.serve(
        lambda wav: run_pipeline(wav, response_wav, llm_engine),
        received_wav=config.BASE_DIR / "received.wav",
    )


if __name__ == "__main__":
    main()
