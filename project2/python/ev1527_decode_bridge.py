#!/usr/bin/env python3
"""
Decode EV1527 pulses from JSON/TXT and return compact JSON for C gateway.

Input examples:
1) {"pulse":[1200,37200,1200,3600,...]}
2) {"frames":[{"pulse":[...]}]}
3) txt file with one pulse(us) per line
"""

from __future__ import annotations

import json
import statistics
import sys
from pathlib import Path


def load_frames(path: Path) -> list[list[int]]:
    if path.suffix.lower() == ".json":
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            if "pulse" in data and isinstance(data["pulse"], list):
                return [[int(x) for x in data["pulse"] if int(x) > 0]]
            if "frames" in data and data["frames"]:
                frames: list[list[int]] = []
                for row in data["frames"]:
                    pulse = [int(x) for x in row.get("pulse", []) if int(x) > 0]
                    if pulse:
                        frames.append(pulse)
                if frames:
                    return frames
        raise ValueError("JSON has no pulse list.")

    out: list[int] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        out.append(int(line))
    if not out:
        raise ValueError("TXT has no pulses.")
    return [out]


def _rel_err(obs: float, exp: float) -> float:
    if exp <= 1e-6:
        return 1e9
    return abs(obs - exp) / exp


def decode_ev1527(pulses: list[int]) -> dict | None:
    bits_n = 24
    frame_n = 2 + 2 * bits_n
    if len(pulses) < frame_n:
        return None

    best: dict | None = None
    for st in range(0, len(pulses) - frame_n + 1):
        sync_h = float(pulses[st])
        sync_l = float(pulses[st + 1])
        if sync_l < 8000 or sync_h < 100:
            continue

        totals = [pulses[st + 2 + 2 * i] + pulses[st + 2 + 2 * i + 1] for i in range(bits_n)]
        t = float(statistics.median(totals)) / 16.0
        if not (120.0 <= t <= 1200.0):
            continue

        sync_err = _rel_err(sync_h, 4.0 * t) + _rel_err(sync_l, 124.0 * t)
        if sync_err > 2.2:
            continue

        bits: list[str] = []
        bit_err_sum = 0.0
        for i in range(bits_n):
            hi = float(pulses[st + 2 + 2 * i])
            lo = float(pulses[st + 2 + 2 * i + 1])
            err0 = _rel_err(hi, 4.0 * t) + _rel_err(lo, 12.0 * t)
            err1 = _rel_err(hi, 12.0 * t) + _rel_err(lo, 4.0 * t)
            if err1 < err0:
                bits.append("1")
                bit_err_sum += err1
            else:
                bits.append("0")
                bit_err_sum += err0

        code = int("".join(bits), 2)
        conf = max(0.0, 1.0 - min(1.0, (bit_err_sum / bits_n + sync_err * 0.5) / 2.5))
        row = {
            "addr": f"0x{code:06X}",
            "key": str(code & 0xF),
            "confidence": round(conf, 4),
            "clk_us": round(t, 2),
        }
        if best is None or row["confidence"] > best["confidence"]:
            best = row
    return best


def main() -> int:
    if len(sys.argv) != 2:
        print('{"error":"usage: ev1527_decode_bridge.py <pulse.json|pulse.txt>"}')
        return 1
    path = Path(sys.argv[1])
    if not path.exists():
        print('{"error":"input_not_found"}')
        return 2
    try:
        frames = load_frames(path)
        result = None
        for pulse in frames:
            row = decode_ev1527(pulse)
            if row is None:
                continue
            if result is None or row["confidence"] > result["confidence"]:
                result = row
    except Exception as exc:  # pragma: no cover
        print(json.dumps({"error": str(exc)}, ensure_ascii=False))
        return 3

    if result is None:
        print('{"error":"decode_failed"}')
        return 4
    print(json.dumps(result, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
