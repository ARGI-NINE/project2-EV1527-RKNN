# project2_pc_sim

`project2_pc_sim` 是一个 PC 侧离线模拟与阅读工程。它保留了两条可运行链路，方便在 Windows + WSL 环境里做本地验证和代码阅读：

1. RF WAV 回放链
2. WSL vision bridge 链

它不是板端 runtime，也不是 `project2_master` 的验收替身。

## 1. 先记住它是什么，不是什么

### 它是什么

- 一个把 `WAV -> 候选脉冲 -> AA55 回放流 -> rf_gateway -> Qt RF 页面` 串起来的离线 RF 模拟器。
- 一个把 `WSL 视频源/模型处理 -> TCP bridge -> Windows Qt Vision 页面` 串起来的离线视觉展示器。
- 一个适合做“字段形状是否对、UI 是否能接、代码边界在哪里”的本地验证工程。

### 它不是什么

- 不是 `project2_master` 的板端 runtime。
- 不是 `/dev/rf433` 真链。这里的 `rf_gateway` 只从 `stdin` 吃回放流，不接真实驱动节点。
- 不是真实 MQTT 验收环境。当前 `pc_sim` 没有真实 MQTT 发布链，Qt 里的 MQTT 区块只是保留的展示面。
- 不是真实 RTSP 验收环境。当前 `pc_sim` 没有 `master` 那条板端 `VisionRuntime + RTSP push` 链。
- 不是 Qt 本地直接打开 MP4 或摄像头的工程。Windows 侧 Vision 页只吃 WSL bridge。

如果 `project2_pc_sim` 和 `project2_master` 的行为或结论不一致，以 `project2_master` 为准。

“不是 `/dev/rf433` 真链”这件事在源码里是硬编码的。摘自 `linux_app/main.c`：

```c
int main(int argc, char **argv) {
    const char *rf_input = "-";
    uint16_t stable_repeat = 2u;
    uint16_t stable_window = 12u;
    uint8_t stable_near_bits = 4u;
    float min_publish_conf = 0.72f;
    uint16_t publish_gap = 6u;

    if (strcmp(argv[i], "--rf-input") == 0 && i + 1 < argc) {
        rf_input = argv[++i];
        if (!(rf_input[0] == '-' && rf_input[1] == '\0')) {
            fprintf(stderr, "--rf-input only supports '-' in pc_sim replay mode.\n");
            return 1;
        }
    }
```

这段代码把 `pc_sim` 的 `rf_gateway` 输入路径限制死为标准输入 `-`，所以 README 才会明确把它写成“回放流消费者”，而不是驱动节点 `/dev/rf433` 的替身。

## 2. 当前保留的两条链

### 2.1 RF WAV 回放链

```text
WAV
-> python/wav_to_pulses.py
-> pulse_runtime.json / pulse_runtime.txt
-> python/replay_pulse_timeline.py
-> AA55 二进制回放流
-> linux_app/rf_gateway --rf-input -
-> Qt RF Status / System Log
```

这条链的关键点：

- `python/wav_to_pulses.py` 只做候选帧提取，不负责最终业务验收判定。
- `python/replay_pulse_timeline.py` 按 `candidate_wav_sec` 做时间回放，并把脉冲编码成 AA55 帧流写到 `stdout`。
- `linux_app/main.c` 里的 `rf_gateway` 只接受 `--rf-input -`，也就是标准输入。
- `qt_gui/rf/rf_gateway_client.cpp` 负责真正串起预处理、回放和 `rf_gateway`，并把结果送到 Qt。
- Qt 里的 waveform 不是“从真实脉冲逐样本画出来的原始波形”，而是 `qt_gui/core/rf_utils.cpp` 根据解码后的 `addr` 反推出的合成 EV1527 波形。

摘自 `python/replay_pulse_timeline.py`：

```python
def _encode_frame(pulse: list[int]) -> bytes:
    if not pulse:
        return b""
    norm = [int(v) for v in pulse if 0 < int(v) <= 65535]
    if not norm:
        return b""

    header = bytearray([0xAA, 0x55, len(norm) & 0xFF, (len(norm) >> 8) & 0xFF])
    payload = bytearray()
    for p in norm:
        payload.append(p & 0xFF)
        payload.append((p >> 8) & 0xFF)
```

这段代码说明 replay 输出真的是 `AA55/LEN/PAYLOAD` 二进制帧，而不是文本行或 fake JSON。`pc_sim` 的 RF 链因此依旧在复用 pulse-frame 协议，而不是另造一套输入格式。

摘自 `qt_gui/rf/rf_gateway_client.cpp`：

```cpp
QStringList gatewayArgs;
gatewayArgs << QStringLiteral("--rf-input") << QStringLiteral("-");
gatewayArgs << QStringLiteral("--stable-repeat") << QStringLiteral("1");
gatewayArgs << QStringLiteral("--min-publish-confidence") << QStringLiteral("0.92");

replayProcess_.setProgram(pythonBin);
QStringList replayArgs = {
    replayScript,
    QStringLiteral("--pulse-json"),
    runtimePulseJsonPath_,
    QStringLiteral("--speed"),
    QString::number(replaySpeed, 'f', 3)
};
```

这里能直接看到 Qt 侧的编排顺序：先启动 `rf_gateway --rf-input -`，再启动 `replay_pulse_timeline.py --pulse-json ...`。所以 README 里把它总结成 “WAV -> pulse_runtime.json -> realtime replay -> rf_gateway -> Qt” 是有真实调用代码支撑的。

### 2.2 WSL vision bridge 链

```text
WSL 侧视频源解析
-> vision/rknn_pipeline.py
-> python/wsl_vision_bridge_server.py
-> newline-delimited JSON over TCP
-> Windows Qt Vision 页面
```

这条链的关键点：

- 视觉输入源在 WSL 侧解析，必须由 `python/wsl_vision_bridge_server.py --source ...` 指定。
- Windows Qt 不打开本地视频文件，不打开本地摄像头，不跑本地 VisionRuntime。
- bridge 发送的是 JSON 快照，里面带 `fps`、`camera_online`、`model_loaded`、`detections`、`frame_jpeg_b64` 等字段。
- Qt Vision 页只是 bridge 客户端和显示层。

摘自 `python/wsl_vision_bridge_server.py`：

```python
def _resolve_source(raw: str) -> Any:
    text = (raw or "").strip()
    if not text:
        raise FileNotFoundError("vision source is required for the fixed bridge chain; pass --source explicitly")
    if text.isdigit():
        return int(text)
    for path in _candidate_paths(text):
        if path.exists():
            return str(path)
    raise FileNotFoundError(f"video source not found for fixed chain: {text}")
```

这段说明 `pc_sim` 的 Vision bridge 不是 Qt 本地自己打开视频源，而是要求 WSL 侧显式传 `--source`，由 bridge 先解析并产出 JSON 快照，再交给 Windows 侧 Qt 页面消费。

## 3. Qt 前端在 `pc_sim` 里的角色

Qt 前端由 `qt_gui/app/main.cpp` 启动，主窗口在 `qt_gui/app/main_window.cpp` 组装。当前保留 3 个页面：

- `RF Status`
- `Vision`
- `System Log`

各自职责如下：

- `RF Status`
  显示 `rf_gateway` 输出的最近解码结果、候选 WAV 秒数、历史事件和合成波形。
- `Vision`
  连接 WSL bridge，解析每行 JSON，显示 JPEG 画面、检测框、FPS、camera/model 状态。
- `System Log`
  汇总 RF / Vision / System 日志和资源状态。页面里虽然有 MQTT 区块，但 `pc_sim` 当前不会形成真实 MQTT 上报链。

## 4. 目录速览

推荐先把这几个位置和职责对上：

| 路径 | 作用 |
| --- | --- |
| `linux_app/main.c` | `rf_gateway` 入口，只消费回放流，不接 `/dev/rf433` |
| `python/wav_to_pulses.py` | 从 WAV 抽 EV1527 候选脉冲帧 |
| `python/replay_pulse_timeline.py` | 按时间回放候选帧，输出 AA55 二进制流 |
| `python/wsl_vision_bridge_server.py` | WSL 侧 bridge 服务器 |
| `vision/rknn_pipeline.py` | WSL 侧视觉线程管线 |
| `qt_gui/rf/rf_gateway_client.cpp` | Qt 端 RF 总编排 |
| `qt_gui/vision/vision_page.cpp` | Qt 端 Vision bridge 客户端 |
| `python/fake_detection_stream_test.py` | 伪造视觉 bridge 流，验证 Qt Vision 页面 |
| `python/fake_event_record_test.py` | 伪造本地 event / record_done 产物，验证事件交接形状 |

更细的阅读路线见 [docs/project2_pc_sim_reading_guide.md](docs/project2_pc_sim_reading_guide.md)。

## 5. 依赖与环境拆分

`pc_sim` 实际上有两套 Python 使用场景，不要混在一起理解。

### 5.1 Windows 侧 Python

Windows 侧 Python 主要用于：

- `python/wav_to_pulses.py`
- `python/replay_pulse_timeline.py`
- `python/fake_detection_stream_test.py`
- `python/fake_event_record_test.py`
- 作为 Qt `--python-bin` 传给 RF 链

当前仓库内已经有一个可直接引用的解释器：

```text
project2_pc_sim\.venv\Scripts\python.exe
```

建议所有本地离线验证都优先用这个路径，尤其是：

- Qt 的 `--python-bin`
- `fake_detection_stream_test.py`
- `fake_event_record_test.py`

### 5.2 WSL 侧 Python

WSL 侧 Python 主要用于：

- `python/wsl_vision_bridge_server.py`
- `vision/rknn_pipeline.py`

这套环境至少要能安装 `requirements-vision.txt` 里的依赖，并在需要模型执行时额外安装 Rockchip 的 RKNN wheel。

### 5.3 依赖包拆分

当前依赖文件的含义是：

- `requirements-rf.txt`
  RF 辅助脚本层。当前故意为空，因为 `wav_to_pulses.py` 和 `replay_pulse_timeline.py` 只依赖 Python 标准库。
- `requirements-vision.txt`
  WSL bridge / fake detection 所需第三方依赖：
  - `numpy`
  - `opencv-python-headless`
  - 通过 `-r requirements-rf.txt` 继承 RF 层
- `requirements.txt`
  顶层说明性依赖列表，当前同样强调：
  - `numpy`
  - `opencv-python-headless`
  - `rknn-toolkit2` 需要单独安装
  - `rknnlite` 是板端 runtime 相关，不是 Windows Qt 模拟器依赖

## 6. 构建 Qt 和 rf_gateway

当前顶层 `CMakeLists.txt` 会分两个目标：

- `rf_gateway`
- `rf_dashboard_qt5`

`qt_gui/CMakeLists.txt` 明确要求：

- `Qt5::Core`
- `Qt5::Gui`
- `Qt5::Widgets`
- `Qt5::Network`

一个典型的 Windows 构建流程如下：

```powershell
Set-Location D:\project\repos\project2\project2_pc_sim

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

& 'C:\Program Files\CMake\bin\cmake.exe' `
  --build D:\project\repos\project2\project2_pc_sim\build `
  --config Release `
  --parallel 4 `
  --target rf_gateway rf_dashboard_qt5
```

Qt 程序从 shell 启动前，通常还需要把 Qt / MinGW 运行时目录加进 `PATH`：

```powershell
$env:PATH = 'D:\qt\5.12.9\mingw73_64\bin;D:\qt\Tools\mingw730_64\bin;' + $env:PATH
```

## 7. Qt 启动参数说明

Qt 入口在 `qt_gui/app/main.cpp`。当前支持的参数就是下面这些，没有隐藏的 `--disable-vision`：

| 参数 | 含义 | 当前行为 |
| --- | --- | --- |
| `--gateway <path>` | `rf_gateway` 可执行文件路径 | 需要传有效文件路径，否则 RF 链启动失败 |
| `--wav-input <path>` | 要回放的 WAV 文件路径 | 固定链模式下必需 |
| `--wav-loop` | WAV 回放结束后循环 | 可选 |
| `--wav-speed <factor>` | 回放速度倍率 | 运行时要求 `> 0` |
| `--python-bin <path>` | Windows 侧 Python 解释器 | 建议传 `.\.venv\Scripts\python.exe` |
| `--vision-host <host>` | WSL bridge 主机 | 默认 `127.0.0.1` |
| `--vision-port <port>` | WSL bridge 端口 | 默认 `0`，表示禁用 Vision bridge |

特别注意：

- 当前代码里，Vision 是否启用是通过 `--vision-port` 是否大于 `0` 决定的。
- 也就是说，不传 `--vision-port` 时，Vision 页会显示 bridge disabled / offline，不会主动连 `17655`。
- 如果要接 WSL bridge 或 `fake_detection_stream_test.py --serve`，一定要显式传一个大于 `0` 的端口。

## 8. 一套最常用的离线启动方式

### 8.1 只验证 RF，不启 Vision

```powershell
Set-Location D:\project\repos\project2\project2_pc_sim

$env:PATH = 'D:\qt\5.12.9\mingw73_64\bin;D:\qt\Tools\mingw730_64\bin;' + $env:PATH

.\build\qt_gui\rf_dashboard_qt5.exe `
  --gateway .\build\linux_app\rf_gateway.exe `
  --wav-input D:\project\repos\project2\capture03.wav `
  --python-bin .\.venv\Scripts\python.exe `
  --vision-port 0
```

这会触发：

1. Qt 调 `wav_to_pulses.py`
2. 在 `%TEMP%\project2_pc_sim_runtime` 生成 `pulse_runtime.txt` 和 `pulse_runtime.json`
3. Qt 调 `replay_pulse_timeline.py`
4. Qt 把回放出来的 AA55 字节流写给 `rf_gateway`
5. `rf_gateway` 输出 JSON 行，Qt 再更新 RF 页面

### 8.2 验证真实 WSL bridge + Qt Vision 页面

先在 WSL 里启动 bridge：

```bash
cd /mnt/d/project/repos/project2/project2_pc_sim
python python/wsl_vision_bridge_server.py \
  --host 127.0.0.1 \
  --port 17655 \
  --source /mnt/d/project/repos/project2/test.mp4
```

再在 Windows 启 Qt：

```powershell
Set-Location D:\project\repos\project2\project2_pc_sim

$env:PATH = 'D:\qt\5.12.9\mingw73_64\bin;D:\qt\Tools\mingw730_64\bin;' + $env:PATH

.\build\qt_gui\rf_dashboard_qt5.exe `
  --gateway .\build\linux_app\rf_gateway.exe `
  --wav-input D:\project\repos\project2\capture03.wav `
  --python-bin .\.venv\Scripts\python.exe `
  --vision-host 127.0.0.1 `
  --vision-port 17655
```

如果 `--model` 不传，bridge 会默认找：

```text
<project2_pc_sim 父目录>\yolov5s.onnx
```

也就是当前仓库布局下的：

```text
D:\project\repos\project2\yolov5s.onnx
```

### 8.3 只用 fake bridge 验证 Qt Vision 页面

```powershell
Set-Location D:\project\repos\project2\project2_pc_sim

.\.venv\Scripts\python.exe .\python\fake_detection_stream_test.py --serve --loop --port 17656
```

另开一个窗口启动 Qt：

```powershell
Set-Location D:\project\repos\project2\project2_pc_sim

$env:PATH = 'D:\qt\5.12.9\mingw73_64\bin;D:\qt\Tools\mingw730_64\bin;' + $env:PATH

.\build\qt_gui\rf_dashboard_qt5.exe `
  --gateway .\build\linux_app\rf_gateway.exe `
  --wav-input D:\project\repos\project2\capture03.wav `
  --python-bin .\.venv\Scripts\python.exe `
  --vision-host 127.0.0.1 `
  --vision-port 17656
```

## 9. fake 脚本怎么用

### 9.1 `python/fake_detection_stream_test.py`

作用：

- 不跑 RKNN 推理。
- 复用 `vision/rknn_pipeline.py::VisionPipelineState`。
- 复用 `python/wsl_vision_bridge_server.py::_build_payload()`。
- 产出与 Qt Vision 页当前一致的 bridge JSON 形状。

推荐用法：

```powershell
Set-Location D:\project\repos\project2\project2_pc_sim
.\.venv\Scripts\python.exe .\python\fake_detection_stream_test.py
```

常用参数：

- `--source-image <path>`: 用一张本地图做背景，不传则自动生成合成背景。
- `--out-dir <path>`: 输出目录，默认 `sim_data\local_validation\vision`。
- `--frame-count <N>`: 生成帧数，默认 `12`。
- `--fps <F>`: 写进 payload 的名义 FPS，默认 `4.0`。
- `--width <W>` / `--height <H>`: 合成背景尺寸。
- `--serve`: 除了写文件，还开 TCP 服务供 Qt 连接。
- `--loop`: 开服务时循环发送。
- `--host <host>` / `--port <port>`: 服务地址，默认 `127.0.0.1:17656`。

产物：

- `sim_data/local_validation/vision/bridge_stream.ndjson`
- `sim_data/local_validation/vision/annotated_frame_*.jpg`
- `sim_data/local_validation/vision/manifest.json`

它验证的是：

- Qt Vision 页当前消费的 JSON 合同是否还能接住。
- base64 JPEG 画面和检测框渲染是否正常。
- `fps`、`frame_count`、`detections` 等状态字段是否能正确投影到 UI。

它不验证：

- RKNN 模型加载
- 真实视频解码
- WSL 路径解析
- RTSP 推流
- `project2_master` 的板端 VisionRuntime

### 9.2 `python/fake_event_record_test.py`

作用：

- 不调用 `project2_master`
- 不启动真实 recorder
- 只生成一个本地事件交接形状，用于说明“未来接真录制链时应该对齐什么字段”

推荐同样使用仓库内 `.venv`：

```powershell
Set-Location D:\project\repos\project2\project2_pc_sim
.\.venv\Scripts\python.exe .\python\fake_event_record_test.py --trigger rf_confirmed
```

手动触发示例：

```powershell
.\.venv\Scripts\python.exe .\python\fake_event_record_test.py `
  --trigger manual_record `
  --manual-note "operator smoke test"
```

如果要把某一条 `rf_gateway` JSON 行塞进去验证：

```powershell
.\.venv\Scripts\python.exe .\python\fake_event_record_test.py `
  --trigger rf_confirmed `
  --rf-json '{"addr":"0x35A1BC","key":"12","conf":0.94,"src":"replay","seq":7,"wav_sec":1.234,"pulses":50}'
```

产物：

- `sim_data/local_validation/event/event.json`
- `sim_data/local_validation/event/record_done.json`
- `sim_data/local_validation/event/mock_record.txt`

它验证的是：

- `rf_confirmed` / `manual_record` 两种触发的最小交接形状
- RF JSON 中 `addr`、`key`、`conf`、`src`、可选 `seq` / `wav_sec` / `pulses` 的解析规则

它不验证：

- 真录制进程
- MP4 切片
- MQTT 副作用
- 与 `project2_master` 最终事件 schema 的完全兼容性

## 10. 运行时限制与常见误解

### 10.1 RF 侧限制

- `rf_gateway` 当前不是板端 `master` 那个 `/dev/rf433` 消费者角色。
- `pc_sim/linux_app/rf_source.c` 明确规定：这里只支持 `stdin` 回放输入。
- `qt_gui/rf/rf_gateway_client.cpp` 启动 `rf_gateway` 时固定传 `--rf-input - --stable-repeat 1 --min-publish-confidence 0.92`。
- `wav_sec` 不是来自驱动硬件时间戳，而是 Qt 根据 `pulse_runtime.json` 里的 `candidate_idx -> candidate_wav_sec` 回填出来的。

### 10.2 Vision 侧限制

- Qt Vision 页不接本地视频路径。
- WSL bridge 要求显式 `--source`，空 source 会直接报错退出。
- `vision/rknn_pipeline.py` 的模型加载是“能加载就跑，失败就把状态写成 unloaded / error”，不是 `master` 那种板端验收链。

### 10.3 系统日志页的限制

- 你会看到 MQTT 区块，但当前 `pc_sim` 正常离线运行时不应把它理解成“已经打通真实 MQTT”。
- 你会看到 Vision model / FPS 区块，但这只代表 bridge 上报状态，不等同于 `master` 板端 VisionRuntime 的验收结论。

## 11. `project2_pc_sim` 与 `project2_master` 的关系

最简单的说法是：

- `project2_master` 是主工程、板端运行时、真实链路和验收基线。
- `project2_pc_sim` 是配套的 PC 侧离线模拟器，用来提前验证字段合同、UI 消费链和局部脚本行为。

可以按下面这张表理解：

| 维度 | `project2_master` | `project2_pc_sim` |
| --- | --- | --- |
| RF 输入 | `/dev/rf433` 真链 | WAV 提取后回放到 `stdin` |
| RF 时间/波形 | 来自真实帧与真实脉冲 | `candidate_wav_sec` + 合成 waveform |
| Vision 运行位置 | Qt 进程内本地 `VisionRuntime` | WSL 侧 bridge，Qt 只做客户端 |
| MQTT | 真上报链在 master | `pc_sim` 只保留 UI 位，不构成真实验收 |
| RTSP | 真推流链在 master | `pc_sim` 不负责真实 RTSP 验收 |
| 角色 | 主实现 / 主验收面 | 辅助模拟 / 辅读代码面 |

所以文档、讨论和调试时，建议把它表述为：

**`project2_pc_sim` 是 `project2_master` 的离线配套模拟工程，不是其等价运行时。它复用了部分协议和 UI 形状，但故意只保留可在 PC 上复现的局部链路。**

## 12. 下一步读哪里

- 想看整体阅读顺序： [docs/project2_pc_sim_reading_guide.md](docs/project2_pc_sim_reading_guide.md)
- 想按模块查函数： [docs/project2_pc_sim_function_index.md](docs/project2_pc_sim_function_index.md)
- 想看边界和数据流： [docs/pc_sim_architecture.md](docs/pc_sim_architecture.md)
- 想看 RF / EV1527 字段映射： [docs/ev1527_truth_mapping.md](docs/ev1527_truth_mapping.md)
