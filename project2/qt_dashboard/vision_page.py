"""Vision Detection Page for AIoT Dashboard."""
import numpy as np
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QGroupBox,
    QListWidget, QListWidgetItem, QFrame
)
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QImage, QPixmap, QFont, QColor


class VisionPage(QWidget):
    """Page 2: Vision Detection"""
    
    COCO_CLASSES = [
        "person", "bicycle", "car", "motorbike", "aeroplane", "bus", "train", "truck",
        "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
        "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra",
        "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
        "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove",
        "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup",
        "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
        "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "sofa",
        "pottedplant", "bed", "diningtable", "toilet", "tvmonitor", "laptop", "mouse",
        "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
        "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier",
        "toothbrush",
    ]
    
    def __init__(self, backend, parent=None):
        super().__init__(parent)
        self.backend = backend
        self._setup_ui()
        
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._refresh)
        self._timer.start(33)  # ~30fps refresh
        
    def _setup_ui(self):
        layout = QHBoxLayout(self)
        
        # Left: Video display
        video_group = QGroupBox("摄像头视频")
        video_layout = QVBoxLayout(video_group)
        self._video_label = QLabel("等待摄像头...")
        self._video_label.setAlignment(Qt.AlignCenter)
        self._video_label.setMinimumSize(640, 480)
        self._video_label.setStyleSheet("background-color: #1a1a1a; color: #888;")
        video_layout.addWidget(self._video_label)
        layout.addWidget(video_group, stretch=3)
        
        # Right: Info panel
        info_layout = QVBoxLayout()
        
        # Status
        status_group = QGroupBox("状态")
        status_layout = QVBoxLayout(status_group)
        self._fps_label = QLabel("FPS: 0.0")
        self._fps_label.setFont(QFont("Consolas", 24, QFont.Bold))
        self._fps_label.setAlignment(Qt.AlignCenter)
        self._camera_status = QLabel("摄像头: 离线")
        self._model_status = QLabel("模型: 未加载")
        self._frame_count_label = QLabel("总帧数: 0")
        for lbl in [self._fps_label, self._camera_status, self._model_status, self._frame_count_label]:
            status_layout.addWidget(lbl)
        info_layout.addWidget(status_group)
        
        # Detections
        det_group = QGroupBox("检测结果")
        det_layout = QVBoxLayout(det_group)
        self._det_list = QListWidget()
        self._det_list.setFont(QFont("Consolas", 10))
        det_layout.addWidget(self._det_list)
        info_layout.addWidget(det_group, stretch=1)
        
        layout.addLayout(info_layout, stretch=1)
        
    def _refresh(self):
        state = self.backend.get_vision_state()
        
        # FPS
        fps = state.get('fps', 0.0)
        self._fps_label.setText(f"FPS: {fps:.1f}")
        
        # Status
        cam = state.get('camera_online', False)
        model = state.get('model_loaded', False)
        self._camera_status.setText(f"摄像头: {'在线' if cam else '离线'}")
        self._camera_status.setStyleSheet(f"color: {'green' if cam else 'red'};")
        self._model_status.setText(f"模型: {'已加载' if model else '未加载'}")
        self._model_status.setStyleSheet(f"color: {'green' if model else 'red'};")
        self._frame_count_label.setText(f"总帧数: {state.get('frame_count', 0)}")
        
        # Video frame
        frame = state.get('frame')
        if frame is not None and isinstance(frame, np.ndarray):
            # Draw detection boxes on frame
            display = frame.copy()
            boxes = state.get('boxes')
            classes = state.get('classes')
            scores = state.get('scores')
            if boxes is not None and len(boxes) > 0:
                try:
                    import cv2
                    for box, cls_id, score in zip(boxes, classes, scores):
                        x1, y1, x2, y2 = map(int, box)
                        cls_name = self.COCO_CLASSES[int(cls_id)] if int(cls_id) < len(self.COCO_CLASSES) else str(cls_id)
                        cv2.rectangle(display, (x1, y1), (x2, y2), (0, 255, 0), 2)
                        cv2.putText(display, f"{cls_name} {score:.2f}", (x1, y1-5),
                                   cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)
                except ImportError:
                    pass
            
            # Convert to QPixmap
            if len(display.shape) == 3:
                h, w, ch = display.shape
                if ch == 3:
                    # Assume BGR from cv2
                    try:
                        import cv2
                        display = cv2.cvtColor(display, cv2.COLOR_BGR2RGB)
                    except ImportError:
                        pass
                    qimg = QImage(display.data, w, h, w * 3, QImage.Format_RGB888)
                else:
                    qimg = QImage(display.data, w, h, w * ch, QImage.Format_RGBA8888)
            else:
                h, w = display.shape
                qimg = QImage(display.data, w, h, w, QImage.Format_Grayscale8)
            
            pixmap = QPixmap.fromImage(qimg)
            scaled = pixmap.scaled(self._video_label.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation)
            self._video_label.setPixmap(scaled)
        
        # Detection list
        boxes = state.get('boxes')
        classes = state.get('classes')
        scores = state.get('scores')
        self._det_list.clear()
        if boxes is not None and classes is not None and scores is not None:
            for i, (cls_id, score) in enumerate(zip(classes, scores)):
                cls_name = self.COCO_CLASSES[int(cls_id)] if int(cls_id) < len(self.COCO_CLASSES) else f"class_{cls_id}"
                self._det_list.addItem(f"{cls_name}: {score:.2f}")
        if self._det_list.count() == 0:
            self._det_list.addItem("无检测目标")
