#!/usr/bin/env python3
"""
Extract EV1527-like pulse frames from WAV for hardware-free simulation.

Output:
- pulse.txt: one pulse width(us) per line, blank line between frames
- pulse.json: frames + first frame shortcut key `pulse`
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path
from typing import Any


def load_decoder_module(project2_dir: Path) -> Any:
    mod_path = project2_dir / "ev1527_decode.py"
    spec = importlib.util.spec_from_file_location("ev1527_decode_mod", mod_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load {mod_path}")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


def extract_frames(
    decoder_mod: Any,
    wav: Path,
    start_sec: float,
    end_sec: float | None,
    min_frame_conf: float,
    min_occurrences: int,
    min_burst_occurrences: int,
    max_frames: int,
) -> tuple[int, list[dict[str, Any]]]:
    def synth_pulse(bits: str, clk_us: float) -> list[int]:
        t = max(100.0, float(clk_us))
        pulse = [int(round(4.0 * t)), int(round(124.0 * t))]
        for b in bits:
            if b == "1":
                pulse.extend([int(round(12.0 * t)), int(round(4.0 * t))])
            else:
                pulse.extend([int(round(4.0 * t)), int(round(12.0 * t))])
        return pulse

    wav_data = decoder_mod.read_wav(wav)
    sr = wav_data.sample_rate
    all_frames, clusters, _start_idx, _end_idx, _stage_stats = decoder_mod.decode_capture(
        wav=wav_data,
        start_sec=start_sec,
        end_sec=end_sec,
        min_frame_conf=min_frame_conf,
        verbose=False,
    )
    selected_rows, _dominant, _warns = decoder_mod.select_cluster_rows(
        all_frames=all_frames,
        clusters=clusters,
        sample_rate=sr,
        min_occurrences=min_occurrences,
        min_burst_occurrences=min_burst_occurrences,
    )

    frames: list[dict[str, Any]] = []
    for row in selected_rows[: max(1, max_frames)]:
        cl = row[0]
        pulse = synth_pulse(cl.center_bits, cl.mean_clk_us)
        frames.append(
            {
                "start_sec": cl.first_sample / sr,
                "pulse": pulse,
                "bits": cl.center_bits,
                "repeat_count": row[2],
                "avg_confidence": round(cl.avg_confidence, 4),
            }
        )
    return sr, frames


def write_outputs(out_txt: Path, out_json: Path, wav: Path, sr: int, frames: list[dict[str, Any]]) -> None:
    lines: list[str] = []
    for fr in frames:
        lines.extend(str(int(v)) for v in fr["pulse"])
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
    p = argparse.ArgumentParser(description="Extract EV1527 pulses from WAV for simulator input.")
    p.add_argument("--wav", type=Path, required=True)
    p.add_argument("--start-sec", type=float, default=0.0)
    p.add_argument("--end-sec", type=float, default=None)
    p.add_argument("--min-frame-confidence", type=float, default=0.2)
    p.add_argument("--min-occurrences", type=int, default=1)
    p.add_argument("--min-burst-occurrences", type=int, default=1)
    p.add_argument("--max-frames", type=int, default=64)
    p.add_argument("--out-txt", type=Path, default=Path("project2/sim_data/pulse.txt"))
    p.add_argument("--out-json", type=Path, default=Path("project2/sim_data/pulse.json"))
    return p.parse_args()


def main() -> int:
    args = parse_args()
    project2_dir = Path(__file__).resolve().parent.parent
    decoder = load_decoder_module(project2_dir)

    sr, frames = extract_frames(
        decoder_mod=decoder,
        wav=args.wav,
        start_sec=args.start_sec,
        end_sec=args.end_sec,
        min_frame_conf=max(0.0, args.min_frame_confidence),
        min_occurrences=max(1, args.min_occurrences),
        min_burst_occurrences=max(1, args.min_burst_occurrences),
        max_frames=max(1, args.max_frames),
    )
    write_outputs(args.out_txt, args.out_json, args.wav, sr, frames)
    print(f"frames={len(frames)} txt={args.out_txt} json={args.out_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
