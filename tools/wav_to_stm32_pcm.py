#!/usr/bin/env python3
"""Convert a WAV file to an STM32 int16_t PCM C header.

Reads a WAV file, converts to mono 16-bit PCM, optionally resamples,
applies gain, and writes a .h file with a static const int16_t array.
"""

from __future__ import annotations

import argparse
import math
import struct
import wave
from pathlib import Path


def read_wav_mono_16(path: Path) -> tuple[int, list[int]]:
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        sample_rate = wav.getframerate()
        frames = wav.getnframes()
        raw = wav.readframes(frames)

    if sample_width != 2:
        raise ValueError(f"expected 16-bit PCM WAV, got {sample_width * 8}-bit")

    values = struct.unpack("<" + "h" * (len(raw) // 2), raw)
    if channels == 1:
        return sample_rate, list(values)

    mono: list[int] = []
    for index in range(0, len(values), channels):
        mono.append(int(sum(values[index : index + channels]) / channels))
    return sample_rate, mono


def resample_linear(samples: list[int], source_rate: int, target_rate: int) -> list[int]:
    if source_rate == target_rate:
        return samples[:]

    output_len = int(round(len(samples) * target_rate / source_rate))
    result: list[int] = []
    for out_index in range(output_len):
        source_pos = out_index * source_rate / target_rate
        base = int(math.floor(source_pos))
        frac = source_pos - base
        if base >= len(samples) - 1:
            result.append(samples[-1])
        else:
            value = samples[base] * (1.0 - frac) + samples[base + 1] * frac
            result.append(int(round(value)))
    return result


def write_header(
    output: Path,
    symbol: str,
    samples: list[int],
    source: Path,
    source_rate: int,
    target_rate: int,
    gain: float,
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="ascii", newline="\n") as handle:
        handle.write("#ifndef AUDIO_CLIP_H\n")
        handle.write("#define AUDIO_CLIP_H\n\n")
        handle.write("#include <stdint.h>\n\n")
        handle.write(f"// Generated from {source.name}: {source_rate} Hz -> {target_rate} Hz, gain {gain:.3f}.\n")
        handle.write(f"#define {symbol.upper()}_SAMPLE_RATE {target_rate}U\n")
        handle.write(f"#define {symbol.upper()}_SAMPLE_COUNT {len(samples)}U\n\n")
        handle.write(f"static const int16_t {symbol}[{len(samples)}] = {{\n")
        for index in range(0, len(samples), 12):
            chunk = samples[index : index + 12]
            handle.write("  " + ", ".join(str(sample) for sample in chunk) + ",\n")
        handle.write("};\n\n")
        handle.write("#endif\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert WAV to STM32 int16 PCM header.")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--symbol", default="audio_clip")
    parser.add_argument("--rate", type=int, default=16000)
    parser.add_argument("--gain", type=float, default=0.35)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    source_rate, samples = read_wav_mono_16(args.input)
    samples = resample_linear(samples, source_rate, args.rate)
    scaled = [max(-32768, min(32767, int(round(sample * args.gain)))) for sample in samples]
    write_header(args.output, args.symbol, scaled, args.input, source_rate, args.rate, args.gain)


if __name__ == "__main__":
    main()
