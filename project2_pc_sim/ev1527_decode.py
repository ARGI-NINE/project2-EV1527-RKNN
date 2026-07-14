#!/usr/bin/env python3
"""
Helper-only EV1527 WAV utilities used by python/wav_to_pulses.py.

This module intentionally does not expose a standalone analyzer or CLI path.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple

# IMA ADPCM tables
_IMA_STEP_TABLE = [
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    16,
    17,
    19,
    21,
    23,
    25,
    28,
    31,
    34,
    37,
    41,
    45,
    50,
    55,
    60,
    66,
    73,
    80,
    88,
    97,
    107,
    118,
    130,
    143,
    157,
    173,
    190,
    209,
    230,
    253,
    279,
    307,
    337,
    371,
    408,
    449,
    494,
    544,
    598,
    658,
    724,
    796,
    876,
    963,
    1060,
    1166,
    1282,
    1411,
    1552,
    1707,
    1878,
    2066,
    2272,
    2499,
    2749,
    3024,
    3327,
    3660,
    4026,
    4428,
    4871,
    5358,
    5894,
    6484,
    7132,
    7845,
    8630,
    9493,
    10442,
    11487,
    12635,
    13899,
    15289,
    16818,
    18500,
    20350,
    22385,
    24623,
    27086,
    29794,
    32767,
]
_IMA_INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8]


@dataclass
class WavData:
    sample_rate: int
    samples: List[int]
    format_tag: int


@dataclass
class Run:
    level: int
    start: int
    length: int


def _iter_wav_chunks(raw: bytes) -> Iterable[Tuple[bytes, bytes]]:
    if len(raw) < 12 or raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError("Not a valid RIFF/WAVE file.")
    offset = 12
    while offset + 8 <= len(raw):
        chunk_id = raw[offset : offset + 4]
        chunk_size = struct.unpack("<I", raw[offset + 4 : offset + 8])[0]
        data_start = offset + 8
        data_end = data_start + chunk_size
        if data_end > len(raw):
            break
        yield chunk_id, raw[data_start:data_end]
        offset = data_end + (chunk_size & 1)


def _decode_ima_adpcm_mono(data: bytes, block_align: int) -> List[int]:
    if block_align < 4:
        raise ValueError("Invalid IMA ADPCM block alignment.")

    samples: List[int] = []
    offset = 0
    while offset + block_align <= len(data):
        block = data[offset : offset + block_align]
        predictor = struct.unpack("<h", block[0:2])[0]
        step_index = block[2]
        if step_index > 88:
            raise ValueError("Invalid IMA ADPCM step index.")
        samples.append(predictor)
        for byte in block[4:]:
            for nibble in (byte & 0x0F, (byte >> 4) & 0x0F):
                step = _IMA_STEP_TABLE[step_index]
                diff = step >> 3
                if nibble & 1:
                    diff += step >> 2
                if nibble & 2:
                    diff += step >> 1
                if nibble & 4:
                    diff += step
                if nibble & 8:
                    predictor -= diff
                else:
                    predictor += diff
                predictor = max(-32768, min(32767, predictor))
                step_index += _IMA_INDEX_TABLE[nibble & 7]
                step_index = max(0, min(88, step_index))
                samples.append(predictor)
        offset += block_align
    return samples


def read_wav(path: Path) -> WavData:
    raw = path.read_bytes()
    fmt_data = None
    pcm_data = None
    for chunk_id, chunk_data in _iter_wav_chunks(raw):
        if chunk_id == b"fmt ":
            fmt_data = chunk_data
        elif chunk_id == b"data":
            pcm_data = chunk_data
    if fmt_data is None or pcm_data is None:
        raise ValueError("WAV is missing fmt or data chunk.")
    if len(fmt_data) < 16:
        raise ValueError("Invalid fmt chunk.")

    (
        format_tag,
        channels,
        sample_rate,
        _byte_rate,
        block_align,
        bits_per_sample,
    ) = struct.unpack("<HHIIHH", fmt_data[:16])

    if format_tag == 1:
        if channels < 1:
            raise ValueError("Invalid PCM channel count.")
        if bits_per_sample == 16:
            samples = list(struct.unpack("<" + "h" * (len(pcm_data) // 2), pcm_data))
        elif bits_per_sample == 8:
            samples = [((value - 128) << 8) for value in pcm_data]
        else:
            raise ValueError(f"Unsupported PCM bit depth: {bits_per_sample}")
        if channels > 1:
            samples = samples[::channels]
        return WavData(sample_rate=sample_rate, samples=samples, format_tag=format_tag)

    if format_tag == 17:
        if channels != 1:
            raise ValueError("IMA ADPCM decoder currently supports mono WAV only.")
        samples = _decode_ima_adpcm_mono(pcm_data, block_align)
        return WavData(sample_rate=sample_rate, samples=samples, format_tag=format_tag)

    raise ValueError(f"Unsupported WAV format tag: {format_tag}")


def moving_average(values: Sequence[float], window: int) -> List[float]:
    if window <= 1:
        return [float(value) for value in values]
    output: List[float] = [0.0] * len(values)
    acc = 0.0
    for index, value in enumerate(values):
        acc += float(value)
        if index >= window:
            acc -= float(values[index - window])
        output[index] = acc / float(min(index + 1, window))
    return output


def run_length_encode(bits: Sequence[int]) -> List[Run]:
    if not bits:
        return []
    runs: List[Run] = []
    start = 0
    current = bits[0]
    for index in range(1, len(bits)):
        if bits[index] != current:
            runs.append(Run(level=current, start=start, length=index - start))
            start = index
            current = bits[index]
    runs.append(Run(level=current, start=start, length=len(bits) - start))
    return runs


def merge_short_runs(runs: List[Run], min_len: int) -> List[Run]:
    if len(runs) < 3 or min_len <= 0:
        return [Run(run.level, run.start, run.length) for run in runs]

    merged = [Run(run.level, run.start, run.length) for run in runs]
    index = 1
    while index < len(merged) - 1:
        prev_run = merged[index - 1]
        cur_run = merged[index]
        next_run = merged[index + 1]

        should_merge = False
        if prev_run.level == next_run.level and cur_run.length <= min_len:
            neighbor_min = min(prev_run.length, next_run.length)
            if neighbor_min >= max(6, min_len * 4) and cur_run.length * 5 <= neighbor_min:
                should_merge = True

        if should_merge:
            prev_run.length += cur_run.length + next_run.length
            merged.pop(index + 1)
            merged.pop(index)
            if index > 1:
                index -= 1
            continue
        index += 1
    return merged
