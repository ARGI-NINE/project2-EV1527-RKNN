## Vendored RK3568 Vision Runtime Assets

This directory vendors the subset of `rknn_yolov5_rk3568` required by
`project2_master/qt_gui` for the local direct `VisionRuntime` path.

Included:

- `include/`: `rkYolov5s`, `preprocess`, `postprocess`, `v4l2_capture`
- `src/`: matching C++ implementation files used by `qt_gui/CMakeLists.txt`
- `3rdparty/rknn/`: RKNN runtime headers and `librknnrt.so`
- `3rdparty/rga/`: RGA headers and `librga.so`
- `model/`: local label file plus the YOLOv5 RK3568 model variants used by runtime fallback lookup

Excluded on purpose:

- upstream `.git/`
- docs and scripts not used by `project2_master`
- `drm_display`, `mpp_decoder`, `main.cc`, and video samples not used by the Qt direct path

If the upstream runtime is updated, refresh this vendored subset together
with `project2_master/qt_gui/CMakeLists.txt` and `vision_runtime.cpp`.
