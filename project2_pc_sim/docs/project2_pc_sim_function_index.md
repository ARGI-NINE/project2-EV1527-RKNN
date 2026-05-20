# project2_pc_sim 函数与模块索引

## 0. 快速索引 / 阅读地图

| 模块 | 作用 | 入口文件 | 先看哪些函数 |
| --- | --- | --- | --- |
| 顶层构建 | 切分 `linux_app` / `qt_gui` | `CMakeLists.txt`, `qt_gui/CMakeLists.txt` | `find_package(Qt5 ...)` 配置分支 |
| RF 用户态聚合 | 从 stdin 回放流里解码、稳定化，并输出 stdout JSON lines | `linux_app/main.c` | `main`, `on_rf_frame`, `stable_group_find`, `stable_group_update` |
| Qt 启动 | 解析 CLI 并创建主窗口 | `qt_gui/app/main.cpp` | `main`, `failStartup` |
| Qt 主窗口 | 装配 RF / Vision / Log 三页 | `qt_gui/app/main_window.cpp` | `setupUi`, `setupRuntime`, `updateStatusBar` |
| Qt RF 编排 | 起 Python 预处理、预读 `pulse_runtime.json`、起 replay / `rf_gateway`、解析 gateway stdout JSON lines | `qt_gui/rf/rf_gateway_client.cpp` | `start`, `onPrepFinished`, `startGatewayWithRealtimeInput`, `handleGatewayLine`, `parseGatewayEventLine` |
| Qt Vision 页面 | 连接 bridge、解析 JSON、刷新页面 | `qt_gui/vision/vision_page.cpp` | `setupVisionBridge`, `attemptVisionBridgeConnect`, `applyVisionBridgePayload`, `refresh` |
| RF 波形辅助 | 从解码地址生成合成 waveform | `qt_gui/core/rf_utils.cpp` | `parseRawCode`, `buildWaveformFromRawCode` |
| RF 预处理 | 从 WAV 提取 candidate frames | `python/wav_to_pulses.py` | `extract_frames`, `write_outputs`, `main` |
| RF 定时回放 | 按时间线输出 AA55 帧流 | `python/replay_pulse_timeline.py` | `_load_frames`, `_encode_frame`, `main` |
| WSL 视觉桥接 | 跑模型并推送 JSON snapshot | `python/wsl_vision_bridge_server.py` | `_resolve_source`, `_resolve_model_path`, `_build_payload`, `main` |

## 1. 契约 / 职责

### 1.1 `linux_app`

- `main.c` 只做 RF 回放流的用户态消费与输出
- 输入口径固定为 stdin replay stream
- 当前稳定化逻辑保留“近码分组 + 最佳置信度选码”，不再保留“偏向某个固定码”的覆盖路径

### 1.2 `qt_gui`

- `app/main.cpp` 负责 CLI strictness
- `app/main_window.cpp` 负责窗口和页签装配
- `rf/rf_gateway_client.cpp` 负责真正串起 RF 回放链
- `vision/vision_page.cpp` 负责 bridge client，而不是本地视频读取
- `log/system_log_page.cpp` 负责把 RF / Vision / System 日志整理成展示面

### 1.3 `python`

- `wav_to_pulses.py`：WAV -> candidate frames
- `replay_pulse_timeline.py`：candidate frames -> timed AA55 stdout
- `wsl_vision_bridge_server.py`：WSL 模型运行时 -> newline-delimited JSON snapshots

## 2. 生命周期

### 2.1 Qt 启动链

1. `qt_gui/app/main.cpp::main`
2. 解析 `--gateway` / `--wav-input` / `--wav-speed` / `--vision-host` / `--vision-port` / `--disable-vision`
3. 参数非法时走 `failStartup`
4. 构造 `MainWindow`
5. `MainWindow::setupRuntime` 拉起 RF 客户端

### 2.2 RF 回放链

1. `RFGatewayClient::start`
2. `prepareRealtimeTimeline`
3. `python/wav_to_pulses.py`
4. `RFGatewayClient::onPrepFinished` 预读 `pulse_runtime.json` 并建立 `replayWavSecByIdx_`
5. `startGatewayWithRealtimeInput`
6. `python/replay_pulse_timeline.py`
7. `linux_app/main.c::on_rf_frame`
8. `RFGatewayClient::handleGatewayLine` / `parseGatewayEventLine`
9. `DashboardBackend::addRFEvent` / `addLog`

### 2.3 Vision bridge 链

1. `VisionPage::setupVisionBridge`
2. `attemptVisionBridgeConnect`
3. `onVisionBridgeReadyRead`
4. `applyVisionBridgePayload`
5. `refresh`

## 3. 边界条件 / ABI

### 3.1 C/C++ 侧

- `linux_app/main.c::main`
  - `--rf-input` 仅支持 `-`
  - `--stable-repeat`, `--stable-window`, `--stable-near-bits`, `--min-publish-confidence`, `--publish-gap` 仍保留
- `linux_app/main.c::on_rf_frame`
  - 通过 stdout 输出一行一个 JSON 事件，字段包含 `addr`, `key`, `conf`, `src`, `pulses`, `seq`
- `qt_gui/app/main.cpp::main`
  - `--wav-speed > 0`
  - 默认会连接 `127.0.0.1:17655`，除非显式传 `--disable-vision`
  - `--vision-port` 必须是 `1..65535`
  - `--disable-vision` 不能和 `--vision-host` / `--vision-port` 混用

### 3.2 Python 侧

- `wav_to_pulses.py`
  - 产物：`pulse_runtime.txt`, `pulse_runtime.json`
  - `pulse_runtime.json` 的 `frames[*]` 至少包含 `candidate_idx`, `candidate_wav_sec`, `pulse`
- `replay_pulse_timeline.py`
  - 输入：`--pulse-json`
  - 输出：AA55 binary stream 到 stdout
  - 不再输出 `FRAME_TS` / `FRAME_META` sideband
- `wsl_vision_bridge_server.py`
  - 输入：`--source`, `--model`, `--host`, `--port`
  - 输出：一行一个 JSON payload

### 3.3 展示边界

- `qt_gui/core/rf_utils.cpp::buildWaveformFromRawCode` 生成的是合成形状
- `qt_gui/rf/rf_status_page.cpp` 在历史点击时优先显示合成形状
- 因此 GUI 里现在没有“真实 pulse 逐样本核对”这条展示面

## 4. 最后一眼只看代码

### 4.1 `linux_app/main.c`

- `print_usage`: RF CLI 帮助
- `stable_groups_decay`: 按窗口淘汰旧分组
- `stable_group_find`: 找最近的 24-bit Hamming 分组
- `stable_group_alloc`: 分配或复用分组槽
- `stable_group_seed`: 新分组初始化
- `stable_group_update`: 更新最佳码和最佳置信度
- `on_rf_frame`: 解码、过滤、去重、输出最终 RF JSON 行
- `main`: 参数解析、打开输入、启动 epoll

### 4.2 `qt_gui/app/main.cpp`

- `failStartup`: CLI strictness 失败时给出明确错误
- `main`: 解析参数、构造 `AppOptions`、启动 Qt 主窗口

### 4.3 `qt_gui/app/main_window.cpp`

- `setupUi`: 页签与状态栏
- `setupRuntime`: 启动后端日志和 RF 客户端
- `updateStatusBar`: 汇总 RF/Vision/SystemStats

### 4.4 `qt_gui/rf/rf_gateway_client.cpp`

- `start`: RF 链总入口
- `prepareRealtimeTimeline`: 预处理 WAV 并生成 `pulse_runtime.txt` / `pulse_runtime.json`
- `onPrepFinished`: 预读 `pulse_runtime.json`，把 `candidate_idx/idx` 映射到 `candidate_wav_sec/wav_sec`
- `startGatewayWithRealtimeInput`: 启动 `rf_gateway --rf-input - --stable-repeat 1 --min-publish-confidence 0.92` 与 replay 子进程
- `handleGatewayLine`: 解析 gateway stdout JSON lines，按 `seq` 回填 `wav_sec`，写本地 replay report 日志
- `parseGatewayEventLine`: 解析 `addr` / `key` / `conf` / `src` / `seq` / `wav_sec`
- `computeFirstRfTimeoutMs`: 推导首包等待时间

### 4.5 `qt_gui/vision/vision_page.cpp`

- `setupVisionBridge`: bridge 是否启用的分界点
- `attemptVisionBridgeConnect`: 重连逻辑
- `handleVisionBridgeLine`: 每行 JSON 的入口
- `applyVisionBridgePayload`: 反序列化 frame/detections/status
- `refresh`: 把 bridge 状态投影到页面

### 4.6 `python`

- `wav_to_pulses.py`
  - `extract_frames`
  - `write_outputs`
  - `main`
- `replay_pulse_timeline.py`
  - `_load_frames`
  - `_encode_frame`
  - `main`
- `wsl_vision_bridge_server.py`
  - `_resolve_source`
  - `_resolve_model_path`
  - `_build_payload`
  - `main`
