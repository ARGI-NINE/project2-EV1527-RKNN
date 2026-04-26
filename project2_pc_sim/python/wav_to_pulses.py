#!/usr/bin/env python3
"""
Stage-1 RF preprocessing: WAV -> candidate-frame JSON.

This stage is limited to MCU-like candidate filtering only:
- full-WAV flip preprocessing
- fixed-width flip-window candidate generation
- first low pulse is the longest low pulse in the frame
- EV1527 base-clock range guard derived from the frame sync-low width

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

    return {
        "candidate_wav_sec": float(start_sample) / float(sample_rate),
        "pulse": pulse,
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
) -> list[dict[str, Any]]:
    runs = _build_runs(decoder_mod, segment, smooth_window, min_run_samples)
    if not runs:
        return []

    rows: list[dict[str, Any]] = []
    window_pulses = EV1527_FRAME_PULSES
    if len(runs) < window_pulses:
        return rows

    for run_idx in range(0, len(runs) - window_pulses + 1):
        pulse = [_to_us(int(run.length), sample_rate) for run in runs[run_idx : run_idx + window_pulses]]
        if not _pulse_in_range(pulse, min_pulse_us, max_pulse_us):
            continue
        row = _candidate_row(
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
) -> list[dict[str, Any]]:
    wav_data = decoder_mod.read_wav(wav)
    sample_rate = int(wav_data.sample_rate)
    total_samples = len(wav_data.samples)
    start_idx = max(0, int(start_sec * sample_rate))
    end_idx = total_samples if end_sec is None else min(total_samples, int(end_sec * sample_rate))
    if start_idx >= end_idx:
        return []

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
    )

    for idx, row in enumerate(frames, start=1):
        row["candidate_idx"] = idx
    return frames


def write_outputs(out_txt: Path, out_json: Path, frames: list[dict[str, Any]]) -> None:
    lines: list[str] = []
    for frame in frames:
        lines.extend(str(int(value)) for value in frame["pulse"])
        lines.append("")
    out_txt.parent.mkdir(parents=True, exist_ok=True)
    out_txt.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")

    payload = {"frames": frames}
    out_json.parent.mkdir(parents=True, exist_ok=True)
    out_json.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Extract EV1527 candidate frames from WAV for the fixed RF chain.")
    parser.add_argument("--wav", type=Path, required=True)
    parser.add_argument("--start-sec", type=float, default=0.0)
    parser.add_argument("--end-sec", type=float, default=None)
    parser.add_argument("--max-frames", type=int, default=64)
    parser.add_argument("--smooth-window", type=int, default=2)
    parser.add_argument("--min-run-samples", type=int, default=0)
    parser.add_argument("--min-pulse-us", type=int, default=80)
    parser.add_argument("--max-pulse-us", type=int, default=65535)
    parser.add_argument("--out-txt", type=Path, default=Path("sim_data/pulse.txt"))
    parser.add_argument("--out-json", type=Path, default=Path("sim_data/pulse.json"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    project2_dir = Path(__file__).resolve().parent.parent
    decoder = load_decoder_module(project2_dir)

    frames = extract_frames(
        decoder_mod=decoder,
        wav=args.wav,
        start_sec=args.start_sec,
        end_sec=args.end_sec,
        max_frames=args.max_frames,
        smooth_window=max(1, args.smooth_window),
        min_run_samples=max(0, args.min_run_samples),
        min_pulse_us=max(1, args.min_pulse_us),
        max_pulse_us=max(1, args.max_pulse_us),
    )
    write_outputs(args.out_txt, args.out_json, frames)
    print(f"frames={len(frames)} txt={args.out_txt} json={args.out_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
