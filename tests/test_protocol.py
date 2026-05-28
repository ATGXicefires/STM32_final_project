"""Unit tests for the PCM1 / AUD1 wire-protocol helpers.

All pure logic — no hardware, GPU, or external network required.
"""

from __future__ import annotations

import socket
import struct
import wave

import pytest

from tools import aud1_tcp_sender, pcm1_server


# --- read_exact -----------------------------------------------------------

def test_read_exact_reads_full_payload():
    a, b = socket.socketpair()
    try:
        b.sendall(b"hello world")
        assert pcm1_server.read_exact(a, 5) == b"hello"
        assert pcm1_server.read_exact(a, 6) == b" world"
    finally:
        a.close()
        b.close()


def test_read_exact_raises_on_eof():
    a, b = socket.socketpair()
    try:
        b.sendall(b"hi")
        b.close()  # signal EOF after only 2 bytes
        with pytest.raises(ConnectionError):
            pcm1_server.read_exact(a, 4)
    finally:
        a.close()


# --- write_wav ------------------------------------------------------------

def test_write_wav_roundtrip(tmp_path):
    payload = struct.pack("<4h", 0, 1000, -1000, 32767)
    out = tmp_path / "out.wav"
    pcm1_server.write_wav(out, 16000, payload)

    with wave.open(str(out), "rb") as w:
        assert w.getnchannels() == 1
        assert w.getsampwidth() == 2
        assert w.getframerate() == 16000
        assert w.getnframes() == 4
        assert w.readframes(4) == payload


# --- checksum -------------------------------------------------------------

def test_checksum_matches_server_inline_formula():
    payload = bytes(range(256)) * 4
    expected = sum(payload) & 0xFFFFFFFF
    assert aud1_tcp_sender.checksum_bytes(payload) == expected


def test_checksum_wraps_at_32_bits():
    payload = b"\xff" * (2 ** 24)  # sum = 0xFF000000, still within 32 bits
    assert aud1_tcp_sender.checksum_bytes(payload) == (0xFF * len(payload)) & 0xFFFFFFFF


# --- load_wav_as_pcm (resampling) -----------------------------------------

def _write_wav(path, sample_rate, samples):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(struct.pack(f"<{len(samples)}h", *samples))


def test_load_wav_as_pcm_resamples_32k_to_16k(tmp_path):
    src = tmp_path / "src.wav"
    samples = [int(1000 * (i % 7 - 3)) for i in range(3200)]  # 32 kHz, 0.1 s
    _write_wav(src, 32000, samples)

    out = aud1_tcp_sender.load_wav_as_pcm(src)
    out_samples = len(out) // 2
    assert out_samples == pytest.approx(len(samples) * 16000 // 32000, abs=1)
    assert len(out) % 2 == 0  # 16-bit aligned


def test_load_wav_as_pcm_volume_scale_is_linear(tmp_path):
    src = tmp_path / "src.wav"
    samples = [1000, 2000, -3000, 4000]  # already 16 kHz → no resampling
    _write_wav(src, 16000, samples)

    full = struct.unpack("<4h", aud1_tcp_sender.load_wav_as_pcm(src, volume_scale=1.0))
    half = struct.unpack("<4h", aud1_tcp_sender.load_wav_as_pcm(src, volume_scale=0.5))
    for f, h in zip(full, half):
        assert h == int(f * 0.5)


# --- AUD1 header roundtrip ------------------------------------------------

def test_aud1_header_roundtrip():
    payload = b"\x01\x02\x03\x04" * 10
    sample_count = len(payload) // aud1_tcp_sender.AUD_BYTES_PER_SAMPLE
    checksum = aud1_tcp_sender.checksum_bytes(payload)
    header = struct.pack(
        "<4sIIIII",
        aud1_tcp_sender.AUD_MAGIC,
        aud1_tcp_sender.AUD_SAMPLE_RATE,
        sample_count,
        len(payload),
        7,
        checksum,
    )
    assert len(header) == 24
    magic = header[:4]
    rate, count, nbytes, seq, csum = struct.unpack("<IIIII", header[4:])
    assert magic == aud1_tcp_sender.AUD_MAGIC
    assert rate == aud1_tcp_sender.AUD_SAMPLE_RATE
    assert count == sample_count
    assert nbytes == len(payload)
    assert seq == 7
    assert csum == checksum
