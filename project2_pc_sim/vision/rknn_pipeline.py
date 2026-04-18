"""
RKNN vision pipeline for the PC simulator.

Keep the runtime path close to the user's RKNN example scripts:
- ONNX path uses RKNN.config(... target_platform='rk3568') + load_onnx(...) + build(...)
- RKNN path uses load_rknn(...)
- init_runtime() with no parameters on the PC side
- letterbox -> inference -> reshape -> postprocess
"""

from __future__ import annotations

import logging
import queue
import threading
import time
from pathlib import Path

import numpy as np

from .timing_profiler import VisionTimingProfiler
from .yolov5_postprocess import IMG_SIZE, letterbox, yolov5_post_process

logger = logging.getLogger(__name__)


def _runtime_error_message(exc: Exception) -> str:
    text = str(exc).strip()
    lowered = text.lower()
    if "no devices/emulators found" in lowered or "get board target failed" in lowered:
        return "Failed to init RKNN runtime: no reachable RK device/emulator was found. No detections will be produced."
    if text:
        return f"Failed to init RKNN runtime: {text}"
    return "Failed to init RKNN runtime due to an unknown error."


class VisionPipelineState:
    def __init__(self):
        self._lock = threading.Lock()
        self._frame = None
        self._boxes = None
        self._classes = None
        self._scores = None
        self._fps = 0.0
        self._model_loaded = False
        self._camera_online = False
        self._frame_count = 0
        self._last_update = 0.0
        self._error_msg = ""

    def get_snapshot(self, include_frame: bool = True) -> dict:
        with self._lock:
            return {
                "frame": self._frame.copy() if include_frame and self._frame is not None else None,
                "boxes": self._boxes.copy() if include_frame and self._boxes is not None else None,
                "classes": self._classes.copy() if include_frame and self._classes is not None else None,
                "scores": self._scores.copy() if include_frame and self._scores is not None else None,
                "fps": self._fps,
                "model_loaded": self._model_loaded,
                "camera_online": self._camera_online,
                "frame_count": self._frame_count,
                "last_update": self._last_update,
                "error_msg": self._error_msg,
            }

    def update_detections(self, frame, boxes, classes, scores, fps):
        with self._lock:
            self._frame = frame
            self._boxes = boxes
            self._classes = classes
            self._scores = scores
            self._fps = fps
            self._frame_count += 1
            self._last_update = time.time()

    def set_model_status(self, loaded: bool, error: str = ""):
        with self._lock:
            self._model_loaded = loaded
            self._error_msg = error or ""

    def set_camera_status(self, online: bool):
        with self._lock:
            self._camera_online = online


class VisionPipeline:
    def __init__(self, model_path="yolov5s.rknn", camera_source=0, img_size=IMG_SIZE, state=None, profiler=None):
        self.model_path = str(model_path)
        self.camera_source = camera_source
        self.img_size = int(img_size or IMG_SIZE)
        self.state = state or VisionPipelineState()
        self.profiler = profiler if profiler is not None else VisionTimingProfiler()

        self._capture_queue = queue.Queue(maxsize=1)
        self._result_queue = queue.Queue(maxsize=1)
        self._threads = []
        self._running = False
        self._source_frame_interval = 0.0
        self._next_frame_id = 0

        self.profiler.set_runtime_field("model_path", self.model_path)
        self.profiler.set_runtime_field("camera_source", str(self.camera_source))
        self.profiler.set_runtime_field("img_size", self.img_size)

    def _model_load_failed(self, rknn, message: str):
        logger.error("VisionPipeline: %s", message)
        self.state.set_model_status(False, error=message)
        self.profiler.set_runtime_field("model_loaded", False)
        self.profiler.set_runtime_field("model_error", message)
        if rknn is not None:
            try:
                rknn.release()
            except Exception:
                pass
        return None

    def start(self):
        if self._running:
            return
        self._running = True
        for name, target in (("capture", self._capture_loop), ("inference", self._inference_loop), ("postprocess", self._postprocess_loop)):
            thread = threading.Thread(target=target, name=f"vision-{name}", daemon=True)
            thread.start()
            self._threads.append(thread)
            logger.info("VisionPipeline: started %s thread", name)

    def stop(self):
        self._running = False
        for thread in self._threads:
            thread.join(timeout=3.0)
        self._threads.clear()

    def _capture_loop(self):
        import cv2

        cap = None
        next_deadline = 0.0
        is_video_file = self._is_video_file_source()
        try:
            cap = cv2.VideoCapture(self.camera_source)
            if not cap.isOpened():
                self.state.set_camera_status(False)
                logger.error("VisionPipeline: failed to open camera %s", self.camera_source)
                self.profiler.set_runtime_field("camera_opened", False)
                return

            self.state.set_camera_status(True)
            self.profiler.set_runtime_field("camera_opened", True)
            logger.info("VisionPipeline: camera opened (%s)", self.camera_source)

            frame_width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH) or 0)
            frame_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT) or 0)
            source_fps = float(cap.get(cv2.CAP_PROP_FPS) or 0.0)
            source_frame_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
            self.profiler.set_runtime_field("source_frame_width", frame_width)
            self.profiler.set_runtime_field("source_frame_height", frame_height)
            self.profiler.set_runtime_field("source_frame_count", source_frame_count)
            self.profiler.set_runtime_field("source_fps", source_fps)

            if is_video_file:
                if source_fps > 1.0:
                    self._source_frame_interval = 1.0 / source_fps
                    next_deadline = time.perf_counter()
                    logger.info(
                        "VisionPipeline: pacing video-file source at %.3f fps (interval %.3f ms)",
                        source_fps,
                        self._source_frame_interval * 1000.0,
                    )

            while self._running:
                if self._source_frame_interval > 0.0 and next_deadline > time.perf_counter():
                    time.sleep(next_deadline - time.perf_counter())

                frame_id = self._next_frame_id
                self._next_frame_id += 1
                t0 = time.perf_counter()
                ok, frame = cap.read()
                if not ok:
                    self.profiler.increment_counter("capture", "read_fail_count")
                    if is_video_file and cap.set(cv2.CAP_PROP_POS_FRAMES, 0):
                        if self._source_frame_interval > 0.0:
                            next_deadline = time.perf_counter()
                        self.profiler.increment_counter("capture", "loop_restart_count")
                        continue
                    self.state.set_camera_status(False)
                    time.sleep(0.1)
                    continue

                self.state.set_camera_status(True)
                img_rgb, frame_bgr, frame_meta = self._preprocess_frame(frame)
                queued = True
                try:
                    self._capture_queue.put((frame_id, frame_bgr, np.expand_dims(img_rgb, 0), frame_meta), timeout=0.1)
                except queue.Full:
                    queued = False
                    self.profiler.increment_counter("capture", "capture_queue_full_drop_count")

                self.profiler.record_stage_frame(
                    "capture",
                    frame_id,
                    (time.perf_counter() - t0) * 1000.0,
                    queued=queued,
                )

                if self._source_frame_interval > 0.0:
                    next_deadline += self._source_frame_interval
        except Exception as exc:
            logger.exception("VisionPipeline: capture thread error: %s", exc)
            self.state.set_camera_status(False)
        finally:
            if cap is not None:
                cap.release()
            logger.info("VisionPipeline: capture thread exiting")

    def _inference_loop(self):
        rknn = None
        try:
            model_load_t0 = time.perf_counter()
            rknn = self._load_model()
            self.profiler.set_runtime_field("model_prepare_ms", (time.perf_counter() - model_load_t0) * 1000.0)
            self.profiler.set_runtime_field("runtime_mode", "fallback_empty" if rknn is None else "rknn_inference")
            while self._running:
                try:
                    frame_id, frame, img_input, frame_meta = self._capture_queue.get(timeout=0.5)
                except queue.Empty:
                    self.profiler.increment_counter("inference", "capture_queue_empty_count")
                    continue

                t0 = time.perf_counter()
                if rknn is None:
                    result = (
                        frame_id,
                        frame,
                        np.empty((0, 4), dtype=np.float32),
                        np.empty((0,), dtype=np.int32),
                        np.empty((0,), dtype=np.float32),
                        True,
                        frame_meta,
                    )
                    self.profiler.increment_counter("inference", "runtime_fallback_frame_count")
                else:
                    outputs = rknn.inference(inputs=[img_input])
                    result = (frame_id, frame, self._reshape_outputs(outputs), None, None, False, frame_meta)

                queued = True
                try:
                    self._result_queue.put(result, timeout=0.1)
                except queue.Full:
                    queued = False
                    self.profiler.increment_counter("inference", "result_queue_full_drop_count")

                self.profiler.record_stage_frame(
                    "inference",
                    frame_id,
                    (time.perf_counter() - t0) * 1000.0,
                    queued=queued,
                    runtime_fallback=(rknn is None),
                )
        except Exception as exc:
            msg = _runtime_error_message(exc)
            logger.error("VisionPipeline: %s", msg)
            self.state.set_model_status(False, error=msg)
            self.profiler.set_runtime_field("model_loaded", False)
            self.profiler.set_runtime_field("model_error", msg)
        finally:
            if rknn is not None:
                try:
                    rknn.release()
                except Exception:
                    pass
            logger.info("VisionPipeline: inference thread exiting")

    def _postprocess_loop(self):
        fps_marks = []
        try:
            while self._running:
                try:
                    frame_id, frame, d1, d2, d3, already_postprocessed, frame_meta = self._result_queue.get(timeout=0.5)
                except queue.Empty:
                    self.profiler.increment_counter("postprocess", "result_queue_empty_count")
                    continue

                t0 = time.perf_counter()
                if already_postprocessed:
                    boxes, classes, scores = d1, d2, d3
                    self.profiler.increment_counter("postprocess", "already_postprocessed_frame_count")
                else:
                    self.profiler.mark_postprocess_call(Path(yolov5_post_process.__code__.co_filename).resolve())
                    boxes, classes, scores = yolov5_post_process(d1)
                    if boxes is not None:
                        boxes = self._map_boxes_to_frame(boxes, frame, frame_meta)

                now = time.time()
                fps_marks.append(now)
                if len(fps_marks) > 30:
                    fps_marks = fps_marks[-30:]
                fps = 0.0 if len(fps_marks) < 2 else (len(fps_marks) - 1) / max(1e-6, fps_marks[-1] - fps_marks[0])
                self.state.update_detections(frame, boxes, classes, scores, fps)
                self.profiler.record_stage_frame(
                    "postprocess",
                    frame_id,
                    (time.perf_counter() - t0) * 1000.0,
                    already_postprocessed=bool(already_postprocessed),
                    detection_count=0 if scores is None else int(len(scores)),
                )
        except Exception as exc:
            logger.exception("VisionPipeline: postprocess thread error: %s", exc)
        finally:
            logger.info("VisionPipeline: postprocess thread exiting")

    def _preprocess_frame(self, frame):
        import cv2

        original = frame.copy()
        image, ratio, (dw, dh) = letterbox(original, new_shape=(self.img_size, self.img_size), color=(0, 0, 0))
        image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
        meta = {
            "src_h": int(original.shape[0]),
            "src_w": int(original.shape[1]),
            "ratio": float(ratio),
            "pad_w": float(dw),
            "pad_h": float(dh),
        }
        return image, original, meta

    def _resolve_runtime_model_path(self) -> Path:
        path = Path(self.model_path)
        suffix = path.suffix.lower()
        if suffix not in {".onnx", ".rknn"}:
            raise ValueError(f"Unsupported runtime model path: {self.model_path}")
        if path.exists():
            return path
        raise FileNotFoundError(f"Runtime model not found: {path}")

    def _load_model(self):
        from rknn.api import RKNN

        try:
            runtime_model_path = self._resolve_runtime_model_path()
        except Exception as exc:
            msg = f"Failed to prepare RKNN model: {exc}"
            logger.error("VisionPipeline: %s", msg)
            self.state.set_model_status(False, error=msg)
            self.profiler.set_runtime_field("model_loaded", False)
            self.profiler.set_runtime_field("model_error", msg)
            return None

        model_suffix = runtime_model_path.suffix.lower()
        self.profiler.set_runtime_field("runtime_model_path", str(runtime_model_path))
        self.profiler.set_runtime_field("runtime_model_suffix", model_suffix)

        rknn = RKNN(verbose=False)
        try:
            if model_suffix == ".onnx":
                logger.info(
                    "VisionPipeline: ONNX path selected, using load_onnx+build+init_runtime (%s)",
                    runtime_model_path,
                )
                rknn.config(mean_values=[[0, 0, 0]], std_values=[[255, 255, 255]], target_platform="rk3568")
                ret = rknn.load_onnx(model=str(runtime_model_path))
                if ret != 0:
                    return self._model_load_failed(rknn, f"Failed to load ONNX model: {runtime_model_path} (ret={ret})")
                ret = rknn.build(do_quantization=False)
                if ret != 0:
                    return self._model_load_failed(rknn, f"Failed to build RKNN graph from ONNX: {runtime_model_path} (ret={ret})")
            else:
                ret = rknn.load_rknn(path=str(runtime_model_path))
                if ret != 0:
                    return self._model_load_failed(rknn, f"Failed to load RKNN model: {runtime_model_path} (ret={ret})")

            ret = rknn.init_runtime()
            if ret != 0:
                return self._model_load_failed(
                    rknn,
                    f"Failed to init RKNN runtime for {runtime_model_path} (ret={ret}). No detections will be produced.",
                )

            self.state.set_model_status(True)
            self.profiler.set_runtime_field("model_loaded", True)
            logger.info("VisionPipeline: model loaded (%s)", runtime_model_path)
            return rknn
        except Exception as exc:
            return self._model_load_failed(rknn, _runtime_error_message(exc))

    @staticmethod
    def _reshape_outputs(outputs):
        heads = []
        for out in outputs:
            arr = np.asarray(out)
            if arr.ndim == 4 and arr.shape[0] == 1:
                arr = arr[0]
            reshaped = arr.reshape([3, -1] + list(arr.shape[-2:]))
            heads.append(np.transpose(reshaped, (2, 3, 0, 1)).astype(np.float32, copy=False))
        return heads[:3]

    @staticmethod
    def _map_boxes_to_frame(boxes, frame, frame_meta):
        if boxes is None or len(boxes) == 0:
            return boxes
        mapped = np.asarray(boxes, dtype=np.float32).copy()
        ratio = float(frame_meta.get("ratio", 1.0) or 1.0)
        pad_w = float(frame_meta.get("pad_w", 0.0) or 0.0)
        pad_h = float(frame_meta.get("pad_h", 0.0) or 0.0)
        mapped[:, [0, 2]] -= pad_w
        mapped[:, [1, 3]] -= pad_h
        if ratio > 0.0:
            mapped[:, :4] /= ratio
        h, w = frame.shape[:2]
        x1 = np.minimum(mapped[:, 0], mapped[:, 2])
        x2 = np.maximum(mapped[:, 0], mapped[:, 2])
        y1 = np.minimum(mapped[:, 1], mapped[:, 3])
        y2 = np.maximum(mapped[:, 1], mapped[:, 3])
        mapped[:, 0] = np.clip(x1, 0.0, float(w - 1))
        mapped[:, 2] = np.clip(x2, 0.0, float(w - 1))
        mapped[:, 1] = np.clip(y1, 0.0, float(h - 1))
        mapped[:, 3] = np.clip(y2, 0.0, float(h - 1))
        return mapped

    def _is_video_file_source(self) -> bool:
        if isinstance(self.camera_source, int):
            return False
        source = str(self.camera_source).strip()
        if not source or source.isdigit():
            return False
        if source.lower().startswith(("rtsp://", "http://", "https://", "rtmp://", "udp://")):
            return False
        return Path(source).suffix.lower() in {".mp4", ".avi", ".mov", ".mkv", ".m4v", ".wmv", ".flv", ".ts", ".webm"}

