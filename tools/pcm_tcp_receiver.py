#!/usr/bin/env python3
"""Receive one STM32 PCM1 frame over TCP and save it as WAV."""

from __future__ import annotations

import argparse
import socket
import struct
import wave
from pathlib import Path


HEADER_SIZE = 24
MAGIC = b"PCM1"


def read_exact(conn: socket.socket, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size

    while remaining > 0:
        chunk = conn.recv(remaining)
        if not chunk:
            raise ConnectionError(f"connection closed with {remaining} bytes remaining")
        chunks.append(chunk)
        remaining -= len(chunk)

    return b"".join(chunks)


def write_wav(path: Path, sample_rate: int, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(payload)


def receive_once(host: str, port: int, output: Path, raw_output: Path | None) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((host, port))
        server.listen(1)
        print(f"Listening on {host}:{port}")

        conn, addr = server.accept()
        with conn:
            print(f"Connected from {addr[0]}:{addr[1]}")
            header = read_exact(conn, HEADER_SIZE)

            if header[:4] != MAGIC:
                raise ValueError(f"bad magic: {header[:4]!r}")

            sample_rate, sample_count, payload_bytes, seq, expected_checksum = struct.unpack(
                "<IIIII", header[4:]
            )
            payload = read_exact(conn, payload_bytes)
            actual_checksum = sum(payload) & 0xFFFFFFFF

            if actual_checksum != expected_checksum:
                raise ValueError(
                    "checksum mismatch: "
                    f"actual={actual_checksum} expected={expected_checksum}"
                )

            if sample_count * 2 != payload_bytes:
                raise ValueError(
                    f"sample_count/payload mismatch: samples={sample_count} bytes={payload_bytes}"
                )

            write_wav(output, sample_rate, payload)
            print(
                "Saved WAV "
                f"seq={seq} rate={sample_rate} samples={sample_count} "
                f"bytes={payload_bytes} checksum={actual_checksum} path={output}"
            )

            if raw_output is not None:
                raw_output.parent.mkdir(parents=True, exist_ok=True)
                raw_output.write_bytes(payload)
                print(f"Saved raw PCM path={raw_output}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Receive one PCM1 frame from the ESP32 bridge and save a WAV file."
    )
    parser.add_argument("--host", default="0.0.0.0", help="TCP bind host")
    parser.add_argument("--port", type=int, default=5000, help="TCP bind port")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("stage8_received.wav"),
        help="Output WAV path",
    )
    parser.add_argument(
        "--raw-output",
        type=Path,
        default=None,
        help="Optional raw signed 16-bit little-endian PCM output path",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    receive_once(args.host, args.port, args.output, args.raw_output)


if __name__ == "__main__":
    main()
