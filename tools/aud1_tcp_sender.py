#!/usr/bin/env python3
"""Send a WAV file to the ESP32 bridge as a fixed-length AUD1 playback frame."""

from __future__ import annotations

import argparse
import math
import select
import socket
import struct
import time
import wave
from pathlib import Path


AUD_MAGIC = b"AUD1"
AUD_SAMPLE_RATE = 16000
AUD_BYTES_PER_SAMPLE = 2
AUD_BYTE_RATE = AUD_SAMPLE_RATE * AUD_BYTES_PER_SAMPLE

DEFAULT_WAV_FILE = "audio_test/test.wav"
DEFAULT_ESP32_HOST = "172.20.10.3"
DEFAULT_ESP32_PORT = 5001
DEFAULT_PREBUFFER_BYTES = 8192
DEFAULT_WINDOW_BYTES = 24576  # 24 KB in-flight limit; fills STM32's 64 KB ring buffer


def load_wav_as_pcm(path: Path) -> bytes:
    with wave.open(str(path), "rb") as wav_file:
        channels = wav_file.getnchannels()
        sample_width = wav_file.getsampwidth()
        sample_rate = wav_file.getframerate()
        frame_count = wav_file.getnframes()
        raw = wav_file.readframes(frame_count)

    if channels < 1 or channels > 2:
        raise ValueError(f"Only mono or stereo WAV supported, got {channels} channels")

    n = frame_count * channels
    if sample_width == 1:
        interleaved = [(b - 128) << 8 for b in raw]
    elif sample_width == 2:
        interleaved = list(struct.unpack(f"<{n}h", raw))
    elif sample_width == 3:
        interleaved = [int.from_bytes(raw[i:i + 3], "little", signed=True) >> 8
                       for i in range(0, len(raw), 3)]
    elif sample_width == 4:
        interleaved = [v >> 16 for v in struct.unpack(f"<{n}i", raw)]
    else:
        raise ValueError(f"Unsupported sample width: {sample_width} byte(s)")

    if channels == 2:
        mono = [(interleaved[i] + interleaved[i + 1]) // 2
                for i in range(0, len(interleaved), 2)]
    else:
        mono = interleaved

    if sample_rate != AUD_SAMPLE_RATE:
        ratio = sample_rate / AUD_SAMPLE_RATE
        output_len = int(round(len(mono) * AUD_SAMPLE_RATE / sample_rate))
        resampled: list[int] = []
        for i in range(output_len):
            pos = i * ratio
            base = int(math.floor(pos))
            frac = pos - base
            if base >= len(mono) - 1:
                resampled.append(mono[-1])
            else:
                resampled.append(int(round(mono[base] * (1.0 - frac) + mono[base + 1] * frac)))
        mono = resampled

    return struct.pack(f"<{len(mono)}h", *(max(-32768, min(32767, s)) for s in mono))


def checksum_bytes(payload: bytes) -> int:
    return sum(payload) & 0xFFFFFFFF


class _AckStream:
    """Line reader that supports non-blocking drain via select, avoiding sock_file buffering."""

    def __init__(self, sock: socket.socket) -> None:
        self._sock = sock
        self._buf = bytearray()

    def _fill(self, timeout: float) -> None:
        r, _, _ = select.select([self._sock], [], [], timeout)
        if r:
            data = self._sock.recv(4096)
            if not data:
                raise ConnectionError("ESP32 closed connection")
            self._buf.extend(data)

    def _pop_line(self) -> str | None:
        if b"\n" not in self._buf:
            return None
        idx = self._buf.index(b"\n")
        line = bytes(self._buf[: idx + 1])
        del self._buf[: idx + 1]
        return line.decode("ascii", errors="replace").strip()

    def read_line(self, timeout: float = 10.0) -> str:
        deadline = time.monotonic() + timeout
        while True:
            line = self._pop_line()
            if line is not None:
                return line
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("timed out waiting for ESP32 response")
            self._fill(min(remaining, 1.0))

    def drain(self) -> list[str]:
        """Return all immediately-available lines without blocking."""
        self._fill(0.0)
        lines: list[str] = []
        while True:
            line = self._pop_line()
            if line is None:
                break
            lines.append(line)
        return lines


def _parse_ack(text: str) -> tuple[str, int]:
    parts = text.split()
    if len(parts) != 2:
        raise ValueError(f"unexpected ESP32 line: {text!r}")
    tag = parts[0]
    try:
        val = int(parts[1], 10)
    except ValueError as exc:
        raise ValueError(f"bad value in ESP32 line: {text!r}") from exc
    if tag == "AUDERR":
        raise RuntimeError(f"ESP32 reported AUDERR {val}")
    return tag, val


def send_aud1(
    host: str,
    port: int,
    wav_path: Path,
    seq: int,
    prebuffer_bytes: int,
    chunk_bytes: int,
    window_bytes: int,
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
        acks = _AckStream(sock)
        sock.sendall(header)

        tag, val = _parse_ack(acks.read_line())
        if tag != "AUDHOK" or val != seq:
            raise RuntimeError(f"expected AUDHOK {seq}, got {tag} {val}")

        sent = 0
        acked = 0
        prebuffer = min(prebuffer_bytes, len(payload))
        stream_start: float | None = None

        while sent < len(payload):
            # Drain any ACKs that arrived since last iteration (non-blocking)
            for line in acks.drain():
                tag, val = _parse_ack(line)
                if tag == "AUDACK":
                    acked = max(acked, val)

            # If window is full, block until we receive a new ACK
            while (sent - acked) >= window_bytes:
                tag, val = _parse_ack(acks.read_line())
                if tag == "AUDACK":
                    acked = max(acked, val)

            end = min(sent + chunk_bytes, len(payload))
            sock.sendall(payload[sent:end])
            sent = end

            # Rate-limit to audio playback speed after the initial prebuffer burst
            if sent > prebuffer:
                if stream_start is None:
                    stream_start = time.monotonic()
                target_elapsed = max(0.0, (sent - prebuffer) / AUD_BYTE_RATE)
                sleep_for = stream_start + target_elapsed - time.monotonic()
                if sleep_for > 0:
                    time.sleep(sleep_for)

        # Drain remaining ACKs and wait for AUDDONE
        while True:
            tag, val = _parse_ack(acks.read_line(timeout=30.0))
            if tag == "AUDDONE" and val == seq:
                break
            if tag == "AUDACK":
                acked = max(acked, val)

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
        help="Payload bytes to send before rate-limiting kicks in (default: 8192)",
    )
    parser.add_argument(
        "--chunk-bytes",
        type=int,
        default=1024,
        help="Payload bytes per send call (default: 1024)",
    )
    parser.add_argument(
        "--window-bytes",
        type=int,
        default=DEFAULT_WINDOW_BYTES,
        help="Max in-flight unacknowledged bytes; must be <= STM32 ring buffer (64 KB) (default: 24576)",
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
        window_bytes=args.window_bytes,
    )


if __name__ == "__main__":
    main()
