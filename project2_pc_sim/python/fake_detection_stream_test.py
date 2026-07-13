#!/usr/bin/env python3
"""
Minimal local vision validation for project2_pc_sim.

This script does not run RKNN inference. It reuses the retained bridge payload
contract so the Qt Vision page can consume synthetic detections before the
master-side architecture is rebuilt.
"""

from __future__ import annotations

import argparse
import base64
import json
import math
import socket
import sys
import time
from pathlib import Path

import cv2
import numpy as np

PROJECT_ROOT = Path(__file__).resolve().parent.parent
PYTHON_ROOT = PROJECT_ROOT / "python"
for _path in (PROJECT_ROOT, PYTHON_ROOT):
    text = str(_path)
    if text not in sys.path:
        sys.path.insert(0, text)

import wsl_vision_bridge_server as bridge_server  # noqa: E402
from vision.rknn_pipeline import VisionPipelineState  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a synthetic detection stream that matches the retained WSL vision bridge payload."
    )
    parser.add_argument("--source-image", default="", help="Optional local image used as the background frame.")
    parser.add_argument("--out-dir", default=str(PROJECT_ROOT / "sim_data" / "local_validation" / "vision"))
    parser.add_argument("--frame-count", type=int, default=12, help="Synthetic frame count to generate.")
    parser.add_argument("--fps", type=float, default=4.0, help="Nominal FPS written into the payload.")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--serve", action="store_true", help="Serve the generated JSON payloads over TCP for the Qt Vision page.")
    parser.add_argument("--loop", action="store_true", help="When serving, loop frames until Ctrl+C.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=17656)
    return parser.parse_args()


def _load_base_frame(args: argparse.Namespace) -> np.ndarray:
    if args.source_image:
        image_path = Path(args.source_image)
        frame = cv2.imread(str(image_path), cv2.IMREAD_COLOR)
        if frame is None:
            raise FileNotFoundError(f"failed to read source image: {image_path}")
        return frame
    return _make_synthetic_background(args.width, args.height)


def _make_synthetic_background(width: int, height: int) -> np.ndarray:
    width = max(320, int(width))
    height = max(240, int(height))
    x = np.linspace(0.0, 1.0, width, dtype=np.float32)
    y = np.linspace(0.0, 1.0, height, dtype=np.float32)[:, None]

    frame = np.zeros((height, width, 3), dtype=np.uint8)
    frame[..., 0] = np.clip(18.0 + 60.0 * x[None, :], 0.0, 255.0).astype(np.uint8)
    frame[..., 1] = np.clip(26.0 + 90.0 * y, 0.0, 255.0).astype(np.uint8)
    frame[..., 2] = np.clip(30.0 + 40.0 * (1.0 - x[None, :]) + 25.0 * y, 0.0, 255.0).astype(np.uint8)

    cv2.rectangle(frame, (40, height - 180), (width - 40, height - 40), (28, 40, 52), -1)
    cv2.circle(frame, (width // 5, height // 4), 68, (0, 166, 255), -1)
    cv2.circle(frame, (width - width // 5, height // 3), 48, (255, 214, 10), -1)
    cv2.putText(
        frame,
        "project2_pc_sim synthetic bridge frame",
        (40, 52),
        cv2.FONT_HERSHEY_SIMPLEX,
        1.1,
        (240, 248, 255),
        2,
        cv2.LINE_AA,
    )
    return frame


def _make_frame_variant(base_frame: np.ndarray, frame_index: int, total_frames: int) -> np.ndarray:
    frame = base_frame.copy()
    total = max(1, total_frames)
    ratio = float(frame_index % total) / float(total)
    h, w = frame.shape[:2]

    bar_x = int(60 + ratio * max(1, w - 220))
    cv2.rectangle(frame, (bar_x, h - 160), (min(w - 60, bar_x + 120), h - 110), (0, 120, 255), -1)

    label = f"frame={frame_index:03d} phase={ratio:.2f}"
    cv2.putText(frame, label, (40, h - 56), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (245, 245, 245), 2, cv2.LINE_AA)
    return frame


def _make_fake_detections(frame_shape: tuple[int, int, int], frame_index: int, total_frames: int):
    h, w = frame_shape[:2]
    total = max(1, total_frames)
    phase = (2.0 * math.pi * float(frame_index % total)) / float(total)

    person_x = int(80 + ((math.sin(phase) + 1.0) * 0.5) * max(1, w - 320))
    person_y = int(90 + ((math.cos(phase * 0.7) + 1.0) * 0.5) * max(1, h - 420))
    car_x = int(160 + ((math.cos(phase * 0.8) + 1.0) * 0.5) * max(1, w - 500))
    car_y = int(h * 0.58 + math.sin(phase * 1.3) * 40.0)

    boxes = np.asarray(
        [
            [person_x, person_y, min(w - 40, person_x + 120), min(h - 40, person_y + 260)],
            [car_x, car_y, min(w - 40, car_x + 260), min(h - 40, car_y + 120)],
        ],
        dtype=np.float32,
    )
    classes = np.asarray([0, 2], dtype=np.int32)
    scores = np.asarray(
        [
            0.94 - 0.04 * ((frame_index % 3) / 2.0),
            0.86 - 0.03 * ((frame_index + 1) % 4) / 3.0,
        ],
        dtype=np.float32,
    )
    return boxes, classes, scores


def _build_payloads(args: argparse.Namespace) -> list[dict]:
    frame_count = max(1, int(args.frame_count))
    fps = max(0.1, float(args.fps))
    base_frame = _load_base_frame(args)
    state = VisionPipelineState()
    state.set_model_status(True)
    state.set_camera_status(True)

    payloads = []
    for frame_index in range(frame_count):
        frame = _make_frame_variant(base_frame, frame_index, frame_count)
        boxes, classes, scores = _make_fake_detections(frame.shape, frame_index, frame_count)
        state.update_detections(frame, boxes, classes, scores, fps)
        payload = bridge_server._build_payload(state.get_snapshot(include_frame=True))
        payload["simulation"] = {
            "mode": "fake_detection_stream_test",
            "frame_index": frame_index,
            "frame_count": frame_count,
            "uses_source_image": bool(args.source_image),
        }
        payloads.append(payload)
    return payloads


def _write_outputs(payloads: list[dict], out_dir: Path, args: argparse.Namespace) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)

    ndjson_path = out_dir / "bridge_stream.ndjson"
    with ndjson_path.open("w", encoding="utf-8", newline="\n") as handle:
        for payload in payloads:
            handle.write(json.dumps(payload, ensure_ascii=False, separators=(",", ":")))
            handle.write("\n")

    manifest = {
        "mode": "fake_detection_stream_test",
        "frame_count": len(payloads),
        "fps": max(0.1, float(args.fps)),
        "source_image": str(Path(args.source_image).resolve()) if args.source_image else "",
        "bridge_host": bridge_server._effective_bind_host(args.host),
        "bridge_port": int(args.port),
        "serve_enabled": bool(args.serve),
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    for index, payload in enumerate(payloads):
        encoded = str(payload.get("frame_jpeg_b64", "") or "")
        if not encoded:
            continue
        frame_path = out_dir / f"annotated_frame_{index:03d}.jpg"
        frame_path.write_bytes(base64.b64decode(encoded.encode("ascii")))


def _send_payloads(conn: socket.socket, payloads: list[dict], fps: float, loop: bool) -> None:
    interval = 1.0 / max(0.1, fps)
    while True:
        for payload in payloads:
            blob = (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
            conn.sendall(blob)
            time.sleep(interval)
        if not loop:
            return


def _serve_payloads(payloads: list[dict], host: str, port: int, fps: float, loop: bool) -> int:
    bind_host = bridge_server._effective_bind_host(host)
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((bind_host, port))
    server.listen(4)
    print(f"[fake_detection_stream_test] serving {len(payloads)} payload(s) on {bind_host}:{port}", flush=True)

    try:
        while True:
            conn, addr = server.accept()
            print(f"[fake_detection_stream_test] client connected: {addr[0]}:{addr[1]}", flush=True)
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            try:
                _send_payloads(conn, payloads, fps, loop)
            except (BrokenPipeError, ConnectionError, OSError) as exc:
                print(f"[fake_detection_stream_test] client disconnected: {exc}", flush=True)
            finally:
                try:
                    conn.close()
                except OSError:
                    pass
            if not loop:
                return 0
    except KeyboardInterrupt:
        print("[fake_detection_stream_test] interrupted", flush=True)
        return 0
    finally:
        server.close()


def main() -> int:
    args = parse_args()
    payloads = _build_payloads(args)
    out_dir = Path(args.out_dir)
    _write_outputs(payloads, out_dir, args)
    print(f"[fake_detection_stream_test] wrote outputs to {out_dir}", flush=True)

    if args.serve:
        return _serve_payloads(payloads, args.host, int(args.port), float(args.fps), bool(args.loop))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
