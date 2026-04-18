"""
3-thread RKNN vision pipeline for RK3568.

Capture -> Inference -> PostProcess pipeline using threading and queues.
Uses real hardware (RKNNLite + USB camera) on the RK3568 board.

Threads
-------
1. **Capture**      – reads frames from camera, preprocesses (resize +
   BGR→RGB), enqueues into ``capture_queue``.
2. **Inference**    – dequeues preprocessed frames, runs RKNN model,
   enqueues raw outputs into ``result_queue``.
3. **PostProcess**  – dequeues inference results, runs YOLOv5 post-
   processing, updates the thread-safe :class:`VisionPipelineState`.

Usage
-----
::

    from project2.vision.rknn_pipeline import VisionPipeline

    pipeline = VisionPipeline(
        model_path="yolov5s.rknn",
        camera_source=0,
    )
    pipeline.start()
    # ... poll pipeline.state.get_snapshot() ...
    pipeline.stop()
"""

import threading
import queue
import time
import numpy as np
import logging

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Thread-safe shared state
# ---------------------------------------------------------------------------


class VisionPipelineState:
    """Thread-safe shared state for vision pipeline results.

    All public methods acquire an internal lock so they are safe to call
    from any thread.
    """

    def __init__(self):
        self._lock = threading.Lock()
        self._frame = None           # latest BGR frame for display
        self._boxes = None           # detected boxes  (K, 4)
        self._classes = None         # detected classes (K,)
        self._scores = None          # detection scores (K,)
        self._fps = 0.0              # current FPS
        self._model_loaded = False
        self._camera_online = False
        self._frame_count = 0
        self._last_update = 0.0
        self._error_msg = ""

    # -- snapshot ---------------------------------------------------------

    def get_snapshot(self) -> dict:
        """Return a dict snapshot of current state.

        Arrays are copied so the caller can use them without holding
        the lock.
        """
        with self._lock:
            return {
                "frame": self._frame.copy() if self._frame is not None else None,
                "boxes": self._boxes.copy() if self._boxes is not None else None,
                "classes": self._classes.copy() if self._classes is not None else None,
                "scores": self._scores.copy() if self._scores is not None else None,
                "fps": self._fps,
                "model_loaded": self._model_loaded,
                "camera_online": self._camera_online,
                "frame_count": self._frame_count,
                "last_update": self._last_update,
                "error_msg": self._error_msg,
            }

    # -- update helpers ---------------------------------------------------

    def update_detections(self, frame, boxes, classes, scores, fps):
        """Update detection results (called from postprocess thread)."""
        with self._lock:
            self._frame = frame
            self._boxes = boxes
            self._classes = classes
            self._scores = scores
            self._fps = fps
            self._frame_count += 1
            self._last_update = time.time()

    def set_model_status(self, loaded, error=""):
        """Update model load status."""
        with self._lock:
            self._model_loaded = loaded
            if error:
                self._error_msg = error

    def set_camera_status(self, online):
        """Update camera online status."""
        with self._lock:
            self._camera_online = online

    # -- convenience read-only properties ---------------------------------

    @property
    def frame_count(self):
        with self._lock:
            return self._frame_count

    @property
    def fps(self):
        with self._lock:
            return self._fps

    @property
    def model_loaded(self):
        with self._lock:
            return self._model_loaded

    @property
    def camera_online(self):
        with self._lock:
            return self._camera_online


# ---------------------------------------------------------------------------
# 3-thread Vision Pipeline
# ---------------------------------------------------------------------------


class VisionPipeline:
    """3-thread RKNN vision pipeline for RK3568.

    Parameters
    ----------
    model_path : str
        Path to the ``.rknn`` model file.
    camera_source : int or str
        Camera index or video file path.
    img_size : int
        Input image size for the model (default 640).
    state : VisionPipelineState, optional
        Shared state object.  Created automatically if not provided.
    """

    def __init__(self, model_path="yolov5s.rknn", camera_source=0,
                 img_size=640, state=None):
        self.model_path = model_path
        self.camera_source = camera_source
        self.img_size = img_size
        self.state = state or VisionPipelineState()

        self._capture_queue = queue.Queue(maxsize=2)
        self._result_queue = queue.Queue(maxsize=2)
        self._running = False
        self._threads = []

        # Validate that hardware libraries are available
        import cv2 as _cv2                       # noqa: F401
        from rknnlite.api import RKNNLite as _R   # noqa: F401
        logger.info("VisionPipeline: real hardware mode")

    # -- lifecycle --------------------------------------------------------

    def start(self):
        """Start all 3 pipeline threads."""
        if self._running:
            logger.warning("VisionPipeline: already running")
            return

        self._running = True

        thread_targets = [
            ("capture",     self._capture_loop),
            ("inference",   self._inference_loop),
            ("postprocess", self._postprocess_loop),
        ]
        for name, target in thread_targets:
            t = threading.Thread(
                target=target, name=f"vision-{name}", daemon=True,
            )
            self._threads.append(t)
            t.start()
            logger.info("VisionPipeline: started %s thread", name)

    def stop(self):
        """Stop all threads gracefully."""
        logger.info("VisionPipeline: stopping…")
        self._running = False

        # Drain queues to unblock threads waiting on put()
        for q in (self._capture_queue, self._result_queue):
            try:
                while True:
                    q.get_nowait()
            except queue.Empty:
                pass

        for t in self._threads:
            t.join(timeout=3.0)
            if t.is_alive():
                logger.warning(
                    "VisionPipeline: thread %s did not stop within timeout",
                    t.name,
                )

        self._threads.clear()
        logger.info("VisionPipeline: stopped")

    @property
    def running(self):
        return self._running

    # -- Thread 1: capture ------------------------------------------------

    def _capture_loop(self):
        """Read frames from camera, preprocess, and enqueue."""
        cam = None
        try:
            cam = self._open_camera()
            if cam is None or not cam.isOpened():
                logger.error(
                    "VisionPipeline: failed to open camera %s",
                    self.camera_source,
                )
                self.state.set_camera_status(False)
                return

            self.state.set_camera_status(True)
            logger.info(
                "VisionPipeline: camera opened (%s)", self.camera_source,
            )

            while self._running:
                ret, frame = cam.read()
                if not ret:
                    logger.warning("VisionPipeline: camera read failed")
                    self.state.set_camera_status(False)
                    time.sleep(0.1)
                    continue

                self.state.set_camera_status(True)

                # Preprocess: letterbox resize to model input, BGR -> RGB
                img_rgb, original, meta = self._preprocess_frame(frame)
                img_input = np.expand_dims(img_rgb, 0)  # add batch dim

                try:
                    self._capture_queue.put(
                        (original, img_input, meta), timeout=0.1,
                    )
                except queue.Full:
                    pass  # drop frame if inference can't keep up

        except Exception as exc:
            logger.exception("VisionPipeline: capture thread error: %s", exc)
            self.state.set_camera_status(False)
        finally:
            if cam is not None:
                cam.release()
            logger.info("VisionPipeline: capture thread exiting")

    def _open_camera(self):
        """Open real camera source via OpenCV."""
        import cv2
        return cv2.VideoCapture(self.camera_source)

    def _preprocess_frame(self, frame):
        """Letterbox-resize and convert BGR -> RGB.

        Parameters
        ----------
        frame : np.ndarray
            Raw BGR frame from camera.

        Returns
        -------
        img_rgb : np.ndarray
            RGB image of shape ``(img_size, img_size, 3)``, dtype uint8.
        original : np.ndarray
            Original BGR frame (for drawing).
        meta : dict
            Letterbox metadata with keys ``src_h``, ``src_w``, ``ratio``,
            ``pad_w``, ``pad_h`` for inverse coordinate mapping.
        """
        from .yolov5_postprocess import letterbox

        original = frame.copy()
        import cv2
        image, ratio, (dw, dh) = letterbox(
            original, new_shape=(self.img_size, self.img_size), color=(0, 0, 0),
        )
        image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
        meta = {
            "src_h": int(original.shape[0]),
            "src_w": int(original.shape[1]),
            "ratio": float(ratio),
            "pad_w": float(dw),
            "pad_h": float(dh),
        }
        return image, original, meta

    # -- Thread 2: inference ----------------------------------------------

    def _inference_loop(self):
        """Dequeue preprocessed frames, run RKNN inference, enqueue results."""
        rknn = None
        try:
            rknn = self._load_model()
            if rknn is None:
                return  # error already logged

            while self._running:
                try:
                    frame, img_input, meta = self._capture_queue.get(timeout=0.5)
                except queue.Empty:
                    continue

                # Real RKNN inference + reshape
                outputs = rknn.inference(inputs=[img_input])
                input_data = self._reshape_outputs(outputs)
                result = (frame, input_data, None, None, False, meta)

                try:
                    self._result_queue.put(result, timeout=0.1)
                except queue.Full:
                    pass  # drop if postprocess can't keep up

        except Exception as exc:
            logger.exception("VisionPipeline: inference thread error: %s", exc)
            self.state.set_model_status(False, error=str(exc))
        finally:
            if rknn is not None:
                rknn.release()
            logger.info("VisionPipeline: inference thread exiting")

    def _load_model(self):
        """Instantiate and initialise the RKNN runtime.

        Returns
        -------
        rknn : RKNNLite or None
            ``None`` on failure.
        """
        from rknnlite.api import RKNNLite
        rknn = RKNNLite(verbose=False)

        ret = rknn.load_rknn(self.model_path)
        if ret != 0:
            msg = f"Failed to load model: {self.model_path} (ret={ret})"
            logger.error("VisionPipeline: %s", msg)
            self.state.set_model_status(False, error=msg)
            rknn.release()
            return None

        ret = rknn.init_runtime()
        if ret != 0:
            msg = f"Failed to init runtime (ret={ret})"
            logger.error("VisionPipeline: %s", msg)
            self.state.set_model_status(False, error=msg)
            rknn.release()
            return None

        self.state.set_model_status(True)
        logger.info("VisionPipeline: model loaded (%s)", self.model_path)
        return rknn

    @staticmethod
    def _reshape_outputs(outputs):
        """Reshape raw RKNN outputs for ``yolov5_post_process``.

        Real RKNN on RK3568 produces flat-channel tensors per head,
        e.g. shape ``(1, 255, 80, 80)`` or ``(255, 80, 80)``.  We
        reshape following the pattern in ``test_rknn_lite_usb.py``::

            data = out.reshape([3, -1] + list(out.shape[-2:]))
            # -> (3, 85, H, W)

        then transpose to ``(1, 3, H, W, 85)`` which is what
        :func:`yolov5_postprocess.process` expects (batch, anchor,
        grid_h, grid_w, attrs).
        """
        input_data = []
        for out in outputs:
            # Reshape: merge anchor and attribute dims
            # e.g. (1, 255, 80, 80) or (255, 80, 80) -> (3, 85, H, W)
            reshaped = out.reshape([3, -1] + list(out.shape[-2:]))
            # Transpose: (3, 85, H, W) -> (3, H, W, 85)
            transposed = np.transpose(reshaped, (0, 2, 3, 1))
            # Add batch dim: -> (1, 3, H, W, 85)
            input_data.append(np.expand_dims(transposed, 0))
        return input_data

    @staticmethod
    def _map_boxes_to_frame(boxes, frame, meta):
        """Map detection boxes from letterbox coordinates to original frame."""
        if boxes is None or len(boxes) == 0:
            return boxes
        mapped = np.asarray(boxes, dtype=np.float32).copy()
        ratio = float(meta.get("ratio", 1.0) or 1.0)
        pad_w = float(meta.get("pad_w", 0.0) or 0.0)
        pad_h = float(meta.get("pad_h", 0.0) or 0.0)
        mapped[:, [0, 2]] -= pad_w
        mapped[:, [1, 3]] -= pad_h
        if ratio > 0.0:
            mapped[:, :4] /= ratio
        h, w = frame.shape[:2]
        mapped[:, [0, 2]] = np.clip(mapped[:, [0, 2]], 0.0, float(w - 1))
        mapped[:, [1, 3]] = np.clip(mapped[:, [1, 3]], 0.0, float(h - 1))
        # Normalize coordinate order: ensure x1<=x2, y1<=y2
        x1 = np.minimum(mapped[:, 0], mapped[:, 2])
        x2 = np.maximum(mapped[:, 0], mapped[:, 2])
        y1 = np.minimum(mapped[:, 1], mapped[:, 3])
        y2 = np.maximum(mapped[:, 1], mapped[:, 3])
        mapped[:, 0] = x1
        mapped[:, 1] = y1
        mapped[:, 2] = x2
        mapped[:, 3] = y2
        return mapped

    # -- Thread 3: post-process -------------------------------------------

    def _postprocess_loop(self):
        """Dequeue inference results, run YOLOv5 post-processing, update state."""
        from .yolov5_postprocess import yolov5_post_process

        # Rolling FPS window
        fps_timestamps = []
        fps_window_size = 30

        try:
            while self._running:
                try:
                    result = self._result_queue.get(timeout=0.5)
                except queue.Empty:
                    continue

                frame, input_data, _, _, _, meta = result

                # Real mode: input_data is a list for yolov5_post_process
                boxes, classes, scores = yolov5_post_process(input_data)

                # Map detection boxes from letterbox coords to original frame
                if boxes is not None:
                    boxes = self._map_boxes_to_frame(boxes, frame, meta)

                # FPS calculation (rolling average)
                now = time.time()
                fps_timestamps.append(now)
                if len(fps_timestamps) > fps_window_size:
                    fps_timestamps = fps_timestamps[-fps_window_size:]
                if len(fps_timestamps) >= 2:
                    elapsed = fps_timestamps[-1] - fps_timestamps[0]
                    fps = ((len(fps_timestamps) - 1) / elapsed
                           if elapsed > 0 else 0.0)
                else:
                    fps = 0.0

                self.state.update_detections(frame, boxes, classes, scores, fps)

        except Exception as exc:
            logger.exception(
                "VisionPipeline: postprocess thread error: %s", exc,
            )
        finally:
            logger.info("VisionPipeline: postprocess thread exiting")


# ---------------------------------------------------------------------------
# Quick self-test
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s [%(threadName)s] %(levelname)s %(name)s - %(message)s",
    )

    print("=== VisionPipeline RK3568 hardware self-test ===")
    pipeline = VisionPipeline(
        model_path="yolov5s.rknn",
        camera_source=0,
    )

    pipeline.start()
    try:
        for i in range(10):
            time.sleep(0.3)
            snap = pipeline.state.get_snapshot()
            n_det = (len(snap["boxes"]) if snap["boxes"] is not None else 0)
            print(
                f"  tick {i}: frames={snap['frame_count']}, "
                f"fps={snap['fps']:.1f}, detections={n_det}, "
                f"cam={snap['camera_online']}, "
                f"model={snap['model_loaded']}",
            )
    finally:
        pipeline.stop()

    print("=== done ===")
