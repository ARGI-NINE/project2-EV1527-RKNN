"""
Vision package for RK3568 RKNN-based object detection pipeline.

Provides a 3-thread pipeline (capture, inference, post-process)
and YOLOv5 post-processing utilities for real hardware deployment.
"""

from .rknn_pipeline import VisionPipeline, VisionPipelineState
from .yolov5_postprocess import yolov5_post_process, draw, CLASSES, IMG_SIZE

__all__ = [
    "VisionPipeline",
    "VisionPipelineState",
    "yolov5_post_process",
    "draw",
    "CLASSES",
    "IMG_SIZE",
]
