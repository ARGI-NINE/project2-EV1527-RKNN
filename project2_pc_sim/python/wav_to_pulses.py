#!/usr/bin/env python3
"""
Stage-1 RF preprocessing: WAV -> candidate-frame JSON.

This stage is limited to MCU-like candidate filtering only:
- full-WAV flip preprocessing
- fixed-width flip-window candidate generation
- first low pulse is the longest low pulse in the frame
- EV1527 base-clock range guard derived from first_low_us / 124.0

It does not perform backend decode-confidence, repeat judgement, or payload
accept/reject logic. Those belong to the later backend stage.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path
from typing import Any


DECODER_MIN_FRAME_CONFIDENCE_DEFAULT = 0.20
DECODER_MIN_OCCURRENCES_DEFAULT = 1
DECODER_MIN_BURST_OCCURRENCES_DEFAULT = 1
EV1527_BITS = 24
EV1527_FRAME_PULSES = 2 + EV1527_BITS * 2
EV1527_SYNC_LOW_T = 124.0
EV1527_CLK_MIN_US = 230.0
EV1527_CLK_MAX_US = 4240.0
EV1527_STAGE1_CLK_MIN_US = EV1527_CLK_MIN_US / 4.0
EV1527_STAGE1_CLK_MAX_US = EV1527_CLK_MAX_US / 4.0


def _first_low_longest_for_phase(pulse: list[int], low_start_index: int) -> bool:
    if low_start_index >= len(pulse):
        return False
    first_low = int(pulse[low_start_index])
    if first_low <= 0:
        return False
    for i in range(low_start_index + 2, len(pulse), 2):
        if int(pulse[i]) > first_low:
            return False
    return True


def _find_first_low_index(pulse: list[int]) -> int | None:
    if len(pulse) < 4:
        return None
    if _first_low_longest_for_phase(pulse, 1):
        return 1
    if _first_low_longest_for_phase(pulse, 0):
        return 0
    return None


def _estimate_candidate_clk_us(pulse: list[int]) -> float:
    first_low_index = _find_first_low_index(pulse)
    if first_low_index is None:
        return 0.0
    first_low_us = float(pulse[first_low_index])
    if first_low_us <= 0.0:
        return 0.0
    # Stage-1 uses the MCU-side base clock from the sync-low width.
    # Backend decode later works on the 4x short-pulse clock.
    return first_low_us / EV1527_SYNC_LOW_T


def load_decoder_module(project2_dir: Path) -> Any:
    mod_path = project2_dir / "ev1527_decode.py"
    spec = importlib.util.spec_from_file_location("ev1527_decode_mod", mod_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load {mod_path}")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


def _to_us(samples: int, sample_rate: int) -> int:
    if sample_rate <= 0:
        return 0
    return int(round(float(samples) * 1_000_000.0 / float(sample_rate)))


def _candidate_row(
    *,
    run_idx: int,
    pulse: list[int],
    start_sample: int,
    sample_rate: int,
) -> dict[str, Any] | None:
    first_low_index = _find_first_low_index(pulse)
    if first_low_index is None:
        return None

    clk_us = _estimate_candidate_clk_us(pulse)
    if clk_us < EV1527_STAGE1_CLK_MIN_US or clk_us > EV1527_STAGE1_CLK_MAX_US:
        return None

    low_values = [int(pulse[i]) for i in range(first_low_index, len(pulse), 2)]
    if not low_values:
        return None

    first_low_us = int(pulse[first_low_index])
    return {
        "start_sec": float(start_sample) / float(sample_rate),
        "pulse": pulse,
        "run_index": int(run_idx),
        "pulse_count": len(pulse),
        "flip_count": max(0, len(pulse) - 1),
        "first_low_index": int(first_low_index),
        "first_low_us": first_low_us,
        "max_low_us": max(low_values),
        "first_low_is_longest": True,
        "candidate_clk_us": round(clk_us, 2),
    }


def _build_runs(decoder_mod: Any, samples: list[int], smooth_window: int, min_run_samples: int):
    raw_signal = [float(x) for x in samples]
    smoothed = decoder_mod.moving_average(raw_signal, max(1, smooth_window))
    levels = [1 if value >= 0.0 else 0 for value in smoothed]
    runs = decoder_mod.run_length_encode(levels)
    return decoder_mod.merge_short_runs(runs, max(0, min_run_samples))


def _pulse_in_range(pulse: list[int], min_pulse_us: int, max_pulse_us: int) -> bool:
    for pulse_us in pulse:
        if pulse_us < min_pulse_us or pulse_us > max_pulse_us:
            return False
    return True


def _segment_frames_from_runs(
    runs: list[Any],
    sample_rate: int,
    sample_offset: int,
    min_pulse_us: int,
    max_pulse_us: int,
    sync_us: int,
    min_frame_pulses: int,
) -> tuple[list[dict[str, Any]], dict[str, int]]:
    frames: list[dict[str, Any]] = []
    current_pulse: list[int] = []
    current_run_indices: list[int] = []
    stats = {
        "run_count": len(runs),
        "discarded_out_of_range_pulses": 0,
        "sync_flushes": 0,
        "carry_pulse_flushes": 0,
        "idle_flushes": 0,
        "segmented_frames": 0,
    }

    def flush_current() -> None:
        if len(current_pulse) < min_frame_pulses or not current_run_indices:
            return
        start_run_idx = int(current_run_indices[0])
        frames.append(
            {
                "run_index": start_run_idx,
                "pulse": list(current_pulse),
                "start_sample": sample_offset + int(runs[start_run_idx].start),
            }
        )
        stats["segmented_frames"] += 1

    for run_idx, run in enumerate(runs):
        pulse_us = _to_us(int(run.length), sample_rate)
        if pulse_us < min_pulse_us or pulse_us > max_pulse_us:
            stats["discarded_out_of_range_pulses"] += 1
            continue

        if pulse_us > sync_us and len(current_pulse) >= min_frame_pulses:
            stats["sync_flushes"] += 1
            carry_pulse: int | None = None
            carry_run_idx: int | None = None
            if len(current_pulse) > min_frame_pulses and current_pulse[-1] <= sync_us:
                carry_pulse = int(current_pulse.pop())
                carry_run_idx = int(current_run_indices.pop())
                stats["carry_pulse_flushes"] += 1
            flush_current()
            current_pulse.clear()
            current_run_indices.clear()
            if carry_pulse is not None and carry_run_idx is not None:
                current_pulse.append(carry_pulse)
                current_run_indices.append(carry_run_idx)

        current_pulse.append(int(pulse_us))
        current_run_indices.append(int(run_idx))

    if len(current_pulse) >= min_frame_pulses:
        stats["idle_flushes"] += 1
        flush_current()

    return frames, stats


def _filter_segmented_frames(
    raw_frames: list[dict[str, Any]],
    sample_rate: int,
    max_frames: int,
) -> tuple[list[dict[str, Any]], dict[str, int]]:
    rows: list[dict[str, Any]] = []
    stats = {
        "accepted_frames": 0,
        "rejected_first_low": 0,
        "rejected_clk_range": 0,
    }
    for frame in raw_frames:
        pulse = [int(value) for value in frame["pulse"]]
        if _find_first_low_index(pulse) is None:
            stats["rejected_first_low"] += 1
            continue
        row = _candidate_row(
            run_idx=int(frame["run_index"]),
            pulse=pulse,
            start_sample=int(frame["start_sample"]),
            sample_rate=sample_rate,
        )
        if row is None:
            stats["rejected_clk_range"] += 1
            continue
        rows.append(row)
        stats["accepted_frames"] += 1

    if max_frames > 0:
        rows = rows[: max(1, max_frames)]
    return rows, stats


def _extract_candidate_frames(
    decoder_mod: Any,
    segment: list[int],
    sample_offset: int,
    sample_rate: int,
    max_frames: int,
    smooth_window: int,
    min_run_samples: int,
    min_pulse_us: int,
    max_pulse_us: int,
    sync_us: int,
    min_frame_pulses: int,
    fixed_frame_pulses: int,
    fixed_pulses_tolerance: int,
    require_first_low_longest: bool,
) -> list[dict[str, Any]]:
    runs = _build_runs(decoder_mod, segment, smooth_window, min_run_samples)
    if not runs:
        return []

    _ = (sync_us, min_frame_pulses, require_first_low_longest)
    rows: list[dict[str, Any]] = []
    window_pulses = max(1, int(fixed_frame_pulses))
    if len(runs) < window_pulses:
        return rows

    for run_idx in range(0, len(runs) - window_pulses + 1):
        pulse = [_to_us(int(run.length), sample_rate) for run in runs[run_idx : run_idx + window_pulses]]
        if not _pulse_in_range(pulse, min_pulse_us, max_pulse_us):
            continue
        row = _candidate_row(
            run_idx=run_idx,
            pulse=pulse,
            start_sample=sample_offset + int(runs[run_idx].start),
            sample_rate=sample_rate,
        )
        if row is None:
            continue
        rows.append(row)
        if max_frames > 0 and len(rows) >= max_frames:
            break

    return rows


def extract_frames(
    decoder_mod: Any,
    wav: Path,
    start_sec: float,
    end_sec: float | None,
    max_frames: int,
    smooth_window: int,
    min_run_samples: int,
    min_pulse_us: int,
    max_pulse_us: int,
    sync_us: int,
    min_frame_pulses: int,
    fixed_frame_pulses: int,
    fixed_pulses_tolerance: int,
    require_first_low_longest: bool,
) -> tuple[int, list[dict[str, Any]]]:
    wav_data = decoder_mod.read_wav(wav)
    sample_rate = int(wav_data.sample_rate)
    total_samples = len(wav_data.samples)
    start_idx = max(0, int(start_sec * sample_rate))
    end_idx = total_samples if end_sec is None else min(total_samples, int(end_sec * sample_rate))
    if start_idx >= end_idx:
        return sample_rate, []

    segment = wav_data.samples[start_idx:end_idx]
    frames = _extract_candidate_frames(
        decoder_mod=decoder_mod,
        segment=segment,
        sample_offset=start_idx,
        sample_rate=sample_rate,
        max_frames=max_frames,
        smooth_window=smooth_window,
        min_run_samples=min_run_samples,
        min_pulse_us=min_pulse_us,
        max_pulse_us=max_pulse_us,
        sync_us=sync_us,
        min_frame_pulses=min_frame_pulses,
        fixed_frame_pulses=fixed_frame_pulses,
        fixed_pulses_tolerance=fixed_pulses_tolerance,
        require_first_low_longest=require_first_low_longest,
    )

    for idx, row in enumerate(frames, start=1):
        row["candidate_idx"] = idx
        row["candidate_wav_sec"] = float(row.get("start_sec", 0.0))
    return sample_rate, frames


def write_outputs(out_txt: Path, out_json: Path, wav: Path, sr: int, frames: list[dict[str, Any]]) -> None:
    lines: list[str] = []
    for frame in frames:
        lines.extend(str(int(value)) for value in frame["pulse"])
        lines.append("")
    out_txt.parent.mkdir(parents=True, exist_ok=True)
    out_txt.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")

    payload = {
        "wav": str(wav),
        "sample_rate": sr,
        "frame_count": len(frames),
        "pulse": frames[0]["pulse"] if frames else [],
        "frames": frames,
    }
    out_json.parent.mkdir(parents=True, exist_ok=True)
    out_json.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Extract EV1527 candidate frames from WAV for the fixed RF chain.")
    parser.add_argument("--wav", type=Path, required=True)
    parser.add_argument("--mode", choices=["timeline", "cluster"], default="timeline")
    parser.add_argument("--start-sec", type=float, default=0.0)
    parser.add_argument("--end-sec", type=float, default=None)
    parser.add_argument("--max-frames", type=int, default=64)
    parser.add_argument("--smooth-window", type=int, default=2)
    parser.add_argument("--min-run-samples", type=int, default=0)
    parser.add_argument("--min-pulse-us", type=int, default=80)
    parser.add_argument("--max-pulse-us", type=int, default=65535)
    parser.add_argument("--sync-us", type=int, default=8000)
    parser.add_argument(
        "--min-frame-pulses",
        type=int,
        default=50,
        help="Minimum pulse count required before hardware-equivalent frame flush (default: 50)",
    )
    parser.add_argument(
        "--selector",
        choices=["decode"],
        default="decode",
        help="Compatibility option kept for command-shape stability. Unused in Stage-1 candidate extraction.",
    )
    parser.add_argument(
        "--decoder-min-frame-confidence",
        type=float,
        default=DECODER_MIN_FRAME_CONFIDENCE_DEFAULT,
        help="Compatibility option kept for callers. Unused in Stage-1 candidate extraction.",
    )
    parser.add_argument(
        "--decoder-min-occurrences",
        type=int,
        default=DECODER_MIN_OCCURRENCES_DEFAULT,
        help="Compatibility option kept for callers. Unused in Stage-1 candidate extraction.",
    )
    parser.add_argument(
        "--decoder-min-burst-occurrences",
        type=int,
        default=DECODER_MIN_BURST_OCCURRENCES_DEFAULT,
        help="Compatibility option kept for callers. Unused in Stage-1 candidate extraction.",
    )
    parser.add_argument(
        "--hw-prefilter",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Compatibility flag kept enabled; Stage-1 filtering is always enforced during extraction.",
    )
    parser.add_argument(
        "--fixed-frame-pulses",
        type=int,
        default=50,
        help="Compatibility option kept for callers. Unused in Stage-1 candidate extraction.",
    )
    parser.add_argument(
        "--fixed-pulses-tolerance",
        type=int,
        default=0,
        help="Compatibility option kept for callers. Unused in Stage-1 candidate extraction.",
    )
    parser.add_argument(
        "--require-first-low-longest",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Compatibility flag kept enabled; Stage-1 always enforces first-low-longest filtering.",
    )
    parser.add_argument("--out-txt", type=Path, default=Path("sim_data/pulse.txt"))
    parser.add_argument("--out-json", type=Path, default=Path("sim_data/pulse.json"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    project2_dir = Path(__file__).resolve().parent.parent
    decoder = load_decoder_module(project2_dir)
    _ = (
        args.mode,
        args.min_frame_pulses,
        args.selector,
        args.hw_prefilter,
        args.decoder_min_frame_confidence,
        args.decoder_min_occurrences,
        args.decoder_min_burst_occurrences,
    )

    sample_rate, frames = extract_frames(
        decoder_mod=decoder,
        wav=args.wav,
        start_sec=args.start_sec,
        end_sec=args.end_sec,
        max_frames=args.max_frames,
        smooth_window=max(1, args.smooth_window),
        min_run_samples=max(0, args.min_run_samples),
        min_pulse_us=max(1, args.min_pulse_us),
        max_pulse_us=max(1, args.max_pulse_us),
        sync_us=max(0, args.sync_us),
        min_frame_pulses=max(1, args.min_frame_pulses),
        fixed_frame_pulses=max(1, args.fixed_frame_pulses),
        fixed_pulses_tolerance=max(0, args.fixed_pulses_tolerance),
        require_first_low_longest=bool(args.require_first_low_longest),
    )
    write_outputs(args.out_txt, args.out_json, args.wav, sample_rate, frames)
    print(f"frames={len(frames)} txt={args.out_txt} json={args.out_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
