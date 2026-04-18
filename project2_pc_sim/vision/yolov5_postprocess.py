"""
YOLOv5 post-processing helpers aligned to the RKNN example scripts.

Keep only reusable project logic:
- letterbox
- output decode
- NMS
- box drawing helper
"""

import numpy as np
import cv2

OBJ_THRESH = 0.25
NMS_THRESH = 0.45
IMG_SIZE = 640

MASKS = [[0, 1, 2], [3, 4, 5], [6, 7, 8]]
ANCHORS = [
    [10, 13], [16, 30], [33, 23],
    [30, 61], [62, 45], [59, 119],
    [116, 90], [156, 198], [373, 326],
]

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


def sigmoid(x):
    return 1.0 / (1.0 + np.exp(-np.clip(x, -88.0, 88.0)))


def xywh2xyxy(x):
    y = np.copy(x)
    y[..., 0] = x[..., 0] - x[..., 2] / 2
    y[..., 1] = x[..., 1] - x[..., 3] / 2
    y[..., 2] = x[..., 0] + x[..., 2] / 2
    y[..., 3] = x[..., 1] + x[..., 3] / 2
    return y


def process(input_data, mask, anchors):
    anchors_for_head = [anchors[m] for m in mask]
    grid_h, grid_w = map(int, input_data.shape[0:2])

    box_confidence = sigmoid(input_data[..., 4])
    box_confidence = np.expand_dims(box_confidence, axis=-1)
    box_class_probs = sigmoid(input_data[..., 5:])
    box_xy = sigmoid(input_data[..., :2]) * 2 - 0.5

    col = np.tile(np.arange(0, grid_w), grid_w).reshape(-1, grid_w)
    row = np.tile(np.arange(0, grid_h).reshape(-1, 1), grid_h)
    col = col.reshape(grid_h, grid_w, 1, 1).repeat(3, axis=-2)
    row = row.reshape(grid_h, grid_w, 1, 1).repeat(3, axis=-2)
    grid = np.concatenate((col, row), axis=-1)
    box_xy += grid
    box_xy *= float(IMG_SIZE) / float(grid_h)

    box_wh = (sigmoid(input_data[..., 2:4]) * 2) ** 2
    box_wh = box_wh * np.array(anchors_for_head, dtype=np.float32)
    return np.concatenate((box_xy, box_wh), axis=-1), box_confidence, box_class_probs


def filter_boxes(boxes, box_confidences, box_class_probs, obj_thresh=None):
    if obj_thresh is None:
        obj_thresh = OBJ_THRESH

    boxes = boxes.reshape(-1, 4)
    box_confidences = box_confidences.reshape(-1)
    box_class_probs = box_class_probs.reshape(-1, box_class_probs.shape[-1])

    obj_keep = np.where(box_confidences >= obj_thresh)
    boxes = boxes[obj_keep]
    box_confidences = box_confidences[obj_keep]
    box_class_probs = box_class_probs[obj_keep]

    if boxes.size == 0:
        return (
            np.empty((0, 4), dtype=np.float32),
            np.empty((0,), dtype=np.int32),
            np.empty((0,), dtype=np.float32),
        )

    class_max_score = np.max(box_class_probs, axis=-1)
    classes = np.argmax(box_class_probs, axis=-1)
    cls_keep = np.where(class_max_score >= obj_thresh)
    boxes = boxes[cls_keep]
    classes = classes[cls_keep]
    scores = (class_max_score * box_confidences)[cls_keep]
    return boxes, classes.astype(np.int32), scores.astype(np.float32)


def nms_boxes(boxes, scores, nms_thresh=None):
    if nms_thresh is None:
        nms_thresh = NMS_THRESH

    x = boxes[:, 0]
    y = boxes[:, 1]
    w = boxes[:, 2] - boxes[:, 0]
    h = boxes[:, 3] - boxes[:, 1]
    areas = w * h
    order = scores.argsort()[::-1]

    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)

        xx1 = np.maximum(x[i], x[order[1:]])
        yy1 = np.maximum(y[i], y[order[1:]])
        xx2 = np.minimum(x[i] + w[i], x[order[1:]] + w[order[1:]])
        yy2 = np.minimum(y[i] + h[i], y[order[1:]] + h[order[1:]])

        w1 = np.maximum(0.0, xx2 - xx1 + 0.00001)
        h1 = np.maximum(0.0, yy2 - yy1 + 0.00001)
        inter = w1 * h1
        ovr = inter / (areas[i] + areas[order[1:]] - inter)
        inds = np.where(ovr <= nms_thresh)[0]
        order = order[inds + 1]
    return np.array(keep)


def yolov5_post_process(input_data, obj_thresh=None, nms_thresh=None):
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
    if boxes.size == 0:
        return None, None, None

    boxes = xywh2xyxy(boxes)
    nboxes, nclasses, nscores = [], [], []
    for cls in set(classes.tolist()):
        inds = np.where(classes == cls)
        b = boxes[inds]
        c = classes[inds]
        s = scores[inds]
        keep = nms_boxes(b, s, nms_thresh)
        nboxes.append(b[keep])
        nclasses.append(c[keep])
        nscores.append(s[keep])

    if not nclasses and not nscores:
        return None, None, None
    return np.concatenate(nboxes), np.concatenate(nclasses), np.concatenate(nscores)


def letterbox(im, new_shape=None, color=(0, 0, 0)):
    if new_shape is None:
        new_shape = (IMG_SIZE, IMG_SIZE)
    if isinstance(new_shape, int):
        new_shape = (new_shape, new_shape)

    h, w = im.shape[:2]
    target_h, target_w = new_shape
    ratio = min(target_w / w, target_h / h)
    new_w = int(round(w * ratio))
    new_h = int(round(h * ratio))
    dw = (target_w - new_w) / 2.0
    dh = (target_h - new_h) / 2.0

    if (w, h) != (new_w, new_h):
        im = cv2.resize(im, (new_w, new_h), interpolation=cv2.INTER_LINEAR)
    top, bottom = int(round(dh - 0.1)), int(round(dh + 0.1))
    left, right = int(round(dw - 0.1)), int(round(dw + 0.1))
    im = cv2.copyMakeBorder(im, top, bottom, left, right, cv2.BORDER_CONSTANT, value=color)
    return im, ratio, (dw, dh)
