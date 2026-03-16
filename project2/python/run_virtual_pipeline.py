#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Run virtual RF pipeline: WAV -> pulse -> decode.")
    p.add_argument("--python-bin", type=str, default="python")
    p.add_argument("--wav", type=Path, required=True)
    p.add_argument("--start-sec", type=float, default=0.0)
    p.add_argument("--out-dir", type=Path, default=Path("project2/sim_data"))
    return p.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parent
    wav_to_pulses = root / "wav_to_pulses.py"
    bridge = root / "ev1527_decode_bridge.py"
    out_txt = args.out_dir / "pulse.txt"
    out_json = args.out_dir / "pulse.json"

    args.out_dir.mkdir(parents=True, exist_ok=True)

    subprocess.check_call(
        [
            args.python_bin,
            str(wav_to_pulses),
            "--wav",
            str(args.wav),
            "--start-sec",
            str(args.start_sec),
            "--out-txt",
            str(out_txt),
            "--out-json",
            str(out_json),
        ]
    )

    output = subprocess.check_output([args.python_bin, str(bridge), str(out_json)], text=True).strip()
    try:
        obj = json.loads(output)
    except json.JSONDecodeError:
        print(output)
        return 1

    print(json.dumps(obj, ensure_ascii=False, indent=2))
    print(f"Simulator input file: {out_txt}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

