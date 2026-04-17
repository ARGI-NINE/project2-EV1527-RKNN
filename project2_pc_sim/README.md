# project2_pc_sim

`project2_pc_sim` is the PC-side integration workspace for `project2`.
It is the Windows + WSL validation bench for the current dual-chain runtime, not the final board firmware project.

## Runtime Scope

- RF433 chain: `capture03.wav` -> `python/wav_to_pulses.py` -> candidate-frame JSON -> `python/replay_pulse_timeline.py` -> `build_live/linux_app/rf_gateway.exe` -> Qt RF page
- Vision chain: `test.mp4` -> `python/wsl_vision_bridge_server.py` -> `vision/rknn_pipeline.py` -> Qt Vision page

## Alignment Matrix (master / pc_sim / hardware)

| Dimension | project2_master | project2_pc_sim (this repo) | project2_hardware |
|---|---|---|---|
| RF input source | `/dev/ttyS9` realtime UART only | WAV + replay pipeline | RF pulse capture on STM32 |
| Simulation path | Not supported | Supported | Not applicable |
| Bypass input (`/dev/rf433`, stdin, pipes) | Blocked in master | Not applicable | Not applicable |
| Role | Board-side runtime | PC validation bench | Lower-board firmware |

## Current Status

- This workspace remains the simulation/integration bench and is unchanged by master-side serial hardening.
- `project2_master` no longer accepts simulated RF input paths; use this repo for WAV/stage-1 replay validation.
- Hardware truth source remains STM32 UART frames and `ev1527_decode.py` mapping rules.
- `project2_pc_sim` must not be used to infer `project2_master` realtime serial behavior; master serial validation belongs to `project2_master`.

## Canonical RF CLI & Metrics (pc_sim)

- Stage-1 extractor command shape: `python/wav_to_pulses.py --wav <wav_path> --max-frames 0 --out-txt <pulse_txt> --out-json <pulse_json>`
- Replay command shape: `python/replay_pulse_timeline.py --pulse-json <pulse_json> --speed <speed> [--loop]`
- Qt fixed RF entry flag: `--wav-input` (dashboard side), not `--input`/`--output`.
- `stage1_candidate_frames`: `frames=<N>` from `wav_to_pulses.py` (`N=2982` on full `capture03.wav` baseline).
- `gateway_seq`: `seq=<n>` in `[RF]` line from `rf_gateway.exe` (e.g. `2612`), a decode-time sequence index inside the replay stream.
- Historical counts from older artifacts (such as `786`/`1154`) are not the current baseline and must not be mixed with `stage1_candidate_frames`.

## Hard Constraints

- `ev1527_decode.py` is the RF truth reference.
- RF Stage-1 candidate-frame preprocessing is mandatory and stays separate from backend decode and repeat judgment.
- Final RF validation uses full-duration `capture03.wav`.
- Vision must not fabricate detections.
- If RKNN runtime is unavailable, the bridge must report an explicit error and emit zero detections.

## Main Directories

- `common/`: shared RF protocol helpers
- `linux_app/`: `rf_gateway` backend and RF decode logic
- `qt_gui/`: Qt5 frontend pages, widgets, backend state, and app shell
- `python/`: RF preprocessing, replay, decode bridge, and Vision bridge entrypoint
- `vision/`: RKNN/ONNX pipeline and YOLOv5 postprocess
- `docs/`: maintained runtime, rule, architecture, and audit documents
- `sim_data/`: retained evidence artifacts and RF intermediate outputs
- `build_live/`: active generated build tree used by the validated workflow
- `.venv/`: project-local Python environment referenced by the runtime baseline

## Toolchain Baseline

- `C:\Program Files\CMake\bin\cmake.exe`
- `D:\qt\5.12.9\mingw73_64\bin\qmake.exe`
- `D:\qt\5.12.9\mingw73_64\bin\windeployqt.exe`
- `D:\qt\Tools\mingw730_64\bin\mingw32-make.exe`
- `D:\qt\Tools\mingw730_64\bin\gcc.exe`
- `D:\qt\Tools\mingw730_64\bin\g++.exe`
- `D:\project\repos\project2\project2_pc_sim\.venv\Scripts\python.exe`
- `C:\Windows\System32\wsl.exe`
- WSL RKNN Python: `/home/mctupubuser/miniconda3/envs/rknn/bin/python`

Runtime inputs:

- RF WAV: `D:\project\repos\project2\capture03.wav`
- Vision video: `D:\project\repos\project2\test.mp4`
- Model: `D:\project\repos\project2\yolov5s.onnx`

## Build

```powershell
cmake -S . -B build_live -G "MinGW Makefiles" -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=ON
cmake --build .\build_live --target rf_gateway rf_dashboard_qt5 -j 8
D:\qt\5.12.9\mingw73_64\bin\windeployqt.exe --release --compiler-runtime .\build_live\qt_gui\rf_dashboard_qt5.exe
```

## Start The WSL Vision Bridge

```bash
cd /mnt/d/project/repos/project2/project2_pc_sim
/home/mctupubuser/miniconda3/envs/rknn/bin/python python/wsl_vision_bridge_server.py \
  --host 0.0.0.0 \
  --port 17655 \
  --source /mnt/d/project/repos/project2/test.mp4 \
  --model /mnt/d/project/repos/project2/yolov5s.onnx \
  --log-level INFO
```

On this machine, Windows Qt may need the WSL IPv4 address from `wsl.exe hostname -I` instead of `127.0.0.1`.

## Run The Integrated Dashboard

```powershell
.\build_live\qt_gui\rf_dashboard_qt5.exe `
  --gateway .\build_live\linux_app\rf_gateway.exe `
  --wav-input D:/project/repos/project2/capture03.wav `
  --video-input D:/project/repos/project2/test.mp4 `
  --python-bin .\.venv\Scripts\python.exe `
  --vision-host <WSL_IPV4> `
  --vision-port 17655
```

## Documentation Map

Entry docs:

- [README.md](README.md)
- [docs/pc_sim_architecture.md](docs/pc_sim_architecture.md)
- [docs/ev1527_truth_mapping.md](docs/ev1527_truth_mapping.md)

Runtime baselines:

- [docs/toolchain_environment_record_20260407.txt](docs/toolchain_environment_record_20260407.txt)
- [docs/stage1_test_commands.md](docs/stage1_test_commands.md)

Design references:

- [docs/rknn_c_accel_design_from_article.txt](docs/rknn_c_accel_design_from_article.txt)

Acceptance records:

- [docs/rf_chain_retest_full.txt](docs/rf_chain_retest_full.txt)
- [docs/vision_chain_retest_full.txt](docs/vision_chain_retest_full.txt)
- [docs/dual_chain_retest_final.txt](docs/dual_chain_retest_final.txt)
- [docs/final_acceptance_audit.txt](docs/final_acceptance_audit.txt)
