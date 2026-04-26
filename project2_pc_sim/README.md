# project2_pc_sim

`project2_pc_sim` is an offline PC-side simulator. It is not the board runtime and it is not the acceptance source of truth.

## Surviving Chains

Only these two chains are in scope.

Detailed toolchain inventory: `docs/toolchain_environment.txt`

### RF chain

```text
WAV
-> python/wav_to_pulses.py
-> candidate-frame extraction
-> python/replay_pulse_timeline.py
-> simulated lower-machine to upper-machine AA55 stream
-> linux_app/rf_gateway
-> Qt RF page analysis and display
```

### Vision chain

```text
video source resolved inside WSL
-> python/wsl_vision_bridge_server.py --source <wsl_source>
-> TCP bridge
-> Windows Qt Vision page display
```

Qt does not open any local video file or local camera path. The Windows side only connects to the WSL bridge through `--vision-host` and `--vision-port`.

## Qt Frontend

The retained Windows Qt frontend has three pages:

- `RF Status`
- `Vision`
- `System Log`

These pages are display surfaces over the retained RF replay chain and WSL bridge vision chain. The `System Log` page is restored UI, not a third runtime chain.

## Normal Build And Launch

The normal user-facing build directory is `build`.

Validated configure/build flow on this machine:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' `
  -S D:\project\repos\project2\project2_pc_sim `
  -B D:\project\repos\project2\project2_pc_sim\build `
  -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=D:/qt/Tools/mingw730_64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=D:/qt/Tools/mingw730_64/bin/g++.exe `
  -DBUILD_LINUX_APP=ON `
  -DBUILD_QT5_GUI=ON `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=D:/qt/5.12.9/mingw73_64

& 'C:\Program Files\CMake\bin\cmake.exe' --build D:\project\repos\project2\project2_pc_sim\build --config Release --parallel 4 --target rf_gateway rf_dashboard_qt5
```

Before launching `build\qt_gui\rf_dashboard_qt5.exe`, set the Qt and MinGW runtime DLL paths:

```powershell
$env:PATH = 'D:\qt\5.12.9\mingw73_64\bin;D:\qt\Tools\mingw730_64\bin;' + $env:PATH
```

Current WSL bridge behavior:

- The actual vision input is resolved on the WSL side by `python/wsl_vision_bridge_server.py`.
- `--source` may be an explicit WSL/translated path or a camera index string.
- `--source <wsl_source>` is required for the retained simulator vision path.

## Explicit Non-Goals

- No direct Qt-side MP4/local-video vision path.
- No standalone timing analysis or profiling outputs kept as part of the documented flow. RF replay metadata such as Qt-visible `wav_sec` remains part of the retained RF chain.
- No verification artifact bundles retained as part of the documented simulator flow.

## Relationship With master

| Area | `project2_master` | `project2_pc_sim` |
|---|---|---|
| RF | Real `/dev/rf433` runtime path | WAV replay simulation path |
| Vision | Board-local `/dev/video9 + VisionRuntime + RKNN` | WSL processing plus bridge display path |
| Acceptance | Primary | Secondary reference only |

If `pc_sim` and `master` disagree, `project2_master` wins for acceptance.
