"""
Mock camera and RKNN inference module for hardware-free testing.

Provides MockCamera (simulates cv2.VideoCapture) and MockRKNNLite
(simulates rknnlite.api.RKNNLite) so the 3-thread vision pipeline can
be developed and tested on any PC without RK3568 hardware, USB cameras,
or real RKNN models.

MockCamera generates synthetic frames with moving coloured rectangles.
MockRKNNLite returns shaped numpy arrays matching YOLOv5 RKNN output
heads, and optionally provides pre-built detection results that will
survive the real yolov5_postprocess pipeline.

Usage:
    from mock_camera import MockCamera, MockRKNNLite
    cam = MockCamera(source=0, width=640, height=480, fps=25)
    rknn = MockRKNNLite()
    rknn.load_rknn("model.rknn")
    rknn.init_runtime()
    ret, frame = cam.read()
    outputs = rknn.inference([frame])
"""

import time
import random
import logging

import numpy as np

try:
    import cv2

    _HAS_CV2 = True
except ImportError:
    _HAS_CV2 = False

__all__ = ["MockCamera", "MockRKNNLite"]

logger = logging.getLogger(__name__)


# -----------------------------------------------------------------------
# MockCamera
# -----------------------------------------------------------------------

class MockCamera:
    """Simulates ``cv2.VideoCapture`` for testing without a real camera.

    If *source* is a path to an existing video file and OpenCV is available
    the file is played back.  Otherwise synthetic frames with moving
    coloured rectangles are generated.

    Parameters
    ----------
    source : int or str
        Camera index or path to a video file.
    width : int
        Frame width in pixels.
    height : int
        Frame height in pixels.
    fps : float
        Target frames-per-second (controls ``read()`` pacing).
    """

    def __init__(self, source=0, width=640, height=480, fps=25):
        self._width = width
        self._height = height
        self._fps = fps
        self._opened = True
        self._frame_idx = 0
        self._cap = None  # real cv2.VideoCapture, if any
        self._last_read_ts = 0.0

        # Try opening a real video file when source is a path
        if isinstance(source, str) and _HAS_CV2:
            try:
                cap = cv2.VideoCapture(source)
                if cap.isOpened():
                    self._cap = cap
                    logger.info("MockCamera: opened real video file %s", source)
                else:
                    cap.release()
                    logger.warning(
                        "MockCamera: cannot open '%s', falling back to "
                        "synthetic frames", source,
                    )
            except Exception as exc:  # noqa: BLE001
                logger.warning("MockCamera: cv2 error (%s), using synthetic", exc)

        if self._cap is None:
            logger.info(
                "MockCamera: generating synthetic %dx%d @ %d fps",
                width, height, fps,
            )

        # State for synthetic moving rectangles
        self._objects = self._init_objects(random.randint(2, 5))

    # -- synthetic object helpers -----------------------------------------

    @staticmethod
    def _random_color():
        return (
            random.randint(40, 255),
            random.randint(40, 255),
            random.randint(40, 255),
        )

    def _init_objects(self, n):
        """Create *n* randomly-placed rectangle descriptors."""
        objs = []
        for _ in range(n):
            w = random.randint(40, 120)
            h = random.randint(40, 120)
            x = random.randint(0, max(self._width - w, 1))
            y = random.randint(0, max(self._height - h, 1))
            dx = random.choice([-3, -2, -1, 1, 2, 3])
            dy = random.choice([-3, -2, -1, 1, 2, 3])
            color = self._random_color()
            objs.append({"x": x, "y": y, "w": w, "h": h,
                         "dx": dx, "dy": dy, "color": color})
        return objs

    def _update_objects(self):
        for obj in self._objects:
            obj["x"] += obj["dx"]
            obj["y"] += obj["dy"]
            # Bounce off walls
            if obj["x"] < 0 or obj["x"] + obj["w"] > self._width:
                obj["dx"] = -obj["dx"]
                obj["x"] = max(0, min(obj["x"], self._width - obj["w"]))
            if obj["y"] < 0 or obj["y"] + obj["h"] > self._height:
                obj["dy"] = -obj["dy"]
                obj["y"] = max(0, min(obj["y"], self._height - obj["h"]))

    def _render_synthetic_frame(self):
        """Draw a synthetic BGR frame with moving rectangles."""
        frame = np.zeros((self._height, self._width, 3), dtype=np.uint8)
        # dark-grey background with a grid for visual texture
        frame[:] = (30, 30, 30)

        self._update_objects()

        for obj in self._objects:
            x1, y1 = obj["x"], obj["y"]
            x2, y2 = x1 + obj["w"], y1 + obj["h"]
            # Filled rectangle
            frame[y1:y2, x1:x2] = obj["color"]
            # Border
            if _HAS_CV2:
                cv2.rectangle(frame, (x1, y1), (x2, y2), (255, 255, 255), 1)

        # Timestamp text
        ts_text = time.strftime("%H:%M:%S") + f".{int(time.time() * 100) % 100:02d}"
        frame_text = f"Frame {self._frame_idx}"
        if _HAS_CV2:
            cv2.putText(frame, ts_text, (10, 25),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 1,
                        cv2.LINE_AA)
            cv2.putText(frame, frame_text, (10, 50),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 200, 200), 1,
                        cv2.LINE_AA)
        else:
            # Burn simple markers without cv2
            frame[5:12, 5:80] = (0, 255, 0)

        return frame

    # -- public interface (mirrors cv2.VideoCapture) ----------------------

    def isOpened(self) -> bool:  # noqa: N802
        if self._cap is not None:
            return self._cap.isOpened()
        return self._opened

    def read(self):
        """Return ``(ret, frame)`` mimicking ``cv2.VideoCapture.read()``.

        Paces itself to approximately *fps* when generating synthetic data.
        """
        if not self._opened:
            return False, None

        # Real video file path
        if self._cap is not None:
            ret, frame = self._cap.read()
            if not ret:
                # Loop the video
                self._cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                ret, frame = self._cap.read()
            self._frame_idx += 1
            return ret, frame

        # Synthetic frame pacing
        now = time.time()
        elapsed = now - self._last_read_ts
        target_interval = 1.0 / self._fps
        if elapsed < target_interval:
            time.sleep(target_interval - elapsed)
        self._last_read_ts = time.time()

        frame = self._render_synthetic_frame()
        self._frame_idx += 1
        return True, frame

    def release(self):
        """Release resources."""
        self._opened = False
        if self._cap is not None:
            self._cap.release()
            self._cap = None
        logger.info("MockCamera: released (total frames: %d)", self._frame_idx)

    # context-manager support
    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.release()


# -----------------------------------------------------------------------
# MockRKNNLite
# -----------------------------------------------------------------------

_NUM_CLASSES = 80
_NUM_ANCHORS = 3
_BOX_ATTRS = 5  # x, y, w, h, objectness
_DEPTH = _NUM_ANCHORS * (_BOX_ATTRS + _NUM_CLASSES)  # 255


class MockRKNNLite:
    """Simulates ``rknnlite.api.RKNNLite`` for hardware-free testing.

    Produces either raw-shaped tensor outputs (suitable for feeding into
    ``yolov5_postprocess.yolov5_post_process``) or direct post-processed
    detection results via :meth:`inference_postprocessed`.

    Parameters
    ----------
    verbose : bool
        If ``True``, emit debug-level log messages.
    mock_postprocess : bool
        When ``True``, :meth:`inference` returns results that encode
        1-3 fake detections (person / car classes) into the raw tensor
        so that the real post-processing pipeline decodes valid boxes.
        When ``False`` (default), mostly-zero tensors are returned.
    """

    # Expose for code that checks these constants
    NPU_CORE_0 = 0
    NPU_CORE_1 = 1
    NPU_CORE_2 = 2
    NPU_CORE_AUTO = -1

    def __init__(self, verbose=False, mock_postprocess=True):
        self._verbose = verbose
        self._mock_postprocess = mock_postprocess
        self._model_path = None
        self._initialised = False
        self._frame_counter = 0
        if verbose:
            logger.setLevel(logging.DEBUG)
        logger.info("MockRKNNLite created (mock_postprocess=%s)", mock_postprocess)

    # -- lifecycle (mirrors RKNNLite API) ---------------------------------

    def load_rknn(self, path: str) -> int:
        """Simulate loading a .rknn model file.  Always succeeds."""
        self._model_path = path
        logger.info("MockRKNNLite: load_rknn('%s') -> 0 (OK)", path)
        return 0

    def init_runtime(self, target=None, **kwargs) -> int:
        """Simulate runtime initialisation.  Always succeeds."""
        self._initialised = True
        logger.info(
            "MockRKNNLite: init_runtime(target=%s) -> 0 (OK)", target,
        )
        return 0

    def release(self):
        """Release (no-op for mock)."""
        self._initialised = False
        logger.info("MockRKNNLite: released")

    # -- inference --------------------------------------------------------

    def inference(self, inputs, data_format=None, data_type=None):
        """Return a list of 3 numpy arrays shaped like YOLOv5 RKNN output.

        Output shapes (before reshape in postprocessing):
          - outputs[0]: ``(1, 3, 80, 80, 85)``  – P3 / stride-8
          - outputs[1]: ``(1, 3, 40, 40, 85)``  – P4 / stride-16
          - outputs[2]: ``(1, 3, 20, 20, 85)``  – P5 / stride-32

        When *mock_postprocess* is enabled the tensors contain
        strategically placed activations that decode to 1-3 bounding
        boxes through the real ``yolov5_post_process`` pipeline.
        """
        self._frame_counter += 1
        grid_sizes = [80, 40, 20]
        outputs = []

        if self._mock_postprocess:
            outputs = self._build_hot_outputs(grid_sizes)
        else:
            for gs in grid_sizes:
                arr = np.zeros(
                    (1, _NUM_ANCHORS, gs, gs, _BOX_ATTRS + _NUM_CLASSES),
                    dtype=np.float32,
                )
                outputs.append(arr)

        if self._verbose:
            logger.debug(
                "MockRKNNLite: inference frame %d -> %s",
                self._frame_counter,
                [o.shape for o in outputs],
            )
        return outputs

    def inference_postprocessed(self, inputs):
        """Return fake detections directly as ``(boxes, classes, scores)``.

        Bypasses raw-tensor generation entirely.  Returns 1-3 random
        detections of *person* (class 0) or *car* (class 2) with boxes
        inside a 640x640 coordinate space.

        Returns
        -------
        boxes : np.ndarray, shape (K, 4)
            ``[x1, y1, x2, y2]`` in pixel coordinates (0-640).
        classes : np.ndarray, shape (K,)
            Class indices (COCO).
        scores : np.ndarray, shape (K,)
            Confidence scores in ``[0.35, 0.95]``.
        """
        self._frame_counter += 1
        n_det = random.randint(1, 3)
        boxes, classes, scores = [], [], []

        for _ in range(n_det):
            cls = random.choice([0, 0, 2, 2, 1])  # person, car, bicycle
            cx = random.randint(80, 560)
            cy = random.randint(80, 560)
            bw = random.randint(40, 180)
            bh = random.randint(60, 220)
            x1 = max(0, cx - bw // 2)
            y1 = max(0, cy - bh // 2)
            x2 = min(640, cx + bw // 2)
            y2 = min(640, cy + bh // 2)
            boxes.append([x1, y1, x2, y2])
            classes.append(cls)
            scores.append(round(random.uniform(0.35, 0.95), 2))

        return (
            np.array(boxes, dtype=np.float32),
            np.array(classes, dtype=np.int32),
            np.array(scores, dtype=np.float32),
        )

    # -- internal helpers for hot-tensor generation -----------------------

    def _inverse_sigmoid(self, y):
        """Numerically stable inverse sigmoid (logit)."""
        y = np.clip(y, 1e-6, 1.0 - 1e-6)
        return np.log(y / (1.0 - y))

    def _build_hot_outputs(self, grid_sizes):
        """Build 3 output heads each containing 1-3 'hot' cells.

        The values are set so that after the sigmoid / anchor decoding
        in ``yolov5_postprocess.process`` the resulting boxes fall within
        the image and have high objectness + class probability for
        *person* (class 0) or *car* (class 2).
        """
        from itertools import cycle

        # Pre-defined detection specs that cycle each frame for variety
        det_specs = [
            {"grid_idx": 0, "anchor": 0, "row": 30, "col": 40,
             "cls": 0, "conf": 0.85, "bw": 0.15, "bh": 0.25},
            {"grid_idx": 1, "anchor": 1, "row": 15, "col": 20,
             "cls": 2, "conf": 0.75, "bw": 0.20, "bh": 0.12},
            {"grid_idx": 2, "anchor": 2, "row": 8,  "col": 10,
             "cls": 0, "conf": 0.65, "bw": 0.10, "bh": 0.30},
        ]

        # Vary positions slightly each frame
        rng = random.Random(self._frame_counter)
        n_det = rng.randint(1, 3)
        chosen = rng.sample(det_specs, n_det)

        outputs = []
        for gs in grid_sizes:
            arr = np.full(
                (1, _NUM_ANCHORS, gs, gs, _BOX_ATTRS + _NUM_CLASSES),
                -6.0,  # sigmoid(-6) ≈ 0.0025 -> effectively zero
                dtype=np.float32,
            )
            outputs.append(arr)

        for spec in chosen:
            gi = spec["grid_idx"]
            gs = grid_sizes[gi]
            a = spec["anchor"]
            # Clamp row/col to valid grid range
            row = min(spec["row"] + rng.randint(-3, 3), gs - 1)
            col = min(spec["col"] + rng.randint(-3, 3), gs - 1)
            row = max(row, 0)
            col = max(col, 0)

            cls_id = spec["cls"]
            obj_conf = spec["conf"] + rng.uniform(-0.1, 0.1)
            obj_conf = np.clip(obj_conf, 0.4, 0.95)

            cell = outputs[gi][0, a, row, col, :]

            # xy: sigmoid(val)*2 - 0.5 maps to grid offset
            # We want centre roughly at (col+0.5, row+0.5)
            target_xy = 0.5  # centre of cell
            # Solve: sigmoid(v)*2 - 0.5 = target_xy  =>  sigmoid(v) = (target_xy+0.5)/2
            sig_xy = (target_xy + 0.5) / 2.0
            cell[0] = self._inverse_sigmoid(sig_xy)
            cell[1] = self._inverse_sigmoid(sig_xy)

            # wh: (sigmoid(v)*2)^2 * anchor * stride / IMG_SIZE
            # We want a fractional box width of spec["bw"] of IMG_SIZE
            # target_wh_pixels = spec["bw"] * 640
            # (sigmoid(v)*2)^2 * anchor_w * (640/gs) / 640 = spec["bw"]
            # => (sigmoid(v)*2)^2 = spec["bw"] * 640 / anchor_w / (640/gs) ... complex
            # Simpler: set sigmoid(v)*2 = 1.0, i.e. v = 0
            cell[2] = 0.0
            cell[3] = 0.0

            # Objectness
            cell[4] = self._inverse_sigmoid(float(obj_conf))

            # Class probability – set the target class high
            cls_conf = obj_conf * rng.uniform(0.8, 1.0)
            cell[_BOX_ATTRS + cls_id] = self._inverse_sigmoid(
                float(np.clip(cls_conf, 0.4, 0.95))
            )

        return outputs


# -----------------------------------------------------------------------
# Quick self-test
# -----------------------------------------------------------------------

if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)

    print("=== MockCamera test ===")
    cam = MockCamera(width=640, height=480, fps=5)
    for i in range(5):
        ret, frame = cam.read()
        print(f"  frame {i}: ret={ret}, shape={frame.shape}, "
              f"dtype={frame.dtype}")
    cam.release()

    print("\n=== MockRKNNLite test (raw tensors) ===")
    rknn = MockRKNNLite(verbose=True, mock_postprocess=True)
    rknn.load_rknn("yolov5s.rknn")
    rknn.init_runtime()
    dummy_input = [np.zeros((640, 640, 3), dtype=np.uint8)]
    outputs = rknn.inference(dummy_input)
    for idx, o in enumerate(outputs):
        print(f"  head {idx}: shape={o.shape}, "
              f"min={o.min():.3f}, max={o.max():.3f}")

    print("\n=== MockRKNNLite test (postprocessed) ===")
    boxes, classes, scores = rknn.inference_postprocessed(dummy_input)
    print(f"  detections: {len(boxes)}")
    for b, c, s in zip(boxes, classes, scores):
        print(f"    box={b}, class={c}, score={s:.2f}")

    rknn.release()

    # Optionally test with real postprocess pipeline
    try:
        from yolov5_postprocess import yolov5_post_process
        print("\n=== End-to-end: MockRKNNLite -> yolov5_post_process ===")
        rknn2 = MockRKNNLite(mock_postprocess=True)
        rknn2.load_rknn("test.rknn")
        rknn2.init_runtime()
        raw = rknn2.inference(dummy_input)
        boxes, classes, scores = yolov5_post_process(raw)
        if boxes is not None:
            print(f"  Decoded {len(boxes)} detections through real pipeline!")
            for b, c, s in zip(boxes, classes, scores):
                print(f"    box={b}, class={c}, score={s:.2f}")
        else:
            print("  No detections (tweak hot-cell values if needed)")
        rknn2.release()
    except ImportError:
        print("\n(yolov5_postprocess not on path, skipping e2e test)")
