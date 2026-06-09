"""Shared PCM1 recording server for the STM32 voice assistant pipeline.

Accepts PCM1 audio frames from the ESP32 bridge, reassembles one recording
session into a WAV file, then hands it to a caller-supplied pipeline callback.
Both assistant_server.py and translator_server.py build on this.
"""

from __future__ import annotations

import socket
import struct
import wave
from collections.abc import Callable
from pathlib import Path

from tools import config

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


def serve(
    on_recording: Callable[[Path], None],
    received_wav: Path,
    port: int = config.PCM1_PORT,
) -> None:
    """Listens for PCM1 sessions and invokes on_recording(received_wav) per recording.

    Each TCP connection is one K1 hold-to-record session. Frames are validated by
    magic and additive checksum, reassembled into received_wav, then passed to the
    pipeline callback. Blocks until KeyboardInterrupt.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("0.0.0.0", port))
        server.listen(5)
        print(f"Server listening on port {port} (PCM1)...")

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
                # Frames arrive every ~0.5 s while K1 is held; the ESP32 only closes the
                # connection after a 3 s idle timeout, so treat a 1 s gap as end-of-session
                # to start the pipeline sooner instead of waiting for the remote close.
                conn.settimeout(1.0)
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
                        # Keep the frame anyway: noisy audio beats a silent 0.5 s gap for ASR.
                        print(f"  [WARNING] Checksum mismatch for seq={seq}. Keeping frame anyway.")

                    all_payloads.append(payload)
                    sample_rate = sample_rate_v

            if all_payloads:
                combined_pcm = b"".join(all_payloads)
                print(f"Recording session finished. Received {len(combined_pcm)} bytes of PCM.")
                write_wav(received_wav, sample_rate, combined_pcm)
                on_recording(received_wav)
            else:
                print("Connection closed with no audio packets received.")
