"""
Vision package for RK3568 RKNN-based object detection pipeline.

Provides a 3-thread pipeline (capture, inference, post-process),
YOLOv5 post-processing utilities, and mock classes for hardware-free
development and testing.
"""

from .rknn_pipeline import VisionPipeline, VisionPipelineState
from .yolov5_postprocess import yolov5_post_process, draw, CLASSES, IMG_SIZE
from .mock_camera import MockCamera, MockRKNNLite

__all__ = [
    "VisionPipeline",
    "VisionPipelineState",
    "yolov5_post_process",
    "draw",
    "CLASSES",
    "IMG_SIZE",
    "MockCamera",
    "MockRKNNLite",
]
