"""
YOLOv5 post-processing module for RKNN inference on RK3568.

Extracted from test_rknn_lite_usb.py for use in a 3-thread RKNN pipeline.
Provides image preprocessing (letterbox), YOLOv5 output decoding,
non-maximum suppression, and bounding-box drawing utilities.
"""

import numpy as np
import cv2

# ---------------------------------------------------------------------------
# Configurable thresholds and input size
# ---------------------------------------------------------------------------
OBJ_THRESH = 0.25
NMS_THRESH = 0.45
IMG_SIZE   = 640

# ---------------------------------------------------------------------------
# YOLOv5s standard anchors & masks
# ---------------------------------------------------------------------------
MASKS   = [[0, 1, 2], [3, 4, 5], [6, 7, 8]]
ANCHORS = [
    [10, 13], [16, 30], [33, 23],
    [30, 61], [62, 45], [59, 119],
    [116, 90], [156, 198], [373, 326],
]

# ---------------------------------------------------------------------------
# COCO 80-class names
# ---------------------------------------------------------------------------
CLASSES = (
    "person", "bicycle", "car", "motorbike", "aeroplane", "bus", "train",
    "truck", "boat", "traffic light", "fire hydrant", "stop sign",
    "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep",
    "cow", "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella",
    "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard",
    "sports ball", "kite", "baseball bat", "baseball glove", "skateboard",
    "surfboard", "tennis racket", "bottle", "wine glass", "cup", "fork",
    "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair",
    "sofa", "pottedplant", "bed", "diningtable", "toilet", "tvmonitor",
    "laptop", "mouse", "remote", "keyboard", "cell phone", "microwave",
    "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase",
    "scissors", "teddy bear", "hair drier", "toothbrush",
)


# ===================================================================
# Math helpers
# ===================================================================

def sigmoid(x):
    """Element-wise sigmoid, clipped for numerical stability."""
    return 1.0 / (1.0 + np.exp(-np.clip(x, -88.0, 88.0)))


def xywh2xyxy(x):
    """Convert [cx, cy, w, h] boxes to [x1, y1, x2, y2] format.

    Parameters
    ----------
    x : np.ndarray, shape (..., 4)
        Bounding boxes in centre-width-height format.

    Returns
    -------
    y : np.ndarray, shape (..., 4)
        Bounding boxes in corner format.
    """
    y = np.copy(x)
    y[..., 0] = x[..., 0] - x[..., 2] / 2  # x1
    y[..., 1] = x[..., 1] - x[..., 3] / 2  # y1
    y[..., 2] = x[..., 0] + x[..., 2] / 2  # x2
    y[..., 3] = x[..., 1] + x[..., 3] / 2  # y2
    return y


# ===================================================================
# YOLOv5 output decoding
# ===================================================================

def process(input_data, mask, anchors):
    """Decode a single YOLOv5 detection head output.

    Parameters
    ----------
    input_data : np.ndarray
        Raw network output for one detection scale.
        Accepted shapes:
        - (grid_h, grid_w, num_anchors, 5+num_cls)  — from original test_rknn_lite_usb.py
        - (batch, num_anchors, grid_h, grid_w, 5+num_cls) — batch format
    mask : list[int]
        Indices into the full anchor list for this head.
    anchors : list[list[int]]
        Full anchor list (9 anchors for YOLOv5).

    Returns
    -------
    box : np.ndarray, shape (N, 4)
    box_confidence : np.ndarray, shape (N, 1)
    box_class_probs : np.ndarray, shape (N, num_cls)
    """
    anchors_for_head = [anchors[m] for m in mask]

    # Support original 4D format: (grid_h, grid_w, num_anchors, 5+C)
    if input_data.ndim == 4:
        grid_h, grid_w = input_data.shape[0], input_data.shape[1]
        num_anchors = input_data.shape[2]

        box_confidence = sigmoid(input_data[..., 4])
        box_confidence = np.expand_dims(box_confidence, -1)
        box_class_probs = sigmoid(input_data[..., 5:])
        box_xy = sigmoid(input_data[..., :2]) * 2 - 0.5

        col = np.tile(np.arange(0, grid_w), grid_w).reshape(-1, grid_w)
        row = np.tile(np.arange(0, grid_h).reshape(-1, 1), grid_h)
        col = col.reshape(grid_h, grid_w, 1, 1).repeat(num_anchors, axis=-2)
        row = row.reshape(grid_h, grid_w, 1, 1).repeat(num_anchors, axis=-2)
        grid = np.concatenate((col, row), axis=-1)
        box_xy += grid
        box_xy *= int(IMG_SIZE / grid_h)

        box_wh = (sigmoid(input_data[..., 2:4]) * 2) ** 2
        box_wh = box_wh * np.array(anchors_for_head, dtype=np.float32)

        box = np.concatenate((box_xy, box_wh), axis=-1)
        box = box.reshape(-1, 4)
        box = xywh2xyxy(box)
        box_confidence = box_confidence.reshape(-1, 1)
        box_class_probs = box_class_probs.reshape(-1, box_class_probs.shape[-1])
        return box, box_confidence, box_class_probs

    # 5D batch format: (batch, num_anchors, grid_h, grid_w, 5+C)
    box_confidence = sigmoid(input_data[..., 4])
    box_confidence = np.expand_dims(box_confidence, -1)
    box_class_probs = sigmoid(input_data[..., 5:])
    box_xy = sigmoid(input_data[..., :2]) * 2 - 0.5

    box_wh = (sigmoid(input_data[..., 2:4]) * 2) ** 2
    anchors_arr = np.array(anchors_for_head, dtype=np.float32)
    for i, anchor in enumerate(anchors_arr):
        box_wh[:, i, :, :, 0] *= anchor[0]
        box_wh[:, i, :, :, 1] *= anchor[1]

    grid_h, grid_w = input_data.shape[2], input_data.shape[3]
    col = np.arange(grid_w).reshape(1, 1, 1, grid_w)
    row = np.arange(grid_h).reshape(1, 1, grid_h, 1)

    box_xy[..., 0] += col
    box_xy[..., 1] += row
    box_xy *= int(IMG_SIZE / grid_h)
    box_wh *= int(IMG_SIZE / grid_h) / IMG_SIZE
    box_wh[..., 0] *= IMG_SIZE
    box_wh[..., 1] *= IMG_SIZE

    box = np.concatenate((box_xy, box_wh), axis=-1)
    box = box.reshape(-1, 4)
    box = xywh2xyxy(box)
    box_confidence = box_confidence.reshape(-1, 1)
    box_class_probs = box_class_probs.reshape(-1, box_class_probs.shape[-1])
    return box, box_confidence, box_class_probs


def filter_boxes(boxes, box_confidences, box_class_probs,
                 obj_thresh=None):
    """Filter detections by objectness × class probability.

    Parameters
    ----------
    boxes : np.ndarray, shape (N, 4)
    box_confidences : np.ndarray, shape (N, 1)
    box_class_probs : np.ndarray, shape (N, num_cls)
    obj_thresh : float, optional
        Objectness threshold. Defaults to module-level ``OBJ_THRESH``.

    Returns
    -------
    boxes : np.ndarray, shape (M, 4)
    classes : np.ndarray, shape (M,)
    scores : np.ndarray, shape (M,)
    """
    if obj_thresh is None:
        obj_thresh = OBJ_THRESH

    box_scores = box_confidences * box_class_probs
    box_classes = np.argmax(box_scores, axis=-1)
    box_class_scores = np.max(box_scores, axis=-1)

    keep = box_class_scores > obj_thresh
    boxes = boxes[keep]
    classes = box_classes[keep]
    scores = box_class_scores[keep]

    return boxes, classes, scores


def nms_boxes(boxes, scores, nms_thresh=None):
    """Pure-numpy Non-Maximum Suppression.

    Parameters
    ----------
    boxes : np.ndarray, shape (N, 4)
        Bounding boxes in ``[x1, y1, x2, y2]`` format.
    scores : np.ndarray, shape (N,)
        Confidence scores.
    nms_thresh : float, optional
        IoU threshold for suppression. Defaults to ``NMS_THRESH``.

    Returns
    -------
    keep : list[int]
        Indices of boxes that survive NMS.
    """
    if nms_thresh is None:
        nms_thresh = NMS_THRESH

    x1 = boxes[:, 0]
    y1 = boxes[:, 1]
    x2 = boxes[:, 2]
    y2 = boxes[:, 3]

    areas = (x2 - x1) * (y2 - y1)
    order = scores.argsort()[::-1]

    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)

        xx1 = np.maximum(x1[i], x1[order[1:]])
        yy1 = np.maximum(y1[i], y1[order[1:]])
        xx2 = np.minimum(x2[i], x2[order[1:]])
        yy2 = np.minimum(y2[i], y2[order[1:]])

        w = np.maximum(0.0, xx2 - xx1)
        h = np.maximum(0.0, yy2 - yy1)
        inter = w * h

        iou = inter / (areas[i] + areas[order[1:]] - inter)

        inds = np.where(iou <= nms_thresh)[0]
        order = order[inds + 1]

    return keep


def yolov5_post_process(input_data, obj_thresh=None, nms_thresh=None):
    """Full YOLOv5 post-processing: decode heads, filter, NMS.

    Parameters
    ----------
    input_data : list[np.ndarray]
        List of 3 raw outputs from the RKNN model, one per detection head.
        Each array has shape ``(1, num_anchors, grid_h, grid_w, 5+num_cls)``.
    obj_thresh : float, optional
        Objectness threshold. Defaults to module-level ``OBJ_THRESH``.
    nms_thresh : float, optional
        NMS IoU threshold. Defaults to module-level ``NMS_THRESH``.

    Returns
    -------
    boxes : np.ndarray or None
        Filtered bounding boxes, shape ``(K, 4)`` in ``[x1, y1, x2, y2]``.
    classes : np.ndarray or None
        Class indices, shape ``(K,)``.
    scores : np.ndarray or None
        Final scores, shape ``(K,)``.
    Returns ``(None, None, None)`` when no detection passes the thresholds.
    """
    if obj_thresh is None:
        obj_thresh = OBJ_THRESH
    if nms_thresh is None:
        nms_thresh = NMS_THRESH

    all_boxes, all_classes, all_scores = [], [], []
    for i, head_out in enumerate(input_data):
        b, c_conf, c_probs = process(head_out, MASKS[i], ANCHORS)
        b, c, s = filter_boxes(b, c_conf, c_probs, obj_thresh)
        all_boxes.append(b)
        all_classes.append(c)
        all_scores.append(s)

    boxes = np.concatenate(all_boxes)
    classes = np.concatenate(all_classes)
    scores = np.concatenate(all_scores)

    if boxes.shape[0] == 0:
        return None, None, None

    # Per-class NMS
    unique_classes = set(classes)
    nms_boxes_out, nms_classes_out, nms_scores_out = [], [], []

    for cls in unique_classes:
        idx = np.where(classes == cls)[0]
        cls_boxes = boxes[idx]
        cls_scores = scores[idx]
        keep = nms_boxes(cls_boxes, cls_scores, nms_thresh)
        nms_boxes_out.append(cls_boxes[keep])
        nms_classes_out.append(np.full(len(keep), cls, dtype=np.int32))
        nms_scores_out.append(cls_scores[keep])

    if not nms_boxes_out:
        return None, None, None

    result_boxes = np.concatenate(nms_boxes_out)
    result_classes = np.concatenate(nms_classes_out)
    result_scores = np.concatenate(nms_scores_out)

    return result_boxes, result_classes, result_scores


# ===================================================================
# Image pre/post-processing helpers
# ===================================================================

def letterbox(im, new_shape=None, color=(0, 0, 0)):
    """Resize and pad an image to ``new_shape`` while keeping aspect ratio.

    Parameters
    ----------
    im : np.ndarray
        Input BGR image (HWC).
    new_shape : int or tuple[int, int], optional
        Target size ``(h, w)``. Defaults to ``(IMG_SIZE, IMG_SIZE)``.
    color : tuple[int, int, int]
        Padding colour.

    Returns
    -------
    im_padded : np.ndarray
        Letterboxed image of shape ``(new_h, new_w, 3)``.
    ratio : float
        Scale factor applied.
    (dw, dh) : tuple[int, int]
        Padding added (width, height) on each side.
    """
    if new_shape is None:
        new_shape = (IMG_SIZE, IMG_SIZE)
    if isinstance(new_shape, int):
        new_shape = (new_shape, new_shape)

    h, w = im.shape[:2]
    target_h, target_w = new_shape

    ratio = min(target_w / w, target_h / h)
    new_unpad_w = int(round(w * ratio))
    new_unpad_h = int(round(h * ratio))

    dw = target_w - new_unpad_w
    dh = target_h - new_unpad_h

    # Divide padding evenly on both sides
    dw //= 2
    dh //= 2

    if (w, h) != (new_unpad_w, new_unpad_h):
        im = cv2.resize(im, (new_unpad_w, new_unpad_h),
                        interpolation=cv2.INTER_LINEAR)

    top, bottom = dh, target_h - new_unpad_h - dh
    left, right = dw, target_w - new_unpad_w - dw
    im_padded = cv2.copyMakeBorder(im, top, bottom, left, right,
                                   cv2.BORDER_CONSTANT, value=color)

    return im_padded, ratio, (dw, dh)


def draw(image, boxes, scores, classes):
    """Draw bounding boxes and labels on an image.

    Parameters
    ----------
    image : np.ndarray
        BGR image (HWC, uint8).
    boxes : np.ndarray, shape (K, 4)
        Bounding boxes in ``[x1, y1, x2, y2]`` (pixel coords on the
        letterboxed image).
    scores : np.ndarray, shape (K,)
        Detection scores.
    classes : np.ndarray, shape (K,)
        Class indices (0-79 for COCO).

    Returns
    -------
    image : np.ndarray
        The input image with drawn bounding boxes (modified in-place).
    """
    for box, score, cls in zip(boxes, scores, classes):
        x1, y1, x2, y2 = int(box[0]), int(box[1]), int(box[2]), int(box[3])
        cls_id = int(cls)
        label = f"{CLASSES[cls_id]} {score:.2f}"

        color = _class_color(cls_id)
        cv2.rectangle(image, (x1, y1), (x2, y2), color, 2)

        # Label background
        (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX,
                                      0.5, 1)
        cv2.rectangle(image, (x1, y1 - th - 6), (x1 + tw, y1), color, -1)
        cv2.putText(image, label, (x1, y1 - 4),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1,
                    cv2.LINE_AA)

    return image


def _class_color(cls_id):
    """Return a deterministic BGR colour for a class index."""
    # Simple palette via hashing
    np.random.seed(cls_id)
    return tuple(int(c) for c in np.random.randint(0, 255, size=3))
