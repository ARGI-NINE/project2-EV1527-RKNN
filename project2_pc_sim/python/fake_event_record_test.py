#!/usr/bin/env python3
"""
Minimal local event-trigger validation for project2_pc_sim.

This script does not call project2_master or start a real recorder. It validates
the retained RF JSON contract and writes placeholder event artifacts so the
future master-side integration has a concrete handoff shape to target.
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

PROJECT_ROOT = Path(__file__).resolve().parent.parent


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate a mock event.json / record_done pair for local trigger validation.")
    parser.add_argument("--trigger", choices=("rf_confirmed", "manual_record"), default="rf_confirmed")
    parser.add_argument("--out-dir", default=str(PROJECT_ROOT / "sim_data" / "local_validation" / "event"))
    parser.add_argument("--rf-json", default="", help="Optional raw rf_gateway JSON line to validate and embed.")
    parser.add_argument("--addr", default="0x35A1BC")
    parser.add_argument("--key", default="12")
    parser.add_argument("--conf", type=float, default=0.94)
    parser.add_argument("--src", default="replay")
    parser.add_argument("--seq", type=int, default=7)
    parser.add_argument("--wav-sec", type=float, default=1.234)
    parser.add_argument("--pulses", type=int, default=50)
    parser.add_argument("--manual-note", default="local manual record validation")
    return parser.parse_args()


def _iso_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _event_id() -> str:
    return datetime.now(timezone.utc).strftime("evt-%Y%m%dT%H%M%SZ")


def _default_rf_line(args: argparse.Namespace) -> str:
    payload = {
        "addr": str(args.addr),
        "key": str(args.key),
        "conf": float(args.conf),
        "src": str(args.src),
        "pulses": int(args.pulses),
        "seq": int(args.seq),
        "wav_sec": float(args.wav_sec),
    }
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


def _parse_rf_json_line(text: str) -> dict[str, Any]:
    try:
        obj = json.loads(text)
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid rf_gateway JSON: {exc}") from exc

    if not isinstance(obj, dict):
        raise ValueError("rf_gateway JSON must be an object")

    addr = obj.get("addr")
    key = obj.get("key")
    conf = obj.get("conf")
    src = obj.get("src")
    if not isinstance(addr, str) or not addr.strip():
        raise ValueError("rf_gateway JSON missing non-empty string field: addr")
    if not isinstance(key, (str, int, float)):
        raise ValueError("rf_gateway JSON missing scalar field: key")
    if not isinstance(conf, (int, float)):
        raise ValueError("rf_gateway JSON missing numeric field: conf")
    if not isinstance(src, str) or not src.strip():
        raise ValueError("rf_gateway JSON missing non-empty string field: src")

    parsed = {
        "addr": addr.strip(),
        "key": str(key).strip(),
        "conf": float(conf),
        "src": src.strip(),
    }
    if "seq" in obj and isinstance(obj["seq"], (int, float)):
        parsed["seq"] = int(obj["seq"])
    if "wav_sec" in obj and isinstance(obj["wav_sec"], (int, float)):
        parsed["wav_sec"] = float(obj["wav_sec"])
    if "pulses" in obj and isinstance(obj["pulses"], (int, float)):
        parsed["pulses"] = int(obj["pulses"])
    return parsed


def _build_event_payload(args: argparse.Namespace, event_id: str, created_at: str) -> tuple[dict[str, Any], str]:
    record_file = "mock_record.txt"

    payload: dict[str, Any] = {
        "schema": "project2_pc_sim/local_event_validation/v1",
        "event_id": event_id,
        "created_at": created_at,
        "trigger": args.trigger,
        "record_request": {
            "mode": "mock_local_validation",
            "record_file": record_file,
        },
    }

    if args.trigger == "rf_confirmed":
        rf_line = args.rf_json.strip() or _default_rf_line(args)
        payload["rf_confirmed"] = _parse_rf_json_line(rf_line)
        payload["notes"] = [
            "Validated against the retained rf_gateway JSON field contract.",
            "This artifact does not imply a real recorder or MQTT publish exists in pc_sim.",
        ]
        return payload, rf_line

    payload["manual_record"] = {
        "source": "local_cli",
        "note": str(args.manual_note),
    }
    payload["notes"] = [
        "Manual trigger only verifies the event handoff shape.",
        "No real recording subprocess is started in pc_sim.",
    ]
    return payload, ""


def _build_record_done_payload(event_payload: dict[str, Any], completed_at: str) -> dict[str, Any]:
    return {
        "schema": "project2_pc_sim/local_event_validation/v1",
        "event_id": event_payload["event_id"],
        "completed_at": completed_at,
        "status": "record_done",
        "trigger": event_payload["trigger"],
        "record_file": event_payload["record_request"]["record_file"],
    }


def _write_artifacts(out_dir: Path, event_payload: dict[str, Any], record_done: dict[str, Any]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)

    (out_dir / "event.json").write_text(json.dumps(event_payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (out_dir / "record_done.json").write_text(json.dumps(record_done, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (out_dir / "mock_record.txt").write_text(
        "project2_pc_sim local validation placeholder.\nNo real recorder output is generated here.\n",
        encoding="utf-8",
    )


def main() -> int:
    args = parse_args()
    event_id = _event_id()
    created_at = _iso_now()

    try:
        event_payload, rf_line = _build_event_payload(args, event_id, created_at)
    except ValueError as exc:
        print(f"[fake_event_record_test] {exc}", file=sys.stderr)
        return 2

    completed_at = _iso_now()
    record_done = _build_record_done_payload(event_payload, completed_at)
    out_dir = Path(args.out_dir)
    _write_artifacts(out_dir, event_payload, record_done)

    print(f"[fake_event_record_test] wrote artifacts to {out_dir}", flush=True)
    if rf_line:
        print(f"[fake_event_record_test] rf_gateway line: {rf_line}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
