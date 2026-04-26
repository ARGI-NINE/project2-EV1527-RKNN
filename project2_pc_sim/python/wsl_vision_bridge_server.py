#!/usr/bin/env python3
"""
WSL vision bridge server for the PC simulator.

Required logic only:
- resolve model/source paths
- run VisionPipeline
- send newline-delimited JSON snapshots to Qt

The retained bridge chain requires an explicit WSL-side `--source`.
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
            return str(path)
    raise FileNotFoundError(f"model not found for fixed chain: {model}")


def _resolve_source(raw: str) -> Any:
    text = (raw or "").strip()
    if not text:
        raise FileNotFoundError("vision source is required for the fixed bridge chain; pass --source explicitly")
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


def _encode_frame(frame) -> str:
    if frame is None:
        return ""
    image = frame
    if frame.shape[1] > _TX_MAX_WIDTH:
        scale = float(_TX_MAX_WIDTH) / float(frame.shape[1])
        image = cv2.resize(
            frame,
            (_TX_MAX_WIDTH, max(1, int(round(frame.shape[0] * scale)))),
            interpolation=cv2.INTER_AREA,
        )
    ok, encoded = cv2.imencode(".jpg", image, [int(cv2.IMWRITE_JPEG_QUALITY), int(_TX_JPEG_QUALITY)])
    if not ok:
        return ""
    return base64.b64encode(encoded.tobytes()).decode("ascii")


def _build_payload(snapshot: dict[str, Any]) -> dict[str, Any]:
    det_structs = _format_detections(snapshot)
    overlay = _render_overlay(snapshot.get("frame"), det_structs)
    frame_h, frame_w = (0, 0) if overlay is None else (int(overlay.shape[0]), int(overlay.shape[1]))
    return {
        "ts": time.time(),
        "fps": float(snapshot.get("fps", 0.0)),
        "camera_online": bool(snapshot.get("camera_online", False)),
        "model_loaded": bool(snapshot.get("model_loaded", False)),
        "frame_count": int(snapshot.get("frame_count", 0)),
        "error": str(snapshot.get("error_msg", "") or ""),
        "detections": det_structs,
        "frame_width": frame_w,
        "frame_height": frame_h,
        "frame_jpeg_b64": _encode_frame(overlay),
    }


def _snapshot_changed(snapshot: dict[str, Any], last_frame: int | None, last_error: str | None, last_heartbeat: float, now: float) -> bool:
    return (
        int(snapshot.get("frame_count", 0)) != last_frame
        or str(snapshot.get("error_msg", "") or "") != last_error
        or (now - last_heartbeat) >= _HEARTBEAT_SEC
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
    parser.add_argument("--source", required=True, help="Explicit WSL-side video source path or camera index.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(name)s - %(message)s")
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

    pipeline = VisionPipeline(model_path=model_path, camera_source=source, img_size=int(IMG_SIZE))
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
                conn.settimeout(1.0)
                clients.append(conn)
                client_names[id(conn)] = f"{addr[0]}:{addr[1]}"

            if clients:
                now = time.monotonic()
                summary = pipeline.state.get_snapshot(include_frame=False)
                if _snapshot_changed(summary, last_frame, last_error, last_heartbeat, now):
                    payload = _build_payload(pipeline.state.get_snapshot(include_frame=True))
                    blob = (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
                    stale: list[socket.socket] = []
                    for conn in clients:
                        try:
                            conn.sendall(blob)
                        except socket.timeout:
                            continue
                        except OSError:
                            stale.append(conn)
                    if stale:
                        for conn in stale:
                            try:
                                conn.close()
                            except OSError:
                                pass
                            if conn in clients:
                                clients.remove(conn)
                            client_names.pop(id(conn), None)
                    last_frame = int(summary.get("frame_count", 0))
                    last_error = str(summary.get("error_msg", "") or "")
                    last_heartbeat = now

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

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
