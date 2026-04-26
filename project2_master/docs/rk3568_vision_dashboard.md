# RK3568 Vision Dashboard (master)

## Current Code Path

- `project2_master` now uses a board-side local `VisionRuntime` inside the Qt process.
- The runtime reuses a vendored RK3568 stack under
  `project2_master/third_party/rknn_yolov5_rk3568/`:
  `rkYolov5s`, `preprocess`, `postprocess`, `v4l2_capture`,
  bundled RKNN/RGA headers/libs, and the local model assets.
- The runtime does not use TCP, bridge transport, `pc_sim`, `drm_display`, `mpp_decoder`, or an MP4 primary path.
- The default real camera input is `/dev/video9`.
- The Qt page still consumes the existing `VisionSnapshot`; the integration point stays at `DashboardBackend::updateVisionState()`.

## Runtime Contract

- RF remains on the existing real path:
  `UART -> serdev -> /dev/rf433 -> rf_gateway -> qt_gui`
- Vision now runs on the existing local board path:
  `/dev/video9 -> V4L2Capture -> RGA preprocess -> RKNN infer -> postprocess -> VisionSnapshot -> Qt UI`
- Only Linux `/dev/video*` devices are accepted for vision input.
- Local video files are still not a supported primary input path.

## UI Behaviour

- `qt_gui/vision/vision_page.cpp` shows the latest real frame, detection list, FPS, and runtime status from `VisionSnapshot`.
- `qt_gui/log/system_log_page.cpp` and the main status bar continue to read the same backend snapshot.
- `VisionPage` is no longer used as an integration entry point.

## Build Notes

- `qt_gui/CMakeLists.txt` removes the unfinished bridge/network dependency.
- On Linux, the Qt target conditionally adds the vendored RKNN/RGA/V4L2 sources and links the vendored RKNN/RGA runtime libraries.
- On non-Linux hosts, the Qt build falls back to a stub status path so configure-time checks can still run.

## Validation Boundary

- This change only wires the local direct vision code path into `project2_master`.
- Hardware acceptance on the real RK3568 board has not been completed.
- Treat the current state as code integration plus minimal build/static validation, not as full board-side signoff.
