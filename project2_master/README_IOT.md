# RF433 IoT Gateway (master)

## Current Facts

- This document describes the real board-side IoT path in `project2_master`.
- `project2_pc_sim` is still an offline simulation/regression workspace. It is not the board-side runtime entry.
- The master RF path remains:
  `UART -> serdev -> /dev/rf433 -> rf_gateway -> qt_gui`
- RF online state still comes from the driver `online` bit and real RF frames, not from process startup alone.
- The master vision path is no longer a placeholder page.
- The master vision code path is now local direct integration:
  `/dev/video9 -> local VisionRuntime -> RKNN`
- The RKNN/RGA/model assets needed by that path are now vendored inside
  `project2_master/third_party/rknn_yolov5_rk3568/`.
- The Qt frontend consumes the existing `VisionSnapshot` for real frame, detections, FPS, and runtime status.
- Local video files are still not a supported primary path for master vision input.
- Hardware acceptance has not been completed yet. Treat this as code-path integration, not as full board-side signoff.

## Scope

`project2_master` is responsible for:

1. Receiving real RF data from `/dev/rf433`
2. Decoding and publishing RF results
3. Showing RF and vision runtime state in the Qt dashboard

The vision path used by master is the board-side local direct path. The split simulation path remains in `project2_pc_sim` only.

## Boundary With pc_sim

| Workspace | Role | Path Type |
|---|---|---|
| `project2_master` | Real board-side runtime | Direct local RF + direct local vision |
| `project2_pc_sim` | Offline simulation/regression | Split simulated path |

## Validation Boundary

- Confirmed at code/document level: RF input path, RF online semantics, master local vision code path, and pc_sim offline boundary.
- Not completed: end-to-end hardware acceptance on the real board.
- Do not describe the current state as completed real-machine E2E validation.
