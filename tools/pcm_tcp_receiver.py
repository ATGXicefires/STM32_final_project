#!/usr/bin/env python3
"""Receive PCM1 frame(s) over TCP and save as WAV.

Supports single-frame and multi-frame streaming (K1 hold-to-record).
Each chunk arrives as a separate TCP connection; payloads are
concatenated into one WAV file. Stops after --timeout seconds of
silence (no new connection).
"""

from __future__ import annotations

import argparse
import socket
import struct
import wave
from pathlib import Path


HEADER_SIZE = 24
MAGIC = b"PCM1"
DEFAULT_TIMEOUT = 2.0
DEFAULT_INITIAL_TIMEOUT = 30.0


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


def receive_packets(
    host: str,
    port: int,
    output: Path,
    raw_output: Path | None,
    timeout: float,
    initial_timeout: float,
) -> None:
    """Accept PCM1 connections until timeout, concatenate payloads, save WAV."""
    all_payloads: list[bytes] = []
    sample_rate = 0

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((host, port))
        server.listen(5)
        server.settimeout(initial_timeout)
        print(f"Listening on {host}:{port}  (initial wait={initial_timeout}s, inter-packet timeout={timeout}s)")

        while True:
            try:
                conn, addr = server.accept()
                server.settimeout(timeout)  # switch to short timeout after first packet
            except socket.timeout:
                break

            with conn:
                print(f"Connected from {addr[0]}:{addr[1]}")
                header = read_exact(conn, HEADER_SIZE)

                if header[:4] != MAGIC:
                    print(f"  bad magic: {header[:4]!r}, skipping")
                    continue

                sample_rate_v, sample_count, payload_bytes, seq, expected_checksum = (
                    struct.unpack("<IIIII", header[4:])
                )
                payload = read_exact(conn, payload_bytes)
                actual_checksum = sum(payload) & 0xFFFFFFFF

                if actual_checksum != expected_checksum:
                    print(
                        f"  checksum mismatch seq={seq} "
                        f"actual={actual_checksum} expected={expected_checksum}, skipping"
                    )
                    continue

                print(
                    f"  seq={seq} rate={sample_rate_v} samples={sample_count} "
                    f"bytes={payload_bytes} checksum=OK"
                )
                all_payloads.append(payload)
                sample_rate = sample_rate_v

    if not all_payloads:
        print("No valid packets received")
        return

    combined = b"".join(all_payloads)
    write_wav(output, sample_rate, combined)
    print(
        f"Saved WAV: {len(all_payloads)} packet(s), "
        f"{len(combined) // 2} samples, path={output}"
    )

    if raw_output is not None:
        raw_output.parent.mkdir(parents=True, exist_ok=True)
        raw_output.write_bytes(combined)
        print(f"Saved raw PCM path={raw_output}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Receive PCM1 frame(s) from the ESP32 bridge and save a WAV file."
    )
    parser.add_argument("--host", default="0.0.0.0", help="TCP bind host")
    parser.add_argument("--port", type=int, default=5000, help="TCP bind port")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("received.wav"),
        help="Output WAV path",
    )
    parser.add_argument(
        "--raw-output",
        type=Path,
        default=None,
        help="Optional raw signed 16-bit little-endian PCM output path",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=DEFAULT_TIMEOUT,
        help="seconds to wait for next packet before declaring stream done (default: 2.0)",
    )
    parser.add_argument(
        "--initial-timeout",
        type=float,
        default=DEFAULT_INITIAL_TIMEOUT,
        help="seconds to wait for the very first packet (default: 30.0)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    receive_packets(args.host, args.port, args.output, args.raw_output, args.timeout, args.initial_timeout)


if __name__ == "__main__":
    main()
