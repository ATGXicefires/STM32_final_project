#!/usr/bin/env python3
"""Assistant Server Coordinator.

Integrates PCM1 audio capture, ASR, LLM, TTS, and AUD1 playback
to form a complete dialogue loop with the STM32 voice assistant.
"""

from __future__ import annotations

import socket
import struct
import sys
import wave
from pathlib import Path

# Add parent directory to sys.path to resolve configuration
sys.path.append(str(Path(__file__).resolve().parent.parent))
from tools import asr_local, config, nim_llm, tts_sovits
from tools.aud1_tcp_sender import send_aud1

HEADER_SIZE = 24
MAGIC_PCM1 = b"PCM1"


def read_exact(conn: socket.socket, size: int) -> bytes:
    """Reads exactly size bytes from a socket, raising ConnectionError if EOF is hit."""
    chunks: list[bytes] = []
    remaining = size
    while remaining > 0:
        chunk = conn.recv(remaining)
        if not chunk:
            raise ConnectionError(f"Connection closed with {remaining} bytes remaining")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def write_wav(path: Path, sample_rate: int, payload: bytes) -> None:
    """Saves raw 16-bit mono PCM bytes to a WAV file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(payload)


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

    received_wav = config.BASE_DIR / "received.wav"
    response_wav = config.BASE_DIR / "response.wav"

    print("Initializing Engines...")
    # Warm up local Whisper model
    asr_local.get_asr_engine()
    llm_engine = nim_llm.get_llm_engine()
    print("Initialization complete.")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("0.0.0.0", config.PCM1_PORT))
        server.listen(5)
        print(f"Server listening on port {config.PCM1_PORT} (PCM1)...")

        while True:
            try:
                conn, addr = server.accept()
            except KeyboardInterrupt:
                print("\nServer shutting down.")
                break
            except Exception as e:
                print(f"Error accepting connection: {e}")
                continue

            print(f"Connected from {addr[0]}:{addr[1]} (New recording session)")
            all_payloads: list[bytes] = []
            sample_rate = 16000

            with conn:
                conn.settimeout(5.0)  # Timeout for individual frames
                while True:
                    try:
                        header = read_exact(conn, HEADER_SIZE)
                    except (ConnectionError, OSError):
                        # Connection closed or timed out → K1 released, recording finished
                        break

                    if header[:4] != MAGIC_PCM1:
                        print(f"  [WARNING] Invalid packet magic: {header[:4]!r}. Closing session.")
                        break

                    sample_rate_v, sample_count, payload_bytes, seq, expected_checksum = (
                        struct.unpack("<IIIII", header[4:])
                    )

                    try:
                        payload = read_exact(conn, payload_bytes)
                    except (ConnectionError, OSError):
                        print(f"  [WARNING] Connection lost mid-payload for seq={seq}.")
                        break

                    actual_checksum = sum(payload) & 0xFFFFFFFF
                    if actual_checksum != expected_checksum:
                        print(f"  [WARNING] Checksum mismatch for seq={seq}. Skipping packet.")
                        continue

                    all_payloads.append(payload)
                    sample_rate = sample_rate_v

            if all_payloads:
                combined_pcm = b"".join(all_payloads)
                print(f"Recording session finished. Received {len(combined_pcm)} bytes of PCM.")
                write_wav(received_wav, sample_rate, combined_pcm)
                run_pipeline(received_wav, response_wav, llm_engine)
            else:
                print("Connection closed with no audio packets received.")


if __name__ == "__main__":
    main()
