#!/usr/bin/env python3
"""
EV1527 decoder for noisy captures with unknown transmitter clock.

Features:
- Reads WAV PCM and IMA ADPCM (format tag 17).
- Does not require known sender clock frequency.
- Searches for candidate frames by EV1527 timing model:
  sync + 24 bits => 25 high/low pulse pairs (50 runs).
- Handles long idle flips / noise with:
  hysteresis thresholding, short-run merge, model-fit confidence, repeat clustering.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, List, Optional, Sequence, Tuple

EV1527_CLK_MIN_US = 230.0
EV1527_CLK_MAX_US = 4240.0
EV1527_BIT_SHORT_T = 4.0
EV1527_BIT_LONG_T = 12.0
EV1527_SYNC_HIGH_T = 4.0
EV1527_SYNC_LOW_T = 124.0
EV1527_BITS_PER_FRAME = 24

# Structural guards for frame search / cleanup.
SYNC_LOW_LONGEST_MIN_RATIO = 3.0
OVERLAP_DROP_MIN_RATIO = 0.45
MIN_CLUSTER_AVG_CONFIDENCE = 0.50
MAX_CLUSTER_AVG_BIT_ERROR = 1.20
MAX_CLUSTER_AVG_JITTER = 0.70
MIN_EFFECTIVE_BURST_REPEAT = 2


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


@dataclass
class PreprocessConfig:
    name: str
    smooth_window: int
    min_run: int


@dataclass(frozen=True)
class TimingProfile:
    name: str
    bit_short_t: float
    bit_long_t: float
    sync_high_t: float
    sync_low_t: float
    # Ratio model can be represented as 1/3/31 or scaled 4/12/124.
    # clock_divisor maps profile-internal clock back to user-facing clock.
    clock_divisor: float = 1.0


@dataclass
class FrameCandidate:
    start_sample: int
    end_sample: int
    clk_us: float
    bits: str
    confidence: float
    bit_error: float
    sync_error: float
    period_jitter: float
    source_config: str
    support_count: int = 1
    observation_count: int = 1
    bit_vote_margin: float = 1.0
    bit_confidences: Tuple[float, ...] = ()

    @property
    def code_hex(self) -> str:
        return f"0x{int(self.bits, 2):06X}"

    @property
    def address_bits(self) -> str:
        return self.bits[:20]

    @property
    def data_bits(self) -> str:
        return self.bits[20:]


@dataclass
class Cluster:
    center_bits: str
    mean_clk_us: float
    frames: List[FrameCandidate] = field(default_factory=list)
    _weights_zero: List[float] = field(default_factory=list)
    _weights_one: List[float] = field(default_factory=list)

    def __post_init__(self) -> None:
        if not self._weights_zero:
            self._weights_zero = [0.0] * 24
        if not self._weights_one:
            self._weights_one = [0.0] * 24

    def add(self, frame: FrameCandidate) -> None:
        self.frames.append(frame)
        for i, b in enumerate(frame.bits):
            bit_w = frame.confidence
            if i < len(frame.bit_confidences):
                bit_w *= max(0.05, frame.bit_confidences[i])
            if b == "1":
                self._weights_one[i] += bit_w
            else:
                self._weights_zero[i] += bit_w
        self.center_bits = "".join(
            "1" if self._weights_one[i] >= self._weights_zero[i] else "0"
            for i in range(24)
        )
        total_w = sum(f.confidence for f in self.frames)
        if total_w > 0.0:
            self.mean_clk_us = sum(f.clk_us * f.confidence for f in self.frames) / total_w

    @property
    def avg_confidence(self) -> float:
        return sum(f.confidence for f in self.frames) / max(1, len(self.frames))

    @property
    def first_sample(self) -> int:
        return min(f.start_sample for f in self.frames)

    @property
    def last_sample(self) -> int:
        return max(f.start_sample for f in self.frames)

    @property
    def address_bits(self) -> str:
        return self.center_bits[:20]

    @property
    def data_bits(self) -> str:
        return self.center_bits[20:]

    @property
    def code_hex(self) -> str:
        return f"0x{int(self.center_bits, 2):06X}"


@dataclass
class ActivityBurst:
    start_sec: float
    end_sec: float
    count: int


@dataclass
class DecodeStageStats:
    stage1_structural: int = 0
    stage2_timing: int = 0


def unique_occurrence_times(
    frames: Sequence[FrameCandidate],
    sample_rate: int,
    merge_sec: float = 0.020,
) -> List[int]:
    if not frames:
        return []
    merge_samples = int(sample_rate * merge_sec)
    times = sorted(f.start_sample for f in frames)
    unique_times = [times[0]]
    for t in times[1:]:
        if (t - unique_times[-1]) > merge_samples:
            unique_times.append(t)
    return unique_times


def count_unique_occurrences(frames: Sequence[FrameCandidate], sample_rate: int, merge_sec: float = 0.020) -> int:
    return len(unique_occurrence_times(frames, sample_rate, merge_sec))


def max_count_in_window(sorted_times: Sequence[int], window_samples: int) -> int:
    if not sorted_times:
        return 0
    best = 1
    left = 0
    for right in range(len(sorted_times)):
        while sorted_times[right] - sorted_times[left] > window_samples:
            left += 1
        best = max(best, right - left + 1)
    return best


def estimate_burst_occurrences(
    frames: Sequence[FrameCandidate],
    sample_rate: int,
    clk_us: float,
    merge_sec: float = 0.020,
) -> int:
    if not frames:
        return 0
    unique_times = unique_occurrence_times(frames, sample_rate, merge_sec=merge_sec)
    # EV1527 normalized frame timing: 24 * (1T+3T) + (1T+31T) = 128T.
    frame_sec = max(0.04, 128.0 * clk_us / 1e6)
    # A button press produces repeated frames in a short burst.
    window_sec = max(0.10, min(0.65, frame_sec * 3.0 + 0.06))
    window_samples = max(1, int(window_sec * sample_rate))
    best = max_count_in_window(unique_times, window_samples)
    # Lost/interfered frames may split a 2-repeat burst outside the tight window.
    if best < 2 and len(unique_times) >= 2:
        min_gap_samples = min((b - a) for a, b in zip(unique_times, unique_times[1:]))
        loose_gap_sec = max(0.22, min(0.60, frame_sec * 9.0 + 0.02))
        if min_gap_samples <= int(loose_gap_sec * sample_rate):
            best = 2
    return best


def detect_activity_bursts(
    frames: Sequence[FrameCandidate],
    sample_rate: int,
    window_sec: float = 0.25,
    top_k: int = 6,
) -> List[ActivityBurst]:
    if not frames:
        return []
    times = sorted(f.start_sample for f in frames)
    win = max(1, int(window_sec * sample_rate))
    spans: List[Tuple[int, int, int]] = []  # count, start_sample, end_sample
    for i, t in enumerate(times):
        j = i
        while j < len(times) and (times[j] - t) <= win:
            j += 1
        spans.append((j - i, t, times[j - 1]))
    spans.sort(reverse=True)

    picked: List[ActivityBurst] = []
    for cnt, st, ed in spans:
        overlap = False
        for b in picked:
            st2 = int(b.start_sec * sample_rate)
            ed2 = int(b.end_sec * sample_rate)
            if not (ed < st2 or st > ed2):
                overlap = True
                break
        if overlap:
            continue
        picked.append(ActivityBurst(start_sec=st / sample_rate, end_sec=ed / sample_rate, count=cnt))
        if len(picked) >= top_k:
            break
    picked.sort(key=lambda x: (x.start_sec, -x.count))
    return picked


def _iter_wav_chunks(raw: bytes) -> Iterable[Tuple[bytes, bytes]]:
    if len(raw) < 12 or raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError("Not a valid RIFF/WAVE file.")
    i = 12
    while i + 8 <= len(raw):
        chunk_id = raw[i : i + 4]
        chunk_size = struct.unpack("<I", raw[i + 4 : i + 8])[0]
        data_start = i + 8
        data_end = data_start + chunk_size
        if data_end > len(raw):
            break
        yield chunk_id, raw[data_start:data_end]
        i = data_end + (chunk_size & 1)


def _decode_ima_adpcm_mono(data: bytes, block_align: int) -> List[int]:
    if block_align < 4:
        raise ValueError("Invalid IMA ADPCM block alignment.")

    out: List[int] = []
    i = 0
    while i + block_align <= len(data):
        block = data[i : i + block_align]
        pred = struct.unpack("<h", block[0:2])[0]
        step_index = block[2]
        if step_index > 88:
            raise ValueError("Invalid IMA ADPCM step index.")
        out.append(pred)
        for byte in block[4:]:
            # WAV IMA ADPCM uses low nibble first.
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
                    pred -= diff
                else:
                    pred += diff
                pred = max(-32768, min(32767, pred))
                step_index += _IMA_INDEX_TABLE[nibble & 7]
                step_index = max(0, min(88, step_index))
                out.append(pred)
        i += block_align
    return out


def read_wav(path: Path) -> WavData:
    raw = path.read_bytes()
    fmt_data: Optional[bytes] = None
    pcm_data: Optional[bytes] = None
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
        # PCM
        if channels < 1:
            raise ValueError("Invalid PCM channel count.")
        if bits_per_sample == 16:
            all_samples = list(struct.unpack("<" + "h" * (len(pcm_data) // 2), pcm_data))
        elif bits_per_sample == 8:
            # Unsigned 8-bit PCM -> signed 16-ish scale
            all_samples = [((b - 128) << 8) for b in pcm_data]
        else:
            raise ValueError(f"Unsupported PCM bit depth: {bits_per_sample}")
        if channels > 1:
            all_samples = all_samples[::channels]
        return WavData(sample_rate=sample_rate, samples=all_samples, format_tag=format_tag)

    if format_tag == 17:
        # IMA ADPCM. Current implementation supports mono capture.
        if channels != 1:
            raise ValueError("IMA ADPCM decoder currently supports mono WAV only.")
        samples = _decode_ima_adpcm_mono(pcm_data, block_align)
        return WavData(sample_rate=sample_rate, samples=samples, format_tag=format_tag)

    raise ValueError(f"Unsupported WAV format tag: {format_tag}")


def moving_average(values: Sequence[float], window: int) -> List[float]:
    if window <= 1:
        return [float(v) for v in values]
    out: List[float] = [0.0] * len(values)
    acc = 0.0
    for i, v in enumerate(values):
        acc += float(v)
        if i >= window:
            acc -= float(values[i - window])
        out[i] = acc / float(min(i + 1, window))
    return out


def run_length_encode(bits: Sequence[int]) -> List[Run]:
    if not bits:
        return []
    runs: List[Run] = []
    start = 0
    cur = bits[0]
    for i in range(1, len(bits)):
        if bits[i] != cur:
            runs.append(Run(level=cur, start=start, length=i - start))
            start = i
            cur = bits[i]
    runs.append(Run(level=cur, start=start, length=len(bits) - start))
    return runs


def merge_short_runs(runs: List[Run], min_len: int) -> List[Run]:
    if len(runs) < 3 or min_len <= 0:
        return [Run(r.level, r.start, r.length) for r in runs]
    merged = [Run(r.level, r.start, r.length) for r in runs]
    i = 1
    while i < len(merged) - 1:
        prev = merged[i - 1]
        cur = merged[i]
        nxt = merged[i + 1]

        # Keep short pulses unless they are tiny spikes sandwiched by much longer runs.
        # This avoids killing true short symbols under low sample rates (8 kHz).
        should_merge = False
        if prev.level == nxt.level and cur.length <= min_len:
            neighbor_min = min(prev.length, nxt.length)
            # Be very conservative at low sample rates: keep 1-sample real pulses.
            if neighbor_min >= max(6, min_len * 4):
                if cur.length * 5 <= neighbor_min:
                    should_merge = True

        if should_merge:
            prev.length += cur.length + nxt.length
            merged.pop(i + 1)
            merged.pop(i)
            if i > 1:
                i -= 1
            continue
        i += 1
    return merged


def hamming_distance(a: str, b: str) -> int:
    return sum(x != y for x, y in zip(a, b))


def decode_frames_from_runs(
    runs: List[Run],
    sample_rate: int,
    source_config: str,
    sample_offset: int,
    min_conf: float,
    timing_profiles: Sequence[TimingProfile],
    stage_stats: Optional[DecodeStageStats] = None,
) -> List[FrameCandidate]:
    out: List[FrameCandidate] = []
    # 125us sampling introduces quantization on every edge duration measurement.
    # Use sample-domain tolerance to avoid over-penalizing small timing deltas.
    sample_quant_tol = 2.0  # samples
    bits_n = EV1527_BITS_PER_FRAME
    for profile in timing_profiles:
        min_clk = sample_rate * ((EV1527_CLK_MIN_US / max(1e-9, profile.clock_divisor)) * 1e-6)
        max_clk = sample_rate * ((EV1527_CLK_MAX_US / max(1e-9, profile.clock_divisor)) * 1e-6)
        pair_t = profile.bit_short_t + profile.bit_long_t

        for i in range(1, len(runs) - 2 * bits_n):
            # Runs must look like:
            # [sync_high][sync_low][bit0_high][bit0_low]...[bit23_high][bit23_low]
            if runs[i - 1].level != 1 or runs[i].level != 0:
                continue

            sync_high = runs[i - 1].length
            # Keep raw sync boundary from edges. Under low sample rates, over-bridging can
            # swallow true short data highs right after sync and break frame phase.
            sync_low = float(runs[i].length)
            bit_start = i + 1
            if bit_start + (2 * bits_n) > len(runs):
                continue

            # Stronger absolute sync guard to reject fake short sync lows.
            sync_low_min = max(45.0, profile.sync_low_t * min_clk * 0.78)
            sync_low_max = profile.sync_low_t * max_clk * 1.45
            if sync_low < sync_low_min or sync_low > sync_low_max:
                continue
            if sync_high < profile.sync_high_t * min_clk * 0.20 or sync_high > profile.sync_high_t * max_clk * 2.20:
                continue

            alternating_ok = True
            for k in range(bit_start, bit_start + 2 * bits_n):
                expected_level = 1 if ((k - bit_start) % 2 == 0) else 0
                if runs[k].level != expected_level:
                    alternating_ok = False
                    break
            if not alternating_ok:
                continue

            # Fast reject: sync low must be the longest low in frame.
            # Under 8 kHz sampling, keep strict order, then apply ratio guard.
            low_runs = [sync_low] + [runs[bit_start + 2 * b + 1].length for b in range(bits_n)]
            max_data_low = max(low_runs[1:])
            if low_runs[0] <= max_data_low:
                continue
            if (low_runs[0] / max(1.0, max_data_low)) < SYNC_LOW_LONGEST_MIN_RATIO:
                continue

            if stage_stats is not None:
                stage_stats.stage1_structural += 1

            totals = [runs[bit_start + 2 * b].length + runs[bit_start + 2 * b + 1].length for b in range(bits_n)]
            clk_from_totals = sorted((t / pair_t for t in totals))[len(totals) // 2]
            clk_from_sync = sync_low / profile.sync_low_t
            # Data section is usually more stable than sync under noisy thresholding.
            clk = 0.80 * clk_from_totals + 0.20 * clk_from_sync
            if clk < min_clk or clk > max_clk:
                continue

            sync_lo_ratio = sync_low / max(1e-9, clk)
            if (
                sync_lo_ratio < profile.sync_low_t * 0.55
                or sync_lo_ratio > profile.sync_low_t * 1.75
            ):
                continue

            bits: List[str] = []
            bit_errs: List[float] = []
            bit_conf: List[float] = []

            def rel_err(obs_len: float, expected_len: float) -> float:
                diff = abs(obs_len - expected_len) - sample_quant_tol
                if diff < 0.0:
                    diff = 0.0
                return diff / max(1.0, expected_len)

            for b in range(bits_n):
                hi = runs[bit_start + 2 * b].length
                lo = runs[bit_start + 2 * b + 1].length
                err_0 = rel_err(hi, profile.bit_short_t * clk) + rel_err(lo, profile.bit_long_t * clk)
                err_1 = rel_err(hi, profile.bit_long_t * clk) + rel_err(lo, profile.bit_short_t * clk)
                margin = abs(err_1 - err_0) / max(1e-6, (err_1 + err_0))
                best_err = err_1 if err_1 < err_0 else err_0
                quality = 1.0 - min(best_err / 3.0, 1.0)
                bit_conf.append(max(0.03, min(1.0, (0.20 + 0.80 * margin) * (0.15 + 0.85 * quality))))
                if err_1 < err_0:
                    bits.append("1")
                    bit_errs.append(err_1)
                else:
                    bits.append("0")
                    bit_errs.append(err_0)
            bit_error = sum(bit_errs) / float(bits_n)
            max_bit_error = max(bit_errs)
            p90_bit_error = sorted(bit_errs)[int(0.90 * (len(bit_errs) - 1))]

            if bit_error > 3.00:
                continue
            if p90_bit_error > 5.00:
                continue
            if max_bit_error > 12.00:
                continue

            period_base = pair_t * clk
            period_jitter = sum(rel_err(total, period_base) for total in totals) / float(bits_n)
            period_spread = (max(totals) - min(totals)) / max(1e-9, period_base)
            period_med = sorted(totals)[len(totals) // 2]
            period_outliers = sum(
                1
                for t in totals
                if abs(t - period_med) > (0.45 * period_med + sample_quant_tol)
            )
            sync_error = rel_err(sync_high, profile.sync_high_t * clk) + rel_err(sync_low, profile.sync_low_t * clk)

            if period_jitter > 1.80:
                continue
            if period_spread > 5.00:
                continue
            if period_outliers > 8:
                continue
            if sync_error > 8.50:
                continue

            low_ratio = low_runs[0] / max(1e-6, (sum(low_runs[1:]) / float(len(low_runs) - 1)))
            if low_ratio < 2.20:
                continue

            # Structural guard from EV1527 timing limits.
            min_run_t = profile.bit_short_t * clk
            max_run_t = profile.sync_low_t * clk
            bad_count = 0
            for k in range(i - 1, bit_start + 2 * bits_n):
                ln = runs[k].length
                if ln < (min_run_t - sample_quant_tol):
                    bad_count += 1
                if ln > (max_run_t + sample_quant_tol):
                    bad_count += 1
            if bad_count > 10:
                continue

            bit_norm = min(bit_error / 3.00, 1.0)
            jitter_norm = min(period_jitter / 1.80, 1.0)
            spread_norm = min(period_spread / 5.00, 1.0)
            outlier_norm = min(period_outliers / 10.0, 1.0)
            sync_error_norm = min(sync_error / 8.50, 1.0)
            sync_ratio_penalty = min(abs(sync_lo_ratio - profile.sync_low_t) / profile.sync_low_t, 1.0)
            low_penalty = max(0.0, (1.8 - low_ratio) / 1.8)
            raw_conf = 1.0 - (
                0.41 * bit_norm
                + 0.19 * jitter_norm
                + 0.14 * spread_norm
                + 0.03 * outlier_norm
                + 0.08 * sync_error_norm
                + 0.05 * sync_ratio_penalty
                + 0.10 * low_penalty
            )
            conf = max(0.0, raw_conf) * min(1.0, low_ratio / 2.2)

            if conf < min_conf:
                continue

            if stage_stats is not None:
                stage_stats.stage2_timing += 1

            start_sample = sample_offset + runs[i - 1].start
            end_sample = sample_offset + (runs[bit_start + 2 * bits_n - 1].start + runs[bit_start + 2 * bits_n - 1].length)
            out.append(
                FrameCandidate(
                    start_sample=start_sample,
                    end_sample=end_sample,
                    clk_us=(clk * 1e6 / sample_rate) * profile.clock_divisor,
                    bits="".join(bits),
                    confidence=conf,
                    bit_error=bit_error,
                    sync_error=sync_error,
                    period_jitter=period_jitter,
                    source_config=f"{source_config}/{profile.name}",
                    bit_confidences=tuple(bit_conf),
                )
            )

    return out


def deduplicate_frames(frames: List[FrameCandidate], sample_rate: int) -> List[FrameCandidate]:
    if not frames:
        return []
    frames = sorted(frames, key=lambda f: f.start_sample)
    time_tol = int(0.010 * sample_rate)  # 10ms

    groups: List[dict] = []
    for fr in frames:
        matched: Optional[dict] = None
        for g in reversed(groups):
            if fr.start_sample - g["max_start"] > time_tol:
                break
            clk_tol = max(35.0, 0.10 * g["clk_mean"])
            if (
                abs(fr.start_sample - g["start_mean"]) <= time_tol
                and abs(fr.clk_us - g["clk_mean"]) <= clk_tol
                and hamming_distance(fr.bits, g["bit_ref"]) <= 8
            ):
                matched = g
                break
        if matched is None:
            groups.append(
                {
                    "items": [fr],
                    "start_mean": float(fr.start_sample),
                    "clk_mean": float(fr.clk_us),
                    "max_start": fr.start_sample,
                    "bit_ref": fr.bits,
                    "bit_ref_conf": fr.confidence,
                }
            )
        else:
            matched["items"].append(fr)
            n = float(len(matched["items"]))
            matched["start_mean"] = (matched["start_mean"] * (n - 1.0) + fr.start_sample) / n
            matched["clk_mean"] = (matched["clk_mean"] * (n - 1.0) + fr.clk_us) / n
            matched["max_start"] = max(matched["max_start"], fr.start_sample)
            if fr.confidence >= matched["bit_ref_conf"]:
                matched["bit_ref"] = fr.bits
                matched["bit_ref_conf"] = fr.confidence

    merged: List[FrameCandidate] = []
    for g in groups:
        items: List[FrameCandidate] = g["items"]
        obs_count = len(items)
        cfg_count = len({x.source_config for x in items})
        if obs_count == 0:
            continue

        bit_list: List[str] = []
        margin_sum = 0.0
        bit_margins: List[float] = []
        for b_idx in range(24):
            w0 = 0.0
            w1 = 0.0
            for it in items:
                bit_rel = it.bit_confidences[b_idx] if b_idx < len(it.bit_confidences) else 1.0
                w = max(0.02, it.confidence * max(0.05, bit_rel))
                if it.bits[b_idx] == "1":
                    w1 += w
                else:
                    w0 += w
            total = w0 + w1
            if total <= 1e-9:
                bit_list.append(items[0].bits[b_idx])
                bit_margins.append(0.0)
                continue
            if w1 >= w0:
                bit_list.append("1")
                margin = (w1 - w0) / total
                margin_sum += margin
                bit_margins.append(margin)
            else:
                bit_list.append("0")
                margin = (w0 - w1) / total
                margin_sum += margin
                bit_margins.append(margin)
        vote_margin = margin_sum / 24.0
        strong_bits = sum(1 for m in bit_margins if m >= 0.22)
        ones_count = bit_list.count("1")

        sum_conf = sum(max(0.01, it.confidence) for it in items)
        avg_conf = sum(it.confidence for it in items) / float(obs_count)
        clk_us = sum(it.clk_us * max(0.01, it.confidence) for it in items) / sum_conf
        start_sample = int(round(sum(it.start_sample * max(0.01, it.confidence) for it in items) / sum_conf))
        end_sample = max(it.end_sample for it in items)
        bit_error = sum(it.bit_error for it in items) / float(obs_count)
        sync_error = sum(it.sync_error for it in items) / float(obs_count)
        period_jitter = sum(it.period_jitter for it in items) / float(obs_count)

        support_factor = min(1.40, 1.0 + 0.10 * (cfg_count - 1))
        obs_factor = min(1.25, 1.0 + 0.05 * (obs_count - 1))
        vote_factor = 0.55 + 0.45 * vote_margin
        merged_conf = min(1.0, avg_conf * support_factor * obs_factor * vote_factor)

        # Reject weak single-observation frames that are likely noise.
        if obs_count <= 1 and cfg_count <= 1 and merged_conf < 0.50:
            continue
        if vote_margin < 0.10 and merged_conf < 0.60:
            continue
        # If too many bits are ambiguous, this frame is likely threshold/noise artifact.
        if strong_bits < 6:
            continue
        # Very extreme 24-bit patterns are usually generated by noisy clipping.
        if ones_count <= 0 or ones_count >= 24:
            continue

        merged.append(
            FrameCandidate(
                start_sample=start_sample,
                end_sample=end_sample,
                clk_us=clk_us,
                bits="".join(bit_list),
                confidence=merged_conf,
                bit_error=bit_error,
                sync_error=sync_error,
                period_jitter=period_jitter,
                source_config=f"merged[{cfg_count}cfg/{obs_count}obs]",
                support_count=cfg_count,
                observation_count=obs_count,
                bit_vote_margin=vote_margin,
                bit_confidences=tuple(bit_margins),
            )
        )

    merged = suppress_overlapping_frames(merged)
    merged.sort(key=lambda f: f.start_sample)
    return merged


def suppress_overlapping_frames(frames: List[FrameCandidate]) -> List[FrameCandidate]:
    if len(frames) <= 1:
        return list(frames)

    ranked = sorted(
        frames,
        key=lambda f: (f.confidence * max(1, f.observation_count), f.support_count, -f.start_sample),
        reverse=True,
    )
    kept: List[FrameCandidate] = []
    for fr in ranked:
        drop = False
        for k in kept:
            ov = min(fr.end_sample, k.end_sample) - max(fr.start_sample, k.start_sample)
            if ov <= 0:
                continue
            dur_fr = max(1, fr.end_sample - fr.start_sample)
            dur_k = max(1, k.end_sample - k.start_sample)
            ov_ratio = ov / float(min(dur_fr, dur_k))
            if ov_ratio < OVERLAP_DROP_MIN_RATIO:
                continue
            clk_close = abs(fr.clk_us - k.clk_us) <= max(50.0, 0.15 * max(fr.clk_us, k.clk_us))
            bits_close = hamming_distance(fr.bits, k.bits) <= 10
            if clk_close and bits_close:
                drop = True
                break
        if not drop:
            kept.append(fr)
    return kept


def cluster_frames(frames: List[FrameCandidate], sample_rate: int) -> List[Cluster]:
    if not frames:
        return []

    def frame_duration_sec(f: FrameCandidate) -> float:
        return max(0.04, 128.0 * f.clk_us / 1e6)

    # Step 1: split by temporal bursts. Do not merge across long gaps.
    by_time = sorted(frames, key=lambda f: f.start_sample)
    bursts: List[List[FrameCandidate]] = [[by_time[0]]]
    for fr in by_time[1:]:
        prev = bursts[-1][-1]
        gap_sec = (fr.start_sample - prev.start_sample) / float(sample_rate)
        gap_limit = min(0.28, max(0.08, 1.4 * frame_duration_sec(prev), 1.4 * frame_duration_sec(fr)))
        if gap_sec <= gap_limit:
            bursts[-1].append(fr)
        else:
            bursts.append([fr])

    # Step 2: cluster only inside each burst.
    all_clusters: List[Cluster] = []
    for burst in bursts:
        local_clusters: List[Cluster] = []
        for fr in sorted(
            burst,
            key=lambda x: (-(x.confidence * max(1, x.observation_count)), x.start_sample),
        ):
            assigned = False
            for cl in local_clusters:
                if abs(fr.clk_us - cl.mean_clk_us) > max(40.0, 0.10 * cl.mean_clk_us):
                    continue
                if hamming_distance(fr.bits, cl.center_bits) > 6:
                    continue
                cl.add(fr)
                assigned = True
                break
            if not assigned:
                cl = Cluster(center_bits=fr.bits, mean_clk_us=fr.clk_us)
                cl.add(fr)
                local_clusters.append(cl)
        all_clusters.extend(local_clusters)

    return all_clusters


def cluster_hamming_stats(cluster: Cluster) -> Tuple[float, int]:
    if not cluster.frames:
        return 0.0, 0
    dists = [hamming_distance(f.bits, cluster.center_bits) for f in cluster.frames]
    return (sum(dists) / float(len(dists)), max(dists))


def bit_distance_with_shift(a: str, b: str) -> int:
    if len(a) != len(b):
        n = min(len(a), len(b))
        return sum(a[i] != b[i] for i in range(n)) + abs(len(a) - len(b))
    if len(a) <= 1:
        return hamming_distance(a, b)
    direct = hamming_distance(a, b)
    shift_r = sum(a[i] != b[i + 1] for i in range(len(a) - 1)) + 1
    shift_l = sum(a[i + 1] != b[i] for i in range(len(a) - 1)) + 1
    return min(direct, shift_r, shift_l)


def cluster_avg_errors(cluster: Cluster) -> Tuple[float, float, float, float]:
    if not cluster.frames:
        return 1.0, 1.0, 1.0, 1.0
    bes = [f.bit_error for f in cluster.frames]
    ses = [f.sync_error for f in cluster.frames]
    pjs = [f.period_jitter for f in cluster.frames]
    return (
        sum(bes) / len(bes),
        min(bes),
        sum(ses) / len(ses),
        sum(pjs) / len(pjs),
    )


def cluster_quality_score(cluster: Cluster, uniq: int, burst: int) -> float:
    avg_be, min_be, avg_se, avg_pj = cluster_avg_errors(cluster)
    return (
        cluster.avg_confidence
        - 1.20 * avg_be
        - 0.40 * avg_pj
        - 0.20 * avg_se
        + 0.10 * (1.0 - min(1.0, min_be))
        + 0.03 * min(uniq, 6)
        + 0.03 * min(burst, 6)
    )


def cluster_polluted(cluster: Cluster) -> bool:
    avg_be, _min_be, _avg_se, avg_pj = cluster_avg_errors(cluster)
    return (
        cluster.avg_confidence < 0.72
        or avg_be > 0.28
        or avg_pj > 0.22
    )


def cluster_passes_basic_quality(cluster: Cluster) -> bool:
    avg_be, _min_be, _avg_se, avg_pj = cluster_avg_errors(cluster)
    return (
        cluster.avg_confidence >= MIN_CLUSTER_AVG_CONFIDENCE
        and avg_be <= MAX_CLUSTER_AVG_BIT_ERROR
        and avg_pj <= MAX_CLUSTER_AVG_JITTER
    )


def overlaps_dominant_window(cluster: Cluster, sample_rate: int, start_sec: float, end_sec: float, pad_sec: float) -> bool:
    st = cluster.first_sample / float(sample_rate)
    ed = cluster.last_sample / float(sample_rate)
    return not (ed < (start_sec - pad_sec) or st > (end_sec + pad_sec))


def cluster_overlaps_activity_burst(
    cluster: Cluster,
    sample_rate: int,
    burst: ActivityBurst,
    pad_sec: float = 0.20,
) -> bool:
    st = cluster.first_sample / float(sample_rate)
    ed = cluster.last_sample / float(sample_rate)
    return not (ed < (burst.start_sec - pad_sec) or st > (burst.end_sec + pad_sec))


def clusters_share_activity_burst(
    a: Cluster,
    b: Cluster,
    sample_rate: int,
    bursts: Sequence[ActivityBurst],
    pad_sec: float = 0.20,
) -> bool:
    for burst in bursts:
        if cluster_overlaps_activity_burst(a, sample_rate, burst, pad_sec) and cluster_overlaps_activity_burst(
            b, sample_rate, burst, pad_sec
        ):
            return True
    return False


def clusters_same_family(
    a: Cluster,
    b: Cluster,
    sample_rate: int,
    bursts: Optional[Sequence[ActivityBurst]] = None,
) -> bool:
    clk_close = abs(a.mean_clk_us - b.mean_clk_us) <= max(25.0, 0.10 * max(a.mean_clk_us, b.mean_clk_us))
    if not clk_close:
        return False
    dist = bit_distance_with_shift(a.center_bits, b.center_bits)
    polluted = cluster_polluted(a) or cluster_polluted(b)
    share_burst = bool(bursts) and clusters_share_activity_burst(a, b, sample_rate, bursts, pad_sec=0.20)
    strict_singleton_bridge = (
        share_burst
        and dist <= 5
        and (len(a.frames) <= 1 or len(b.frames) <= 1)
        and abs(a.mean_clk_us - b.mean_clk_us) <= max(18.0, 0.06 * max(a.mean_clk_us, b.mean_clk_us))
    )
    if dist > (6 if polluted else 4) and not strict_singleton_bridge:
        return False
    if a.center_bits == b.center_bits:
        max_gap_sec = 1.20
    elif dist <= 2:
        max_gap_sec = 0.80
    elif dist <= 4:
        max_gap_sec = 0.55
    else:
        max_gap_sec = 0.45
    time_close = not (
        (a.last_sample / float(sample_rate)) < (b.first_sample / float(sample_rate) - max_gap_sec)
        or (b.last_sample / float(sample_rate)) < (a.first_sample / float(sample_rate) - max_gap_sec)
    )
    if time_close:
        return True
    if share_burst:
        return True
    return False


def rows_overlap_in_time(
    a: Tuple[Cluster, int, int, float],
    b: Tuple[Cluster, int, int, float],
    sample_rate: int,
) -> bool:
    a0 = a[0].first_sample / float(sample_rate)
    a1 = a[0].last_sample / float(sample_rate)
    b0 = b[0].first_sample / float(sample_rate)
    b1 = b[0].last_sample / float(sample_rate)
    return not (a1 <= b0 or b1 <= a0)


def suppress_overlapping_cluster_rows(
    rows: List[Tuple[Cluster, int, int, float]],
    sample_rate: int,
) -> Tuple[List[Tuple[Cluster, int, int, float]], List[str]]:
    ranked = sorted(rows, key=lambda r: (r[2], r[1], r[3]), reverse=True)
    kept: List[Tuple[Cluster, int, int, float]] = []
    warnings: List[str] = []
    for row in ranked:
        dropped = False
        for cur in kept:
            if not rows_overlap_in_time(row, cur, sample_rate):
                continue
            warnings.append(
                "overlap_drop:"
                f"{row[0].code_hex}@{row[0].first_sample/float(sample_rate):.3f}-{row[0].last_sample/float(sample_rate):.3f}s"
                " vs "
                f"{cur[0].code_hex}@{cur[0].first_sample/float(sample_rate):.3f}-{cur[0].last_sample/float(sample_rate):.3f}s"
            )
            dropped = True
            break
        if not dropped:
            kept.append(row)
    kept.sort(key=lambda r: (r[0].first_sample, r[0].last_sample))
    return kept, warnings


def detect_interleaving_patterns(
    rows: Sequence[Tuple[Cluster, int, int, float]],
) -> List[str]:
    # Detect A-B-A pattern by per-frame temporal sequence.
    events: List[Tuple[int, int]] = []
    for idx, row in enumerate(rows):
        for fr in row[0].frames:
            events.append((fr.start_sample, idx))
    if len(events) < 3:
        return []
    events.sort(key=lambda x: x[0])
    seq: List[int] = []
    for _t, idx in events:
        if not seq or seq[-1] != idx:
            seq.append(idx)
    warns: List[str] = []
    for i in range(len(seq) - 2):
        a = seq[i]
        b = seq[i + 1]
        c = seq[i + 2]
        if a == c and a != b:
            warns.append(
                f"interleave_pattern:{rows[a][0].code_hex}-{rows[b][0].code_hex}-{rows[c][0].code_hex}"
            )
    return warns


def select_cluster_rows(
    all_frames: Sequence[FrameCandidate],
    clusters: Sequence[Cluster],
    sample_rate: int,
    min_occurrences: int,
    min_burst_occurrences: int,
) -> Tuple[List[Tuple[Cluster, int, int, float]], Optional[ActivityBurst], List[str]]:
    cluster_rows: List[Tuple[Cluster, int, int, float]] = []
    bursts = detect_activity_bursts(all_frames, sample_rate, window_sec=0.25, top_k=20)
    for c in clusters:
        if not cluster_passes_basic_quality(c):
            continue
        uniq = count_unique_occurrences(c.frames, sample_rate)
        burst = estimate_burst_occurrences(c.frames, sample_rate, c.mean_clk_us)
        if c.avg_confidence < 0.65 and burst < 3:
            # Very likely polluted singleton family.
            continue
        score = cluster_quality_score(c, uniq, burst)
        cluster_rows.append((c, uniq, burst, score))

    if not cluster_rows:
        return [], None, []

    dominant_burst: Optional[ActivityBurst] = None
    top_bursts = bursts[:6]
    if top_bursts:
        by_count = sorted(top_bursts, key=lambda x: x.count, reverse=True)
        top = by_count[0]
        second = by_count[1].count if len(by_count) > 1 else 0
        if top.count >= 20 and top.count >= int(math.ceil(1.15 * max(1, second))):
            dominant_burst = top
            padded = [
                row
                for row in cluster_rows
                if overlaps_dominant_window(row[0], sample_rate, top.start_sec, top.end_sec, pad_sec=0.12)
            ]
            if padded:
                cluster_rows = padded

    cluster_rows.sort(key=lambda row: row[3], reverse=True)
    family_selected: List[Tuple[Cluster, int, int, float]] = []
    for row in cluster_rows:
        replaced = False
        for i, cur in enumerate(family_selected):
            if not clusters_same_family(row[0], cur[0], sample_rate, bursts=bursts):
                continue
            replaced = True
            # Aggregate repeats by family (damaged frames count to same repeat family).
            fam_frames = list(family_selected[i][0].frames) + list(row[0].frames)
            fam_uniq = count_unique_occurrences(fam_frames, sample_rate)
            fam_clk = (
                (family_selected[i][0].mean_clk_us * len(family_selected[i][0].frames))
                + (row[0].mean_clk_us * len(row[0].frames))
            ) / max(1, (len(family_selected[i][0].frames) + len(row[0].frames)))
            fam_burst = estimate_burst_occurrences(fam_frames, sample_rate, fam_clk)
            best = row if row[3] > cur[3] else cur
            family_selected[i] = (best[0], fam_uniq, fam_burst, best[3])
            break
        if not replaced:
            family_selected.append(row)

    eff_min_occ = max(1, min_occurrences)
    eff_min_burst = max(MIN_EFFECTIVE_BURST_REPEAT, min_burst_occurrences)
    filtered = [r for r in family_selected if r[1] >= eff_min_occ and r[2] >= eff_min_burst]
    confidence_filtered: List[Tuple[Cluster, int, int, float]] = []
    for row in filtered:
        cl, _uniq, burst, score = row
        # Confidence-first denoise:
        # single-frame representative with low quality is usually a polluted split frame.
        if len(cl.frames) <= 1 and score < 0.95:
            continue
        if cluster_polluted(cl) and burst < 3:
            continue
        confidence_filtered.append(row)
    filtered = confidence_filtered
    filtered.sort(key=lambda row: (row[2], row[1], row[3]), reverse=True)
    no_overlap, overlap_warnings = suppress_overlapping_cluster_rows(filtered, sample_rate)
    interleave_warnings = detect_interleaving_patterns(no_overlap)
    warnings = overlap_warnings + interleave_warnings
    no_overlap.sort(key=lambda row: (row[2], row[1], row[3]), reverse=True)
    return no_overlap, dominant_burst, warnings


def build_timing_profiles() -> List[TimingProfile]:
    # Decode on normalized 1/3/31 ratio.
    # Here one unit is the 4T short pulse (4 high clocks).
    # This keeps user-provided clk range directly usable under 8 kHz quantization.
    return [
        TimingProfile(
            name="ratio-1x",
            bit_short_t=1.0,
            bit_long_t=3.0,
            sync_high_t=1.0,
            sync_low_t=31.0,
            clock_divisor=1.0,
        ),
    ]


def build_preprocess_configs() -> List[PreprocessConfig]:
    configs: List[PreprocessConfig] = []
    # User-confirmed level threshold: 0 split (>=0 high, <0 low).
    # At 8 kHz, valid short symbols may be 1~2 samples.
    # Use light smoothing and conservative spike-merge only.
    for window in (1, 2, 3, 4, 6, 8):
        if window <= 2:
            glitch_lens = (0, 1, 2, 3)
        elif window <= 4:
            glitch_lens = (0, 1, 2, 3, 4)
        else:
            glitch_lens = (0, 1, 2, 3, 4, 5)
        for min_run in glitch_lens:
            name = f"zero-w{window}-gl{min_run}"
            configs.append(
                PreprocessConfig(
                    name=name,
                    smooth_window=window,
                    min_run=min_run,
                )
            )
    return configs


def decode_capture(
    wav: WavData,
    start_sec: float,
    end_sec: Optional[float],
    min_frame_conf: float,
    verbose: bool,
) -> Tuple[List[FrameCandidate], List[Cluster], int, int, DecodeStageStats]:
    sr = wav.sample_rate
    total_samples = len(wav.samples)
    start_idx = max(0, int(start_sec * sr))
    end_idx = total_samples if end_sec is None else min(total_samples, int(end_sec * sr))
    if start_idx >= end_idx:
        raise ValueError("Selected time range is empty.")

    segment = wav.samples[start_idx:end_idx]
    configs = build_preprocess_configs()
    timing_profiles = build_timing_profiles()
    all_frames: List[FrameCandidate] = []
    total_stage_stats = DecodeStageStats()

    for cfg in configs:
        cfg_stage_stats = DecodeStageStats()
        raw_signal = [float(x) for x in segment]
        smoothed = moving_average(raw_signal, cfg.smooth_window)
        # Fixed threshold at 0: >=0 => high, <0 => low
        bits = [1 if v >= 0.0 else 0 for v in smoothed]
        runs = run_length_encode(bits)
        runs = merge_short_runs(runs, cfg.min_run)
        frames = decode_frames_from_runs(
            runs=runs,
            sample_rate=sr,
            source_config=cfg.name,
            sample_offset=start_idx,
            min_conf=min_frame_conf,
            timing_profiles=timing_profiles,
            stage_stats=cfg_stage_stats,
        )
        all_frames.extend(frames)
        total_stage_stats.stage1_structural += cfg_stage_stats.stage1_structural
        total_stage_stats.stage2_timing += cfg_stage_stats.stage2_timing

        if verbose:
            print(
                f"[{cfg.name}] runs={len(runs)} step1={cfg_stage_stats.stage1_structural} "
                f"step2={cfg_stage_stats.stage2_timing} frames={len(frames)} threshold=0"
            )

    deduped = deduplicate_frames(all_frames, sr)
    clusters = cluster_frames(deduped, sr)
    return all_frames, clusters, start_idx, end_idx, total_stage_stats


def print_report(
    wav: WavData,
    all_frames: List[FrameCandidate],
    clusters: List[Cluster],
    stage_stats: DecodeStageStats,
    frames_total: int,
    frames_deduped: int,
    start_idx: int,
    end_idx: int,
    top_n: int,
    min_occurrences: int,
    min_burst_occurrences: int,
) -> None:
    sr = wav.sample_rate
    duration = len(wav.samples) / float(sr)
    sample_period_us = 1e6 / float(sr)
    print(
        f"WAV format_tag={wav.format_tag}, sample_rate={sr} Hz, "
        f"sample_period={sample_period_us:.3f} us, duration={duration:.3f}s"
    )
    print(f"Search range: {start_idx/sr:.3f}s - {end_idx/sr:.3f}s")
    print(f"Frame candidates: raw={frames_total}, deduped={frames_deduped}")
    print(
        f"Three-stage stats: step1_structural={stage_stats.stage1_structural}, "
        f"step2_timing={stage_stats.stage2_timing}"
    )

    bursts = detect_activity_bursts(all_frames, sr, window_sec=0.25, top_k=6)
    if bursts:
        print("Activity bursts (time-only, independent of bit stability):")
        for b in bursts:
            print(f"  burst count={b.count:2d} window={b.start_sec:.3f}-{b.end_sec:.3f}s")

    if not clusters:
        print("No candidate frame found. Try lower --min-frame-confidence or adjust time range.")
        return

    filtered_rows, dominant_burst, sanity_warnings = select_cluster_rows(
        all_frames=all_frames,
        clusters=clusters,
        sample_rate=sr,
        min_occurrences=min_occurrences,
        min_burst_occurrences=min_burst_occurrences,
    )
    print(f"Three-stage stats: step3_repeat_clusters={len(filtered_rows)}")
    if dominant_burst is not None:
        print(
            f"Dominant burst focus: window={dominant_burst.start_sec:.3f}-{dominant_burst.end_sec:.3f}s "
            f"count={dominant_burst.count}"
        )
    if sanity_warnings:
        print(f"Sanity warnings: {len(sanity_warnings)}")

    print("")
    if not filtered_rows:
        print(
            f"No cluster passed repeat-aware filter (occ>={min_occurrences}, "
            f"burst>={min_burst_occurrences})."
        )
        return

    print(
        f"Top clusters (repeat-aware, filtered: occ>={min_occurrences}, "
        f"burst>={min_burst_occurrences}):"
    )
    shown = 0
    for cl, uniq, burst, score in filtered_rows:
        if shown >= top_n:
            break
        shown += 1
        first_t = cl.first_sample / float(sr)
        last_t = cl.last_sample / float(sr)
        avg_hd, max_hd = cluster_hamming_stats(cl)
        avg_be, min_be, _avg_se, avg_pj = cluster_avg_errors(cl)
        print(
            f"{shown:2d}. code={cl.code_hex} bits={cl.center_bits} "
            f"repeat={burst} frames={len(cl.frames)} "
            f"repeat_hd_avg={avg_hd:.2f} repeat_hd_max={max_hd} "
            f"bit_err_avg={avg_be:.3f} bit_err_min={min_be:.3f} jitter_avg={avg_pj:.3f} "
            f"score={score:.3f} "
            f"clk={cl.mean_clk_us:.1f}us time={first_t:.3f}-{last_t:.3f}s"
        )


def serialize_clusters(
    all_frames: Sequence[FrameCandidate],
    clusters: List[Cluster],
    sample_rate: int,
    min_occurrences: int,
    min_burst_occurrences: int,
    include_all: bool,
) -> List[dict]:
    if include_all:
        rows = []
        for cluster in clusters:
            unique_occ = count_unique_occurrences(cluster.frames, sample_rate)
            burst_occ = estimate_burst_occurrences(cluster.frames, sample_rate, cluster.mean_clk_us)
            rows.append((cluster, unique_occ, burst_occ, cluster_quality_score(cluster, unique_occ, burst_occ)))
        rows.sort(key=lambda row: (row[2], row[1], row[3]), reverse=True)
    else:
        # Default JSON follows the same repeat-aware sanity filters as the text report.
        rows, _, _ = select_cluster_rows(
            all_frames=all_frames,
            clusters=clusters,
            sample_rate=sample_rate,
            min_occurrences=max(1, min_occurrences),
            min_burst_occurrences=max(MIN_EFFECTIVE_BURST_REPEAT, min_burst_occurrences),
        )

    data: List[dict] = []
    for cl, unique_occ, burst_occ, score in rows:
        frames = sorted(cl.frames, key=lambda f: f.start_sample)
        avg_hd, max_hd = cluster_hamming_stats(cl)
        is_repeat_valid = (unique_occ >= min_occurrences) and (burst_occ >= min_burst_occurrences)
        avg_be, min_be, _avg_se, avg_pj = cluster_avg_errors(cl)
        data.append(
            {
                "code_hex": cl.code_hex,
                "bits": cl.center_bits,
                "repeat_count": burst_occ,
                "frame_count": len(frames),
                "repeat_valid": is_repeat_valid,
                "repeat_hamming_avg": avg_hd,
                "repeat_hamming_max": max_hd,
                "bit_error_avg": avg_be,
                "bit_error_min": min_be,
                "period_jitter_avg": avg_pj,
                "quality_score": score,
                "mean_clk_us": cl.mean_clk_us,
                "first_time_sec": frames[0].start_sample / float(sample_rate),
                "last_time_sec": frames[-1].start_sample / float(sample_rate),
                "frames": [
                    {
                        "time_sec": f.start_sample / float(sample_rate),
                        "bits": f.bits,
                        "clk_us": f.clk_us,
                        "confidence": f.confidence,
                    }
                    for f in frames
                ],
            }
        )
    return data


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="EV1527 unknown-clock decoder for noisy WAV captures.")
    p.add_argument("--wav", type=Path, required=True, help="Path to WAV capture file.")
    p.add_argument("--start-sec", type=float, default=0.0, help="Search start time in seconds.")
    p.add_argument("--end-sec", type=float, default=None, help="Search end time in seconds.")
    p.add_argument(
        "--min-frame-confidence",
        type=float,
        default=0.20,
        help="Minimum per-frame confidence to keep (0~1).",
    )
    p.add_argument("--top", type=int, default=8, help="Number of top clusters to print.")
    p.add_argument(
        "--min-occurrences",
        type=int,
        default=3,
        help="Keep clusters with at least this many unique occurrences.",
    )
    p.add_argument(
        "--min-burst-occurrences",
        type=int,
        default=3,
        help="Keep clusters with at least this many occurrences inside one short burst window.",
    )
    p.add_argument(
        "--json-include-all-clusters",
        action="store_true",
        help="When --json-out is used, include all clusters (default: only repeat-valid clusters).",
    )
    p.add_argument("--json-out", type=Path, default=None, help="Optional output JSON path.")
    p.add_argument("--verbose", action="store_true", help="Print per-config diagnostics.")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    wav = read_wav(args.wav)
    all_frames, clusters, start_idx, end_idx, stage_stats = decode_capture(
        wav=wav,
        start_sec=args.start_sec,
        end_sec=args.end_sec,
        min_frame_conf=args.min_frame_confidence,
        verbose=args.verbose,
    )
    deduped_count = sum(len(c.frames) for c in clusters)
    print_report(
        wav=wav,
        all_frames=all_frames,
        clusters=clusters,
        stage_stats=stage_stats,
        frames_total=len(all_frames),
        frames_deduped=deduped_count,
        start_idx=start_idx,
        end_idx=end_idx,
        top_n=max(1, args.top),
        min_occurrences=max(1, args.min_occurrences),
        min_burst_occurrences=max(1, args.min_burst_occurrences),
    )

    if args.json_out is not None:
        bursts = detect_activity_bursts(all_frames, wav.sample_rate, window_sec=0.25, top_k=20)
        min_occ = max(1, args.min_occurrences)
        min_burst = max(1, args.min_burst_occurrences)
        selected_rows, dominant_burst, sanity_warnings = select_cluster_rows(
            all_frames=all_frames,
            clusters=clusters,
            sample_rate=wav.sample_rate,
            min_occurrences=min_occ,
            min_burst_occurrences=min_burst,
        )
        step3_repeat_clusters = len(selected_rows)
        serialized_clusters = serialize_clusters(
            all_frames=all_frames,
            clusters=clusters,
            sample_rate=wav.sample_rate,
            min_occurrences=min_occ,
            min_burst_occurrences=min_burst,
            include_all=bool(args.json_include_all_clusters),
        )
        payload = {
            "wav": str(args.wav),
            "sample_rate": wav.sample_rate,
            "format_tag": wav.format_tag,
            "start_sec": start_idx / float(wav.sample_rate),
            "end_sec": end_idx / float(wav.sample_rate),
            "repeat_filter": {
                "min_occurrences": max(1, min_occ),
                "min_burst_occurrences": max(MIN_EFFECTIVE_BURST_REPEAT, min_burst),
                "basic_quality": {
                    "min_cluster_avg_confidence": MIN_CLUSTER_AVG_CONFIDENCE,
                    "max_cluster_avg_bit_error": MAX_CLUSTER_AVG_BIT_ERROR,
                    "max_cluster_avg_jitter": MAX_CLUSTER_AVG_JITTER,
                },
            },
            "three_stage_stats": {
                "step1_structural": stage_stats.stage1_structural,
                "step2_timing": stage_stats.stage2_timing,
                "step3_repeat_clusters": step3_repeat_clusters,
            },
            "dominant_burst": (
                None
                if dominant_burst is None
                else {
                    "start_sec": dominant_burst.start_sec,
                    "end_sec": dominant_burst.end_sec,
                    "count": dominant_burst.count,
                }
            ),
            "activity_bursts": [
                {"start_sec": b.start_sec, "end_sec": b.end_sec, "count": b.count} for b in bursts
            ],
            "sanity_warnings": sanity_warnings,
            "clusters": serialized_clusters,
        }
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"\nJSON written to: {args.json_out}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
