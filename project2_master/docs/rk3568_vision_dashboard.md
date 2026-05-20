# RK3568 Vision Dashboard (master)

## Current Code Path

- `project2_master` now uses a board-side local `VisionRuntime` inside the Qt process.
- The runtime reuses a vendored RK3568 stack under
  `project2_master/third_party/rknn_yolov5_rk3568/`:
  `rkYolov5s`, `preprocess`, `postprocess`, `v4l2_capture`,
  `mpp_decoder`, `mpp_encoder_rtsp`, bundled RKNN/RGA/MPP headers/libs,
  FFmpeg integration, and the local model assets.
- The runtime does not use TCP transport, bridge transport, `pc_sim`, or `drm_display`.
- The default real camera input is `/dev/video9`; `--vision-device` also accepts a readable local video file, which still runs through the same board-side runtime rather than any stub path.
- The Qt page still consumes the existing `VisionSnapshot`; the integration point stays at `DashboardBackend::updateVisionState()`.

## Runtime Contract

- RF remains on the existing real path:
  `UART -> serdev -> /dev/rf433 -> rf_gateway(JSON envelope stdout) -> qt_gui`
- Vision runs on one local board runtime with two real input branches:
  - `/dev/video9 -> V4L2Capture -> sourceThread`
  - `local MP4/video file -> MppDecoder -> sourceThread`
- `sourceThread` performs the real dual fan-out for every frame:
  - `copyFrameToAiPool()` -> RKNN infer -> `VisionSnapshot` / detection MQTT
  - `copyFrameToStreamPool()` -> `streamThread` -> `MppRtspEncoder` -> RTSP push
- The MP4 path is not a fake fallback. It is a first-class local decode branch used to feed the same AI and RTSP outputs as the camera branch.

## UI Behaviour

- `qt_gui/vision/vision_page.cpp` shows the latest real frame, detection list, FPS, and runtime status from `VisionSnapshot`.
- `qt_gui/log/system_log_page.cpp` and the main status bar continue to read the same backend snapshot.
- `VisionPage` is no longer used as an integration entry point.

## Build Notes

- `qt_gui/CMakeLists.txt` pulls in the local runtime sources, including `mpp_decoder.cc` and `mpp_encoder_rtsp.cc`.
- The Qt target requires the vendored RKNN/RGA/MPP runtime libraries plus FFmpeg and `libmosquitto`; missing pieces fail the build instead of switching to a supported stub runtime.
- `VisionRuntime` still contains an unsupported-configuration error branch for builds that somehow lack `DASHBOARD_HAVE_LOCAL_VISION_RUNTIME`, but that branch is an explicit failure state, not an acceptable default path.

## Validation Boundary

- This document reflects the current code structure only.
- The contract to preserve is: real local runtime, optional local video-file decode branch, and source-side dual fan-out into AI plus RTSP.
