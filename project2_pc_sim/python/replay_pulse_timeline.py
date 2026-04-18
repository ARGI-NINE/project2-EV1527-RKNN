#!/usr/bin/env python3
"""
Replay pulse frames from pulse.json as RF protocol binary stream in realtime.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path


def _crc8(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
    return crc


def _encode_frame(pulse: list[int]) -> bytes:
    if not pulse:
        return b""
    norm = [int(v) for v in pulse if 0 < int(v) <= 65535]
    if not norm:
        return b""

    header = bytearray([0xAA, 0x55, len(norm) & 0xFF, (len(norm) >> 8) & 0xFF])
    payload = bytearray()
    for p in norm:
        payload.append(p & 0xFF)
        payload.append((p >> 8) & 0xFF)

    crc = _crc8(bytes(header[2:]) + bytes(payload))
    return bytes(header + payload + bytes([crc]))


def _load_frames(path: Path) -> list[dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    frames = data.get("frames", [])
    if not isinstance(frames, list):
        return []

    valid: list[dict] = []
    for row in frames:
        if not isinstance(row, dict):
            continue
        pulse = row.get("pulse", [])
        if not isinstance(pulse, list) or not pulse:
            continue
        start_sec = float(row.get("candidate_wav_sec", row.get("start_sec", 0.0)))
        valid.append(
            {
                "idx": int(row.get("candidate_idx", len(valid) + 1)),
                "start_sec": max(0.0, start_sec),
                "pulse": pulse,
            }
        )

    valid.sort(key=lambda x: x["start_sec"])
    return valid


def _prepare_binary_stdout() -> None:
    if os.name == "nt":
        import msvcrt

        msvcrt.setmode(sys.stdout.fileno(), os.O_BINARY)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Replay pulse timeline to stdout as RF binary packets.")
    p.add_argument("--pulse-json", type=Path, required=True)
    p.add_argument("--speed", type=float, default=1.0)
    p.add_argument("--loop", action="store_true")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    if not args.pulse_json.exists():
        print(f"pulse json not found: {args.pulse_json}", file=sys.stderr)
        return 2

    speed = args.speed if args.speed > 0 else 1.0
    frames = _load_frames(args.pulse_json)
    if not frames:
        print("no valid pulse frames in pulse json", file=sys.stderr)
        return 3

    base = frames[0]["start_sec"]
    timeline = []
    for idx, row in enumerate(frames, start=1):
        rel_t = max(0.0, row["start_sec"] - base)
        pkt = _encode_frame(row["pulse"])
        if pkt:
            timeline.append(
                {
                    "idx": int(row.get("idx", idx)),
                    "wav_sec": float(row["start_sec"]),
                    "rel_sec": rel_t,
                    "packet": pkt,
                }
            )

    if not timeline:
        print("no encodable pulse frames", file=sys.stderr)
        return 4

    _prepare_binary_stdout()
    out = sys.stdout.buffer

    while True:
        prev_t = 0.0
        for item in timeline:
            t = float(item["rel_sec"])
            packet = item["packet"]
            delay = (t - prev_t) / speed
            if delay > 0:
                time.sleep(delay)

            # Keep the legacy idx-only marker and add a timing side marker for UI/backend timing.
            sys.stderr.write(f"FRAME_TS idx={int(item['idx'])}\n")
            sys.stderr.write(
                f"FRAME_META idx={int(item['idx'])} wav_sec={float(item['wav_sec']):.6f}\n"
            )
            sys.stderr.flush()

            out.write(packet)
            out.flush()
            prev_t = t

        if not args.loop:
            break

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
