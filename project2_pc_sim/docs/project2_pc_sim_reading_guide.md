# project2_pc_sim 阅读入口

## 0. 快速索引 / 阅读地图

先按这个顺序读：

1. `README.md`
2. `docs/pc_sim_architecture.md`
3. `docs/toolchain_environment.txt`
4. `docs/project2_pc_sim_function_index.md`
5. `docs/ev1527_truth_mapping.md`

如果你只想先抓边界，先记住四件事：

- `project2_pc_sim` 只保留两条链：RF WAV 回放链、WSL -> Windows 视觉桥接链。
- 它不是板端运行时，也不是验收真值。
- `BUILD_QT5_GUI=ON` 时缺 Qt 依赖会直接在 CMake 配置期失败。
- GUI 里的 waveform 是按解码后的 EV1527 原始码合成出来的展示面，不是原始 pulse 验证面。

## 1. 契约 / 职责

`project2_pc_sim` 的职责只有两类：

1. 在 Windows 侧把 WAV -> candidate frames -> timed replay -> `rf_gateway` 这条 RF 模拟链串起来。
2. 在 WSL 侧把视频源和模型跑起来，再通过 TCP bridge 把视觉状态送到 Windows Qt 页面。

它明确不负责：

- 代替 `project2_master` 的板端运行时
- 提供直接 Qt 本地视频播放路径
- 充当真实 pulse 捕获或 pulse 验证界面

如果 `pc_sim` 和 `project2_master` 对同一件事情给出不同结论，以 `project2_master` 为准。

## 2. 生命周期

### 2.1 构建阶段

- 顶层 `CMakeLists.txt` 负责切分 `linux_app` 和 `qt_gui`
- `qt_gui/CMakeLists.txt` 在 `BUILD_QT5_GUI=ON` 时要求 `Qt5::Core/Gui/Widgets/Network`
- 缺依赖时现在直接失败，不再静默跳过 `rf_dashboard_qt5`

### 2.2 RF 回放阶段

```text
Qt
-> python/wav_to_pulses.py
-> pulse_runtime.json (+ pulse_runtime.txt)
-> Qt preload frames[*].candidate_idx / candidate_wav_sec
-> python/replay_pulse_timeline.py stdout AA55
-> rf_gateway --rf-input - stdin
-> rf_gateway stdout JSON lines
-> Qt seq -> wav_sec backfill
-> RF Status / System Log
```

关键点：

- `linux_app/main.c` 现在不再对某个固定码做偏置优先
- `rf_gateway` 仍只接受 `--rf-input -`，所以不会破坏 Qt 现有 RF CLI 串接方式
- 当前 replay 链已经没有 `FRAME_TS` / `FRAME_META` sideband，Qt 依赖的是预读的 `pulse_runtime.json` 和 gateway stdout JSON lines
- Qt 生成的 RF replay report 只是本地日志，不是真实 MQTT 发布

### 2.3 Vision 阶段

```text
WSL source/model
-> python/wsl_vision_bridge_server.py --source <wsl_source>
-> TCP bridge
-> Qt Vision page
```

关键点：

- Qt 只消费 `--vision-host` / `--vision-port` / `--disable-vision`
- 默认不传时会按 `127.0.0.1:17655` 尝试连接 bridge
- `--vision-port` 必须是 `1..65535` 的整数
- `--disable-vision` 才是明确禁用 bridge 的边界
- `--disable-vision` 不能和 `--vision-host` / `--vision-port` 混用

## 3. 边界条件 / ABI

### 3.1 CLI 与启动约束

- `rf_gateway`：`--rf-input -` 是唯一受支持的输入口径
- Qt：`--wav-speed` 必须大于 `0`
- Qt：`--vision-port` 非法时直接启动失败，不再把 `0` 当作软禁用
- WSL bridge：`--source` 必填

### 3.2 文档与命名约束

- Python 依赖按边界拆成 `requirements-rf.txt` 和 `requirements-vision.txt`
- `requirements-rf.txt` 对应 RF-only helper，保持零第三方依赖口径
- `requirements-vision.txt` 对应 WSL 视觉桥接路径；`requirements.txt` 只保留为兼容别名
- 窗口标题和 System Log 口径现在都以 `project2_pc_sim` / replay / bridge 为主，不再沿用真机残留命名

### 3.3 展示边界

- `RF Status` 里的 waveform 由 `qt_gui/core/rf_utils.cpp` 根据解码地址反推
- 它适合做“当前解码形状的辅助可视化”
- 它不适合被当作“真实 pulse 是否正确”的验收面

## 4. 最后一眼只看代码

核心入口按“从外到内”的顺序是：

- Qt 启动与参数解析：`qt_gui/app/main.cpp`
- 主窗口与页面装配：`qt_gui/app/main_window.cpp`
- RF 回放编排：`qt_gui/rf/rf_gateway_client.cpp`
- Vision bridge 客户端：`qt_gui/vision/vision_page.cpp`
- RF 聚合与发布：`linux_app/main.c`
- WSL 视觉桥接：`python/wsl_vision_bridge_server.py`

继续往下跳代码时，直接配合 `docs/project2_pc_sim_function_index.md` 使用。
