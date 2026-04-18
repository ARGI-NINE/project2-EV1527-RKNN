#!/usr/bin/env python3
"""
WSL vision bridge server for the PC simulator.

Required logic only:
- resolve model/source paths
- run VisionPipeline
- send newline-delimited JSON snapshots to Qt
"""

from __future__ import annotations

import argparse
import base64
import json
import logging
import signal
import socket
import sys
import time
from pathlib import Path
from typing import Any

import cv2

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from vision.rknn_pipeline import VisionPipeline  # noqa: E402
from vision.timing_profiler import VisionTimingProfiler  # noqa: E402
from vision.yolov5_postprocess import CLASSES, IMG_SIZE  # noqa: E402

_LOG = logging.getLogger("wsl_vision_bridge")
_RUNNING = True
_HEARTBEAT_SEC = 1.0
_TX_MAX_WIDTH = 1280
_TX_JPEG_QUALITY = 85
_LOOP_SLEEP_SEC = 0.010


def _windows_path_to_wsl(raw: str) -> str:
    text = (raw or "").strip()
    if len(text) >= 3 and text[1:3] == ":/":
        return f"/mnt/{text[0].lower()}/{text[3:]}"
    if len(text) >= 3 and text[1:3] == ":\\":
        tail = text[3:].replace("\\", "/")
        return f"/mnt/{text[0].lower()}/{tail}"
    return text


def _candidate_paths(raw: str) -> list[Path]:
    text = (raw or "").strip()
    if not text:
        return []
    translated = _windows_path_to_wsl(text)
    items = [Path(text)]
    if translated != text:
        items.append(Path(translated))
    return items


def _effective_bind_host(host: str) -> str:
    return (host or "").strip() or "127.0.0.1"


def _resolve_model_path(raw_model: str) -> str:
    model = (raw_model or "").strip()
    if not model:
        model = str(PROJECT_ROOT.parent / "yolov5s.onnx")
    for path in _candidate_paths(model):
        if path.exists():
            if path.suffix.lower() == ".onnx":
                _LOG.info("resolved ONNX model path: %s", path)
                _LOG.info("ONNX path will use load_onnx+build+init_runtime() in VisionPipeline")
            return str(path)
    raise FileNotFoundError(f"model not found for fixed chain: {model}")


def _resolve_source(raw: str) -> Any:
    text = (raw or "").strip()
    if not text:
        text = str(PROJECT_ROOT.parent / "test.mp4")
    if text.isdigit():
        return int(text)
    for path in _candidate_paths(text):
        if path.exists():
            return str(path)
    raise FileNotFoundError(f"video source not found for fixed chain: {text}")


def _format_detections(snapshot: dict[str, Any], limit: int = 10) -> list[dict[str, Any]]:
    boxes = snapshot.get("boxes")
    classes = snapshot.get("classes")
    scores = snapshot.get("scores")
    if boxes is None or classes is None or scores is None:
        return []

    structs = []
    for i in range(min(len(scores), limit)):
        cls_id = int(classes[i])
        label = CLASSES[cls_id] if 0 <= cls_id < len(CLASSES) else f"cls_{cls_id}"
        score = float(scores[i])
        x1, y1, x2, y2 = [int(v) for v in boxes[i]]
        structs.append({"label": label, "score": score, "x1": x1, "y1": y1, "x2": x2, "y2": y2})
    return structs


def _render_overlay(frame, detections: list[dict[str, Any]]):
    if frame is None:
        return None
    image = frame.copy()
    if not detections:
        return image

    for det in detections:
        x1, y1, x2, y2 = int(det["x1"]), int(det["y1"]), int(det["x2"]), int(det["y2"])
        label = str(det["label"])
        score = float(det["score"])
        cv2.rectangle(image, (x1, y1), (x2, y2), (52, 205, 50), 2)
        text = f"{label} {score:.2f}"
        (tw, th), _ = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)
        text_y = max(th + 4, y1 - 4)
        cv2.rectangle(image, (x1, text_y - th - 6), (x1 + tw + 2, text_y), (0, 0, 0), -1)
        cv2.putText(image, text, (x1 + 1, text_y - 4), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (230, 255, 230), 1, cv2.LINE_AA)
    return image


def _encode_frame(frame) -> tuple[str, dict[str, float]]:
    if frame is None:
        return "", {"jpeg_encode_ms": 0.0, "base64_ms": 0.0}
    image = frame
    if frame.shape[1] > _TX_MAX_WIDTH:
        scale = float(_TX_MAX_WIDTH) / float(frame.shape[1])
        image = cv2.resize(
            frame,
            (_TX_MAX_WIDTH, max(1, int(round(frame.shape[0] * scale)))),
            interpolation=cv2.INTER_AREA,
        )
    t0 = time.perf_counter()
    ok, encoded = cv2.imencode(".jpg", image, [int(cv2.IMWRITE_JPEG_QUALITY), int(_TX_JPEG_QUALITY)])
    jpeg_encode_ms = (time.perf_counter() - t0) * 1000.0
    if not ok:
        return "", {"jpeg_encode_ms": jpeg_encode_ms, "base64_ms": 0.0}
    t1 = time.perf_counter()
    encoded_b64 = base64.b64encode(encoded.tobytes()).decode("ascii")
    return encoded_b64, {
        "jpeg_encode_ms": jpeg_encode_ms,
        "base64_ms": (time.perf_counter() - t1) * 1000.0,
    }


def _build_payload(snapshot: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any]]:
    det_structs = _format_detections(snapshot)
    t0 = time.perf_counter()
    overlay = _render_overlay(snapshot.get("frame"), det_structs)
    draw_ms = (time.perf_counter() - t0) * 1000.0
    frame_h, frame_w = (0, 0) if overlay is None else (int(overlay.shape[0]), int(overlay.shape[1]))
    frame_jpeg_b64, encode_stats = _encode_frame(overlay)
    payload = {
        "ts": time.time(),
        "fps": float(snapshot.get("fps", 0.0)),
        "camera_online": bool(snapshot.get("camera_online", False)),
        "model_loaded": bool(snapshot.get("model_loaded", False)),
        "frame_count": int(snapshot.get("frame_count", 0)),
        "error": str(snapshot.get("error_msg", "") or ""),
        "detections": det_structs,
        "frame_width": frame_w,
        "frame_height": frame_h,
        "frame_jpeg_b64": frame_jpeg_b64,
    }
    return payload, {
        "draw_ms": draw_ms,
        "jpeg_encode_ms": encode_stats["jpeg_encode_ms"],
        "base64_ms": encode_stats["base64_ms"],
        "detection_count": len(det_structs),
        "overlay_width": frame_w,
        "overlay_height": frame_h,
    }


def _snapshot_changed(snapshot: dict[str, Any], last_frame: int | None, last_error: str | None, last_heartbeat: float, now: float) -> bool:
    return (
        int(snapshot.get("frame_count", 0)) != last_frame or
        str(snapshot.get("error_msg", "") or "") != last_error or
        (now - last_heartbeat) >= _HEARTBEAT_SEC
    )


def _on_signal(signum, frame):  # type: ignore[no-untyped-def]
    del signum, frame
    global _RUNNING
    _RUNNING = False


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="WSL RKNN -> Windows Qt vision bridge")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=17655)
    parser.add_argument("--model", default="")
    parser.add_argument("--source", default="")
    parser.add_argument("--log-level", default="INFO")
    parser.add_argument("--profile-output", default="")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    logging.basicConfig(level=getattr(logging, args.log_level.upper(), logging.INFO), format="%(asctime)s [%(levelname)s] %(name)s - %(message)s")
    signal.signal(signal.SIGINT, _on_signal)
    signal.signal(signal.SIGTERM, _on_signal)

    bind_host = _effective_bind_host(args.host)
    try:
        source = _resolve_source(args.source)
        model_path = _resolve_model_path(args.model)
    except FileNotFoundError as exc:
        _LOG.error("%s", exc)
        return 2
    _LOG.info(
        "starting bridge host=%s (requested=%s) port=%s source=%s model=%s img_size=%s tx_jpeg=%s/%s",
        bind_host,
        args.host,
        args.port,
        source,
        model_path,
        int(IMG_SIZE),
        _TX_MAX_WIDTH,
        _TX_JPEG_QUALITY,
    )

    profiler = VisionTimingProfiler(args.profile_output)
    profiler.set_runtime_field("bridge_bind_host", bind_host)
    profiler.set_runtime_field("bridge_requested_host", args.host)
    profiler.set_runtime_field("bridge_port", int(args.port))
    profiler.set_runtime_field("bridge_source", str(source))
    profiler.set_runtime_field("bridge_model_path", model_path)
    profiler.set_runtime_field("tx_max_width", int(_TX_MAX_WIDTH))
    profiler.set_runtime_field("tx_jpeg_quality", int(_TX_JPEG_QUALITY))

    pipeline = VisionPipeline(model_path=model_path, camera_source=source, img_size=int(IMG_SIZE), profiler=profiler)
    pipeline.start()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((bind_host, args.port))
    server.listen(8)
    server.setblocking(False)

    clients: list[socket.socket] = []
    client_names: dict[int, str] = {}
    last_frame = None
    last_error = None
    last_heartbeat = 0.0

    try:
        while _RUNNING:
            while True:
                try:
                    conn, addr = server.accept()
                except BlockingIOError:
                    break
                conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                conn.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
                # Keep send path tolerant to short UI stalls while preserving fast error feedback.
                conn.settimeout(1.0)
                clients.append(conn)
                client_names[id(conn)] = f"{addr[0]}:{addr[1]}"
                profiler.increment_counter("bridge", "client_connect_count")
                _LOG.info("client connected: %s (total=%d)", client_names[id(conn)], len(clients))

            if clients:
                now = time.monotonic()
                summary = pipeline.state.get_snapshot(include_frame=False)
                if _snapshot_changed(summary, last_frame, last_error, last_heartbeat, now):
                    payload, payload_timing = _build_payload(pipeline.state.get_snapshot(include_frame=True))
                    blob = (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
                    stale: list[socket.socket] = []
                    try:
                        send_t0 = time.perf_counter()
                        for conn in clients:
                            try:
                                conn.sendall(blob)
                            except socket.timeout:
                                # Do not eagerly drop client on short stalls (for example screenshot capture).
                                continue
                            except OSError as exc:
                                _LOG.warning("client disconnected: %s (%s)", client_names.get(id(conn), "unknown"), exc)
                                stale.append(conn)
                                profiler.increment_counter("bridge", "client_disconnect_count")
                        if stale:
                            for conn in stale:
                                try:
                                    conn.close()
                                except OSError:
                                    pass
                                if conn in clients:
                                    clients.remove(conn)
                                client_names.pop(id(conn), None)
                        profiler.record_bridge_frame(
                            frame_id=int(summary.get("frame_count", 0)),
                            frame_count=int(summary.get("frame_count", 0)),
                            draw_ms=float(payload_timing["draw_ms"]),
                            jpeg_encode_ms=float(payload_timing["jpeg_encode_ms"]),
                            base64_ms=float(payload_timing["base64_ms"]),
                            send_ms=(time.perf_counter() - send_t0) * 1000.0,
                            payload_bytes=len(blob),
                            detection_count=int(payload_timing["detection_count"]),
                            client_count=len(clients),
                            snapshot_fps=float(summary.get("fps", 0.0)),
                            overlay_width=int(payload_timing["overlay_width"]),
                            overlay_height=int(payload_timing["overlay_height"]),
                        )
                        last_frame = int(summary.get("frame_count", 0))
                        last_error = str(summary.get("error_msg", "") or "")
                        last_heartbeat = now
                    except OSError as exc:
                        _LOG.warning("bridge send loop error: %s", exc)
                        profiler.increment_counter("bridge", "send_loop_error_count")

            time.sleep(_LOOP_SLEEP_SEC)
    finally:
        for conn in list(clients):
            try:
                conn.close()
            except OSError:
                pass
        clients.clear()
        client_names.clear()
        server.close()
        pipeline.stop()
        profiler.write_json()
        _LOG.info("bridge stopped")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

