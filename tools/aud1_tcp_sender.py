#!/usr/bin/env python3
"""Send a WAV file to the ESP32 bridge as a fixed-length AUD1 playback frame."""

from __future__ import annotations

import argparse
import audioop
import socket
import struct
import time
import wave
from pathlib import Path


AUD_MAGIC = b"AUD1"
AUD_SAMPLE_RATE = 16000
AUD_BYTES_PER_SAMPLE = 2
AUD_BYTE_RATE = AUD_SAMPLE_RATE * AUD_BYTES_PER_SAMPLE


DEFAULT_WAV_FILE = "audio_test/03 春を待つ (feat. 倚水).wav"
DEFAULT_ESP32_HOST = "172.20.10.3"
DEFAULT_ESP32_PORT = 5001
DEFAULT_PREBUFFER_BYTES = 8192


def load_wav_as_pcm(path: Path) -> bytes:
    with wave.open(str(path), "rb") as wav_file:
        channels = wav_file.getnchannels()
        sample_width = wav_file.getsampwidth()
        sample_rate = wav_file.getframerate()
        frame_count = wav_file.getnframes()
        pcm = wav_file.readframes(frame_count)

    if channels < 1:
        raise ValueError("WAV must have at least one channel")

    if channels > 1:
        weights = [1.0 / channels] * channels
        if channels == 2:
            pcm = audioop.tomono(pcm, sample_width, weights[0], weights[1])
        else:
            raise ValueError("Only mono or stereo WAV files are supported")

    if sample_rate != AUD_SAMPLE_RATE:
        pcm, _state = audioop.ratecv(pcm, sample_width, 1, sample_rate, AUD_SAMPLE_RATE, None)

    if sample_width != AUD_BYTES_PER_SAMPLE:
        pcm = audioop.lin2lin(pcm, sample_width, AUD_BYTES_PER_SAMPLE)

    if len(pcm) % AUD_BYTES_PER_SAMPLE != 0:
        pcm = pcm[:-1]

    return pcm


def checksum_bytes(payload: bytes) -> int:
    return sum(payload) & 0xFFFFFFFF


def read_ack_line(sock_file) -> tuple[str, int]:
    line = sock_file.readline()
    if not line:
        raise ConnectionError("ESP32 closed the connection while waiting for ACK")

    text = line.decode("ascii", errors="replace").strip()
    parts = text.split()
    if len(parts) != 2:
        raise ValueError(f"bad ACK line: {text!r}")

    tag = parts[0]
    try:
        value = int(parts[1], 10)
    except ValueError as exc:
        raise ValueError(f"bad ACK value: {text!r}") from exc

    if tag == "AUDERR":
        raise RuntimeError(f"ESP32 reported AUDERR {value}")

    return tag, value


def wait_for_header_ack(sock_file, seq: int) -> None:
    tag, value = read_ack_line(sock_file)
    if (tag != "AUDHOK") or (value != seq):
        raise RuntimeError(f"expected AUDHOK {seq}, got {tag} {value}")


def wait_for_payload_ack(sock_file, expected_bytes: int) -> None:
    while True:
        tag, value = read_ack_line(sock_file)
        if (tag == "AUDACK") and (value >= expected_bytes):
            return
        if tag == "AUDDONE":
            raise RuntimeError(f"AUDDONE arrived before AUDACK {expected_bytes}")


def wait_for_done(sock_file, seq: int) -> None:
    while True:
        tag, value = read_ack_line(sock_file)
        if (tag == "AUDDONE") and (value == seq):
            return
        if tag == "AUDACK":
            continue
        raise RuntimeError(f"expected AUDDONE {seq}, got {tag} {value}")


def send_aud1(
    host: str,
    port: int,
    wav_path: Path,
    seq: int,
    prebuffer_bytes: int,
    chunk_bytes: int,
) -> None:
    payload = load_wav_as_pcm(wav_path)
    sample_count = len(payload) // AUD_BYTES_PER_SAMPLE
    checksum = checksum_bytes(payload)
    header = struct.pack(
        "<4sIIIII",
        AUD_MAGIC,
        AUD_SAMPLE_RATE,
        sample_count,
        len(payload),
        seq,
        checksum,
    )

    print(
        f"AUD1 connect {host}:{port} seq={seq} "
        f"samples={sample_count} bytes={len(payload)} checksum={checksum}"
    )

    with socket.create_connection((host, port), timeout=10.0) as sock:
        sock.settimeout(10.0)
        sock_file = sock.makefile("rb")
        sock.sendall(header)
        wait_for_header_ack(sock_file, seq)

        sent = 0
        prebuffer = min(prebuffer_bytes, len(payload))
        if prebuffer > 0:
            sock.sendall(payload[:prebuffer])
            sent = prebuffer
            wait_for_payload_ack(sock_file, sent)

        stream_start = time.monotonic()
        while sent < len(payload):
            end = min(sent + chunk_bytes, len(payload))
            sock.sendall(payload[sent:end])
            sent = end
            wait_for_payload_ack(sock_file, sent)

            target_elapsed = max(0.0, (sent - prebuffer) / AUD_BYTE_RATE)
            sleep_for = (stream_start + target_elapsed) - time.monotonic()
            if sleep_for > 0:
                time.sleep(sleep_for)

        wait_for_done(sock_file, seq)

    print(f"AUD1 sent complete seq={seq} bytes={len(payload)}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send a WAV file to the ESP32 AUD1 playback bridge."
    )
    parser.add_argument(
        "wav",
        type=Path,
        nargs="?",
        default=Path(DEFAULT_WAV_FILE),
        help="Input WAV file"
    )
    parser.add_argument("--host", default=DEFAULT_ESP32_HOST, help="ESP32 Wi-Fi IPv4 address")
    parser.add_argument("--port", type=int, default=DEFAULT_ESP32_PORT, help="ESP32 AUD1 TCP port")
    parser.add_argument("--seq", type=int, default=1, help="Debug sequence number")
    parser.add_argument(
        "--prebuffer-bytes",
        type=int,
        default=DEFAULT_PREBUFFER_BYTES,
        help="Payload bytes to send before paced chunks",
    )
    parser.add_argument(
        "--chunk-bytes",
        type=int,
        default=1024,
        help="Payload bytes per ACK-paced chunk",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    send_aud1(
        host=args.host,
        port=args.port,
        wav_path=args.wav,
        seq=args.seq,
        prebuffer_bytes=args.prebuffer_bytes,
        chunk_bytes=args.chunk_bytes,
    )


if __name__ == "__main__":
    main()
