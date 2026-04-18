"""
Lightweight timing profiler for VisionPipeline and bridge packaging.

The profiler is inactive unless an output path is provided.
"""

from __future__ import annotations

import copy
import json
import logging
import math
import os
import platform
import socket
import sys
import threading
import time
from pathlib import Path
from typing import Any

_LOG = logging.getLogger(__name__)


def _is_wsl() -> bool:
    release = platform.release().lower()
    if "microsoft" in release or "wsl" in release:
        return True
    proc_version = Path("/proc/version")
    if proc_version.exists():
        try:
            text = proc_version.read_text(encoding="utf-8", errors="ignore").lower()
        except OSError:
            return False
        return "microsoft" in text or "wsl" in text
    return False


def _percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(float(v) for v in values)
    if len(ordered) == 1:
        return ordered[0]
    pos = (len(ordered) - 1) * fraction
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return ordered[lo]
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (pos - lo)


def _series_summary(values: list[float]) -> dict[str, Any]:
    numeric = [float(v) for v in values]
    if not numeric:
        return {
            "count": 0,
            "min": None,
            "max": None,
            "avg": None,
            "p50": None,
            "p95": None,
        }
    return {
        "count": len(numeric),
        "min": min(numeric),
        "max": max(numeric),
        "avg": sum(numeric) / len(numeric),
        "p50": _percentile(numeric, 0.50),
        "p95": _percentile(numeric, 0.95),
    }


def _throughput_fps(done_ts: list[float]) -> float:
    stamps = [float(v) for v in done_ts]
    if len(stamps) < 2:
        return 0.0
    elapsed = stamps[-1] - stamps[0]
    if elapsed <= 1e-9:
        return 0.0
    return (len(stamps) - 1) / elapsed


def _frame_store() -> dict[str, Any]:
    return {"frames": [], "counters": {}}


class VisionTimingProfiler:
    def __init__(self, output_path: str | None = None):
        self.output_path = str(output_path or "").strip()
        self.enabled = bool(self.output_path)
        self._lock = threading.Lock()
        self._data = {
            "runtime": {
                "start_wall_time": time.strftime("%Y-%m-%d %H:%M:%S %z", time.localtime()),
                "start_unix_time": time.time(),
                "pid": os.getpid(),
                "python_executable": sys.executable,
                "python_version": sys.version,
                "platform": platform.platform(),
                "system": platform.system(),
                "release": platform.release(),
                "machine": platform.machine(),
                "hostname": socket.gethostname(),
                "cwd": os.getcwd(),
                "is_wsl": _is_wsl(),
            },
            "notes": [],
            "stages": {
                "capture": _frame_store(),
                "inference": _frame_store(),
                "postprocess": _frame_store(),
                "bridge": _frame_store(),
            },
            "yolov5_postprocess": {
                "call_count": 0,
                "module_path": "",
                "first_call": None,
            },
        }

    def set_runtime_field(self, key: str, value: Any) -> None:
        if not self.enabled:
            return
        with self._lock:
            self._data["runtime"][str(key)] = value

    def add_note(self, text: str) -> None:
        if not self.enabled:
            return
        with self._lock:
            self._data["notes"].append(str(text))

    def increment_counter(self, stage: str, counter: str, amount: int = 1) -> None:
        if not self.enabled:
            return
        with self._lock:
            counters = self._data["stages"][stage]["counters"]
            counters[counter] = int(counters.get(counter, 0)) + int(amount)

    def record_stage_frame(self, stage: str, frame_id: int, duration_ms: float, **extra: Any) -> None:
        if not self.enabled:
            return
        entry = {
            "frame_id": int(frame_id),
            "duration_ms": float(duration_ms),
            "done_ts": time.perf_counter(),
        }
        for key, value in extra.items():
            entry[str(key)] = value
        with self._lock:
            self._data["stages"][stage]["frames"].append(entry)

    def record_bridge_frame(
        self,
        frame_id: int,
        frame_count: int,
        draw_ms: float,
        jpeg_encode_ms: float,
        base64_ms: float,
        send_ms: float,
        payload_bytes: int,
        detection_count: int,
        client_count: int,
        snapshot_fps: float,
        overlay_width: int,
        overlay_height: int,
    ) -> None:
        if not self.enabled:
            return
        self.record_stage_frame(
            "bridge",
            frame_id=frame_id,
            duration_ms=float(draw_ms + jpeg_encode_ms + base64_ms + send_ms),
            frame_count=int(frame_count),
            draw_ms=float(draw_ms),
            jpeg_encode_ms=float(jpeg_encode_ms),
            base64_ms=float(base64_ms),
            send_ms=float(send_ms),
            payload_bytes=int(payload_bytes),
            detection_count=int(detection_count),
            client_count=int(client_count),
            snapshot_fps=float(snapshot_fps),
            overlay_width=int(overlay_width),
            overlay_height=int(overlay_height),
        )

    def mark_postprocess_call(self, module_path: str) -> None:
        if not self.enabled:
            return
        with self._lock:
            payload = self._data["yolov5_postprocess"]
            payload["call_count"] = int(payload.get("call_count", 0)) + 1
            if payload["first_call"] is None:
                payload["module_path"] = str(module_path)
                payload["first_call"] = {
                    "unix_time": time.time(),
                    "thread_name": threading.current_thread().name,
                    "pid": os.getpid(),
                    "python_executable": sys.executable,
                    "platform": platform.platform(),
                    "hostname": socket.gethostname(),
                    "cwd": os.getcwd(),
                    "is_wsl": _is_wsl(),
                }
                _LOG.info(
                    "VISION_YOLOV5_POSTPROCESS_EXEC pid=%s thread=%s is_wsl=%s module=%s",
                    payload["first_call"]["pid"],
                    payload["first_call"]["thread_name"],
                    payload["first_call"]["is_wsl"],
                    payload["module_path"],
                )

    def _summary_for_stage(self, stage_name: str) -> dict[str, Any]:
        frames = self._data["stages"][stage_name]["frames"]
        done_ts = [entry["done_ts"] for entry in frames]
        duration_ms = [entry["duration_ms"] for entry in frames]
        return {
            "count": len(frames),
            "throughput_fps": _throughput_fps(done_ts),
            "duration_ms": _series_summary(duration_ms),
            "counters": copy.deepcopy(self._data["stages"][stage_name]["counters"]),
        }

    def _summary_for_bridge(self) -> dict[str, Any]:
        frames = self._data["stages"]["bridge"]["frames"]
        done_ts = [entry["done_ts"] for entry in frames]
        return {
            "count": len(frames),
            "throughput_fps": _throughput_fps(done_ts),
            "duration_ms": _series_summary([entry["duration_ms"] for entry in frames]),
            "draw_ms": _series_summary([entry["draw_ms"] for entry in frames]),
            "jpeg_encode_ms": _series_summary([entry["jpeg_encode_ms"] for entry in frames]),
            "base64_ms": _series_summary([entry["base64_ms"] for entry in frames]),
            "send_ms": _series_summary([entry["send_ms"] for entry in frames]),
            "payload_bytes": _series_summary([float(entry["payload_bytes"]) for entry in frames]),
            "detection_count": _series_summary([float(entry["detection_count"]) for entry in frames]),
            "counters": copy.deepcopy(self._data["stages"]["bridge"]["counters"]),
        }

    def _build_summary(self) -> dict[str, Any]:
        return {
            "capture": self._summary_for_stage("capture"),
            "inference": self._summary_for_stage("inference"),
            "postprocess": self._summary_for_stage("postprocess"),
            "bridge": self._summary_for_bridge(),
            "yolov5_postprocess": copy.deepcopy(self._data["yolov5_postprocess"]),
        }

    def write_json(self) -> str | None:
        if not self.enabled:
            return None

        with self._lock:
            self._data["runtime"]["end_unix_time"] = time.time()
            payload = copy.deepcopy(self._data)

        payload["summary"] = self._build_summary()
        output = Path(self.output_path)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")

        if int(payload["yolov5_postprocess"].get("call_count", 0)) == 0:
            _LOG.info(
                "VISION_YOLOV5_POSTPROCESS_EXEC call_count=0 is_wsl=%s module=%s",
                payload["runtime"].get("is_wsl"),
                payload["yolov5_postprocess"].get("module_path") or "not-invoked",
            )
        _LOG.info("VISION_PROFILE_WRITTEN path=%s", output)
        return str(output)
