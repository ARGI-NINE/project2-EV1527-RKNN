# project2_pc_sim 模块与函数索引

这份索引不是逐行注释，而是帮助你快速定位：

- 每个模块的职责
- 从哪里进入
- 哪些函数最值得先看
- 哪些边界最容易误读

## 0. 快速索引 / 阅读地图

| 模块 | 作用 | 当前详细段落 |
| --- | --- | --- |
| `linux_app` | 消费 replay 流、解码并输出 RF JSON 行 | “7. `rf_gateway` 用户态实现” |
| `qt_gui` | 装配页面、编排 RF 链、消费 Vision bridge | “2. Qt 启动与页面装配”到“5. Vision 链模块” |
| `python` | 做 RF 预处理/回放与 WSL Vision bridge 组包 | “6. RF 预处理与回放脚本”与“5. Vision 链模块” |
| fake 验证脚本 | 复用真实合同做离线验证 | “8. fake 验证脚本” |

## 1. 契约 / 职责

### 1.1 `linux_app`

`pc_sim` 里的 `linux_app` 只消费 `stdin` 回放流，不承担 `/dev/rf433` 真链职责。

### 1.2 `qt_gui`

`qt_gui` 负责参数收口、页面装配以及 RF/Vision 两条展示链的消费与编排。

### 1.3 `python`

`python` 目录负责把 WAV 预处理成候选帧、按时间线回放 AA55，以及在 WSL 侧组装 Vision bridge payload。

## 1. 顶层与构建

| 文件 | 作用 | 建议先看什么 | 备注 |
| --- | --- | --- | --- |
| `CMakeLists.txt` | 划分 `linux_app` 和 `qt_gui` 两个构建目标 | `option(BUILD_LINUX_APP ...)`, `option(BUILD_QT5_GUI ...)` | `pc_sim` 的 RF 与 Qt 是并列目标，不是单体程序 |
| `qt_gui/CMakeLists.txt` | 定义 `rf_dashboard_qt5` | `find_package(Qt5 REQUIRED ...)`, `add_executable(...)` | Qt 依赖 `Core/Gui/Widgets/Network` |
| `linux_app/CMakeLists.txt` | 定义 `rf_gateway` | `add_executable(rf_gateway ...)` | Windows 下通过 `stdin` 跑回放流 |

#### 代表性代码摘录：顶层构建先把 `rf_gateway` 和 Qt 前端拆成并列目标

代码来源：`project2_pc_sim/CMakeLists.txt` / `qt_gui/CMakeLists.txt` / `linux_app/CMakeLists.txt`

```cmake
option(BUILD_LINUX_APP "Build Linux gateway app" ON)
option(BUILD_QT5_GUI "Build Qt5 C++ dashboard frontend" ON)

if(BUILD_LINUX_APP)
  add_subdirectory(linux_app)
endif()

if(BUILD_QT5_GUI)
  add_subdirectory(qt_gui)
endif()

add_executable(rf_dashboard_qt5 ${QT_GUI_SOURCES})
add_executable(rf_gateway main.c $<TARGET_OBJECTS:rf_gateway_core> $<TARGET_OBJECTS:rf_common>)
```

- 为什么先看这段：它最先把 `pc_sim` 的整体形状定死了，RF 网关和 Qt 前端是两个独立可执行目标，不是单体 runtime。
- 看代码时要注意什么：如果后面你看到 Qt 拉起 `rf_gateway`，那是运行时编排，不是编译期就把它们合成一个程序。

## 2. 生命周期

### 2.1 Qt 启动链

当前 Qt 启动与页面装配的详细回查见本节下方两个文件小节。

## 2. Qt 启动与页面装配

### 2.1 `qt_gui/app/main.cpp`

### 4.2 `qt_gui/app/main.cpp`

| 函数/位置 | 作用 | 要点 |
| --- | --- | --- |
| `main()` | Qt 程序入口 | 解析 `--gateway --wav-input --wav-loop --wav-speed --python-bin --vision-host --vision-port` |

阅读要点：

- 当前没有 `--disable-vision`
- `--vision-port` 默认 `0`
- Vision 是否启用由 `visionPort > 0` 决定

### 2.2 `qt_gui/app/main_window.cpp`

### 4.3 `qt_gui/app/main_window.cpp`

| 函数 | 作用 | 要点 |
| --- | --- | --- |
| `MainWindow::MainWindow()` | 主窗口构造 | 创建页面并启动运行时 |
| `setupUi()` | 组装 `RF Status / Vision / System Log` 三页 | 这里只负责 UI 壳 |
| `setupRuntime()` | 启动 backend 日志和 RF 客户端 | Vision 不在这里跑本地运行时 |
| `updateStatusBar()` | 汇总 RF/Vision/System 状态栏 | 读 `DashboardBackend` 快照 |

#### 代表性代码摘录：Qt 启动层负责参数收口和页面装配，不负责底层链路实现

代码来源：`project2_pc_sim/qt_gui/app/main.cpp::main()` / `qt_gui/app/main_window.cpp::setupRuntime()`

```cpp
QCommandLineOption gatewayOption(
    QStringLiteral("gateway"),
    QStringLiteral("Path to rf_gateway executable."),
    QStringLiteral("path")
);
QCommandLineOption wavInputOption(
    QStringLiteral("wav-input"),
    QStringLiteral("Real WAV input path for fixed full-duration RF playback."),
    QStringLiteral("path")
);
QCommandLineOption wavLoopOption(
    QStringLiteral("wav-loop"),
    QStringLiteral("Loop WAV timeline playback after reaching end.")
);
QCommandLineOption wavSpeedOption(
    QStringLiteral("wav-speed"),
    QStringLiteral("WAV timeline playback speed multiplier (1.0 means realtime)."),
    QStringLiteral("factor"),
    QStringLiteral("1.0")
);
QCommandLineOption pythonBinOption(
    QStringLiteral("python-bin"),
    QStringLiteral("Python interpreter path passed to rf_gateway."),
    QStringLiteral("path")
);
QCommandLineOption visionHostOption(
    QStringLiteral("vision-host"),
    QStringLiteral("WSL vision bridge host."),
    QStringLiteral("host"),
    QStringLiteral("127.0.0.1")
);
QCommandLineOption visionPortOption(
    QStringLiteral("vision-port"),
    QStringLiteral("WSL vision bridge TCP port (0 disables the bridge)."),
    QStringLiteral("port"),
    QStringLiteral("0")
);
parser.addOption(gatewayOption);
parser.addOption(wavInputOption);
parser.addOption(wavLoopOption);
parser.addOption(wavSpeedOption);
parser.addOption(pythonBinOption);
parser.addOption(visionHostOption);
parser.addOption(visionPortOption);

parser.process(app);

options.gatewayPath = parser.value(gatewayOption).trimmed();
options.wavPath = parser.value(wavInputOption).trimmed();
options.wavLoop = parser.isSet(wavLoopOption);
options.wavSpeed = parser.value(wavSpeedOption).toDouble();
options.pythonBin = parser.value(pythonBinOption).trimmed();
options.visionHost = parser.value(visionHostOption).trimmed();
options.visionPort = parser.value(visionPortOption).toInt();

void MainWindow::setupRuntime() {
    backend_.addLog(QStringLiteral("INFO"), QStringLiteral("SYSTEM"), QStringLiteral("Qt5 frontend started"));
    rfClient_.start();
    updateStatusBar();
}
```

- 为什么先看这段：它最能代表启动层的风格，先把所有外部路径和开关收进 `AppOptions`，然后只拉起 UI 和 RF 客户端。
- 真实参数和默认值：`--gateway`、`--wav-input`、`--python-bin` 在这里都没有 CLI 默认值，所以 `AppOptions.gatewayPath / wavPath / pythonBin` 初始就是空字符串；`--wav-loop` 默认 `false`；`--wav-speed` 默认 `1.0`；`--vision-host` 默认 `127.0.0.1`；`--vision-port` 默认 `0`，即 bridge 默认禁用。
- 真实字段和重要性：`main.cpp` 解析出的就是 `gatewayPath / wavPath / wavLoop / wavSpeed / pythonBin / visionHost / visionPort` 这组启动合同，而 `setupRuntime()` 随后只拉起 `rfClient_`；这正好说明 Vision 页并不在这里启动本地 runtime，而是依赖前面解析好的 bridge 参数自行连接。

## 3. Qt 后端状态汇聚

### `qt_gui/core/dashboard_backend.cpp`

| 函数 | 作用 | 备注 |
| --- | --- | --- |
| `DashboardBackend::addRFEvent()` | 记录最新 RF 事件和历史列表 | RF 页与日志页都依赖它 |
| `updateWaveform()` | 更新当前 waveform | 页面显示的是合成波形 |
| `incrementCrcError()` / `incrementParseError()` / `incrementDrop()` | 记录错误计数 | 来自 `rf_gateway` 输出解析 |
| `updateVisionState()` | 更新 Vision 快照 | Qt Vision 页定时刷新后写回这里 |
| `setVisionOffline()` | 把视觉状态置为离线 | Vision bridge 未启用或连接失败时使用 |
| `addLog()` | 统一追加日志 | `RF` / `VISION` / `SYSTEM` / `MQTT` 都走这里 |
| `snapshotRF()` | 取 RF 快照 | RF 页面读 |
| `snapshotVisionState()` | 取 Vision 快照 | 日志页和状态栏读 |
| `snapshotSystemStats()` | 取 CPU / 内存 / uptime | Windows 下调用系统 API |

#### 代表性代码摘录：backend 的典型风格是“收快照，不做业务推理”

代码来源：`project2_pc_sim/qt_gui/core/dashboard_backend.cpp::addRFEvent()` / `updateVisionState()`

```cpp
void DashboardBackend::addRFEvent(const RFEvent &event) {
    QMutexLocker locker(&mutex_);
    hasLastDecode_ = true;
    lastDecode_ = event;
    eventHistory_.prepend(event);
    if (eventHistory_.size() > kMaxHistory) {
        eventHistory_.resize(kMaxHistory);
    }
    ++frameCount_;
}

void DashboardBackend::updateVisionState(const VisionSnapshot &snapshot) {
    QMutexLocker locker(&mutex_);
    visionSnapshot_ = snapshot;
    visionSnapshot_.statusReported = true;
}
```

- 为什么先看这段：它说明 backend 更像线程安全状态仓库，而不是再次解码或推断 RF/Vision 业务。
- 看代码时要注意什么：RF 波形和 Vision 状态都是其他模块算好后写进来的；页面只会从这里读快照。

## 3. 边界条件 / ABI

### 3.1 C/C++ 侧

最关键的 C/C++ 边界是：`rf_gateway` 仍只接受 `--rf-input -`，Qt 负责外部进程编排和页面消费，不承担板端 `/dev/rf433` 语义。

### 3.2 Python 侧

最关键的 Python 边界是：`wav_to_pulses.py` 只筛候选帧，`replay_pulse_timeline.py` 只输出 AA55 二进制流，`wsl_vision_bridge_server.py` 负责 Vision JSON 组包。

### 3.3 展示边界

RF waveform 是按解码地址合成的展示面；Vision 画面来自 bridge payload 中的 `frame_jpeg_b64`，两者都不是板端真值源。

## 4. RF 链模块

### 2.2 RF 回放链

当前 RF 生命周期的实际编排从这里展开：Qt 先预处理 WAV，再拉起 replay 与 `rf_gateway`，最后把 stdout JSON 行投给 backend。

### 4.1 `qt_gui/rf/rf_gateway_client.cpp`

### 4.4 `qt_gui/rf/rf_gateway_client.cpp`

这是 RF 链最关键的文件。

| 函数 | 作用 | 关键事实 |
| --- | --- | --- |
| `start()` | 启动 RF 链 | 入口 |
| `startGatewayProcess()` | 做参数检查并准备预处理 | `--wav-input` / `--python-bin` / `--gateway` 都要能解析到现有文件 |
| `prepareRealtimeTimeline()` | 启 `wav_to_pulses.py` | 生成 `%TEMP%\project2_pc_sim_runtime\pulse_runtime.txt/json` |
| `onPrepFinished()` | 读取 `pulse_runtime.json` | 建 `candidate_idx -> candidate_wav_sec` 索引 |
| `startGatewayWithRealtimeInput()` | 启 `rf_gateway` 和 `replay_pulse_timeline.py` | `rf_gateway` 固定收 `stdin` |
| `onReplayStdout()` | 把 replay 输出写进 `rf_gateway` stdin | 真正把链串起来 |
| `handleGatewayLine()` | 处理 `rf_gateway` 输出行 | 成功时写 RF event，失败时加 parse/crc/drop 计数 |
| `parseGatewayEventLine()` | 解析 `addr/key/conf/src/seq/wav_sec` | Qt RF 页读这个结构 |
| `computeFirstRfTimeoutMs()` | 推算首帧等待超时 | 按时间线帧数粗估 |
| `resolvePythonBin()` / `resolveGatewayPath()` / `resolveWavInputPath()` | 路径解析 | 支持直接路径、cwd 相对路径、项目相对路径 |

阅读要点：

- 这个类不是“串口客户端”，而是“WAV 回放编排器”。
- 它会给 `rf_gateway` 传 `--stable-repeat 1 --min-publish-confidence 0.92`。
- `wav_sec` 在当前链里不是设备直接给出来的，而是 Qt 依据 `pulse_runtime.json` 回填。

### 4.2 `qt_gui/rf/rf_status_page.cpp`

| 函数 | 作用 | 备注 |
| --- | --- | --- |
| `setupUi()` | 搭 RF 页面结构 | 包括 link、waveform、latest decode、history |
| `refresh()` | 定时从 backend 拉取快照 | 100ms 刷新 |
| `showEventDetails()` | 展示选中事件详情 | `Address / WAV Sec / Confidence` |
| `onHistoryRowClicked()` | 点击历史项后锁定显示 | 5 秒后自动释放 |
| `sameEvent()` | 历史项比对 | 优先用 `frameSeq` |

阅读要点：

- RF 页面展示的波形优先来自 `addr -> 合成波形`
- `WAV Sec` 只是候选回放时间

### 4.3 `qt_gui/core/rf_utils.cpp`

| 函数 | 作用 | 备注 |
| --- | --- | --- |
| `parseRawCode()` | 解析 `0x123456` 风格地址 | 返回 24-bit raw code |
| `buildWaveformFromRawCode()` | 根据 raw code 生成 EV1527 脉冲数组 | 固定 `tUs = 300` 的合成展示波形 |

这两个函数的边界非常重要：

- 它们是 UI 辅助函数
- 不是 RF 真值来源

#### 代表性代码摘录：RF 链模块的典型风格是“编排外部进程，再做本地回填和展示”

代码来源：`project2_pc_sim/qt_gui/rf/rf_gateway_client.cpp::startGatewayWithRealtimeInput()` / `handleGatewayLine()`

```cpp
gatewayArgs << QStringLiteral("--rf-input") << QStringLiteral("-");
gatewayArgs << QStringLiteral("--stable-repeat") << QStringLiteral("1");
gatewayArgs << QStringLiteral("--min-publish-confidence") << QStringLiteral("0.92");

QStringList replayArgs = {
    replayScript,
    QStringLiteral("--pulse-json"),
    runtimePulseJsonPath_,
    QStringLiteral("--speed"),
    QString::number(replaySpeed, 'f', 3)
};

if (event.frameSeq > 0 && event.candidateWavSec < 0.0) {
    event.candidateWavSec = replayWavSecByIdx_.value(static_cast<int>(event.frameSeq), -1.0);
}
backend_->addRFEvent(event);
backend_->updateWaveform(buildWaveformFromRawCode(parseRawCode(event.address)));
```

- 为什么先看这段：它把 RF 模块最关键的两件事都放出来了，一是拉起 replay/gateway，二是把页面展示字段本地补齐。
- 看代码时要注意什么：`pc_sim` 的波形不是原始 pulse 直接画出来的；`wav_sec` 也不是底层设备给的，而是 Qt 按 `pulse_runtime.json` 回填。

## 5. Vision 链模块

### 2.3 Vision bridge 链

当前 Vision 生命周期的实际编排从这里展开：WSL 侧 bridge 组包，Windows Qt 页面只负责连线、解包和刷新展示。

### 5.1 `qt_gui/vision/vision_page.cpp`

### 4.5 `qt_gui/vision/vision_page.cpp`

| 函数 | 作用 | 关键事实 |
| --- | --- | --- |
| `setupVisionBridge()` | 决定是否启用 bridge | `visionPort <= 0` 时直接视为禁用 |
| `attemptVisionBridgeConnect()` | 发起或重试连接 | 2 秒重连一次 |
| `onVisionBridgeReadyRead()` | 按行拆 JSON | payload 是 newline-delimited |
| `handleVisionBridgeLine()` | 解析单行 JSON | 失败时记 `VISION` 日志 |
| `applyVisionBridgePayload()` | 把 JSON 投影到页面状态 | 解析 frame/detections/fps/model/camera/error |
| `resetRemoteState()` | 断线清状态 | 避免显示陈旧数据 |
| `refresh()` | 刷新页面控件并写回 backend | 33ms 定时器 |
| `normalizedVisionBridgeHost()` | 客户端 host 规范化 | `localhost` / `0.0.0.0` 会被改成 `127.0.0.1` |

阅读要点：

- Qt Vision 页只是 bridge 消费层
- `frame_jpeg_b64` 是它显示画面的唯一直接来源
- 没有本地视频打开逻辑

### 5.2 `python/wsl_vision_bridge_server.py`

| 函数 | 作用 | 备注 |
| --- | --- | --- |
| `parse_args()` | bridge CLI | `--source` 是必填 |
| `_windows_path_to_wsl()` | 试着把 Windows 路径翻译成 `/mnt/<drive>/...` | 支持桥接脚本直接吃 Windows 风格路径 |
| `_candidate_paths()` | 生成候选路径列表 | 同时尝试原始路径和翻译路径 |
| `_resolve_model_path()` | 解析模型路径 | 默认找 `PROJECT_ROOT.parent / "yolov5s.onnx"` |
| `_resolve_source()` | 解析视频源 | 支持数字摄像头索引或文件路径 |
| `_format_detections()` | 把内部 boxes/classes/scores 转成 JSON 结构 | 供 Qt 消费 |
| `_render_overlay()` | 把检测框画到图上 | JPEG 发给 Qt 的是带框图 |
| `_encode_frame()` | JPEG + base64 编码 | payload 里的 `frame_jpeg_b64` 来源 |
| `_build_payload()` | 组装 bridge JSON | 当前 Vision 协议核心 |
| `main()` | 启管线、开 TCP 服务、广播 payload | WSL 侧入口 |

阅读要点：

- bridge 是服务端
- payload 只在有客户端连接时推送
- 即使模型失败，也会通过 `error` / `model_loaded` 状态向 Qt 报告

### 5.3 `vision/rknn_pipeline.py`

| 函数/类 | 作用 | 备注 |
| --- | --- | --- |
| `VisionPipelineState` | 线程安全视觉状态容器 | fake detection 和 bridge 都复用 |
| `get_snapshot()` | 取状态快照 | `include_frame` 决定是否拷画面 |
| `update_detections()` | 更新 frame + boxes + fps | 每帧更新 |
| `set_model_status()` | 记模型加载状态 | 出错也通过这里报 |
| `set_camera_status()` | 记相机/视频源状态 | Qt 页面会反映 |
| `VisionPipeline.start()` | 启动 capture/inference/postprocess 三线程 | WSL 侧实际管线入口 |
| `_capture_loop()` | 打开视频源并采帧 | 文件源可循环 |
| `_inference_loop()` | 加载 RKNN 并跑 inference | `rknn.api` 在这里按需导入 |
| `_postprocess_loop()` | 做后处理与 FPS 统计 | 输出最终 detections |
| `_load_model()` | 支持 `.onnx` 或 `.rknn` | 失败则写错误状态 |

阅读要点：

- 这是 WSL 侧运行时，不是 Windows Qt 本地 runtime
- 其存在目的是把视觉结果送过 bridge，而不是复刻 `master` 的 `VisionRuntime`

#### 代表性代码摘录：Vision 合同由 bridge 端组包，Qt 端只做字段投影

代码来源：`project2_pc_sim/python/wsl_vision_bridge_server.py::_build_payload()` / `qt_gui/vision/vision_page.cpp::applyVisionBridgePayload()`

```python
return {
    "fps": float(snapshot.get("fps", 0.0)),
    "camera_online": bool(snapshot.get("camera_online", False)),
    "model_loaded": bool(snapshot.get("model_loaded", False)),
    "frame_count": int(snapshot.get("frame_count", 0)),
    "error": str(snapshot.get("error_msg", "") or ""),
    "detections": det_structs,
    "frame_jpeg_b64": _encode_frame(overlay),
}
```

```cpp
if (obj.contains(QStringLiteral("fps"))) {
    remoteFps_ = obj.value(QStringLiteral("fps")).toDouble(remoteFps_);
}
if (obj.contains(QStringLiteral("camera_online"))) {
    remoteCameraOnline_ = obj.value(QStringLiteral("camera_online")).toBool(remoteCameraOnline_);
}
const QByteArray jpegData = QByteArray::fromBase64(encoded.toLatin1());
```

- 为什么先看这段：它把 Vision 模块最重要的合同字段列得很全，也展示了 Qt 端只是被动消费这些字段。
- 看代码时要注意什么：只要 bridge 端字段不变，Qt Vision 页就能工作；这条链的权威来源是 bridge 组包，不是页面代码。

## 6. RF 预处理与回放脚本

### 4.6 `python`

### 6.1 `python/wav_to_pulses.py`

| 函数 | 作用 | 备注 |
| --- | --- | --- |
| `load_decoder_module()` | 动态加载 `ev1527_decode.py` | 重用 WAV 解析辅助逻辑 |
| `_build_runs()` | 平滑 + run-length | 候选帧前置处理 |
| `_extract_candidate_frames()` | 滑窗提取候选 pulse 帧 | Stage-1 核心 |
| `_candidate_row()` | 组装候选帧 | 只写 `candidate_wav_sec` 和 `pulse` |
| `extract_frames()` | 对外主入口 | 会补 `candidate_idx` |
| `write_outputs()` | 写 txt/json 产物 | Qt 后续读取 JSON |
| `main()` | CLI 入口 | 当前常被 Qt 调用 |

边界：

- 不做最终发布
- 不做 UI
- 不做硬件时序验收

### 6.2 `python/replay_pulse_timeline.py`

| 函数 | 作用 | 备注 |
| --- | --- | --- |
| `_crc8()` | 计算 RF 协议 CRC | AA55 帧编码辅助 |
| `_encode_frame()` | 把 pulse 数组编码为 AA55 数据包 | `SYNC + LEN + PAYLOAD + CRC` |
| `_load_frames()` | 读 `pulse.json` / `pulse_runtime.json` | 取 `candidate_idx` 和 `candidate_wav_sec` |
| `_prepare_binary_stdout()` | Windows 下切换 `stdout` 为二进制模式 | 回放脚本关键兼容处理 |
| `main()` | 按时间线回放并写 `stdout` | 给 `rf_gateway` 喂数据 |

边界：

- 输出是二进制帧，不是文本日志
- 时间依据是 `candidate_wav_sec`

#### 代表性代码摘录：RF 预处理与回放脚本先筛候选，再编码 AA55

代码来源：`project2_pc_sim/python/wav_to_pulses.py::_extract_candidate_frames()` / `python/replay_pulse_timeline.py::_encode_frame()`

```python
for run_idx in range(0, len(runs) - window_pulses + 1):
    pulse = [_to_us(int(run.length), sample_rate) for run in runs[run_idx : run_idx + window_pulses]]
    if not _pulse_in_range(pulse, min_pulse_us, max_pulse_us):
        continue
    row = _candidate_row(
        pulse=pulse,
        start_sample=sample_offset + int(runs[run_idx].start),
        sample_rate=sample_rate,
    )
    if row is not None:
        rows.append(row)

header = bytearray([0xAA, 0x55, len(norm) & 0xFF, (len(norm) >> 8) & 0xFF])
for p in norm:
    payload.append(p & 0xFF)
    payload.append((p >> 8) & 0xFF)
```

- 为什么先看这段：它最能代表这组脚本的分工，前者负责挑候选 pulse 帧，后者负责把 pulse 帧变成 `rf_gateway` 能吃的 AA55 包。
- 真实参数和字段：`_candidate_row()` 的实参只有 `pulse`、`start_sample`、`sample_rate`，返回后保留下来的字段也只有 `candidate_wav_sec` 和 `pulse`；后半段 `_encode_frame()` 再把 `pulse` 编进 `AA55 + LEN + PAYLOAD + CRC`，没有额外 sideband 元数据。
- 默认值和重要性：候选窗口固定来自 `EV1527_FRAME_PULSES = 50`，而脚本默认筛选参数是 `--max-frames 64 --smooth-window 2 --min-run-samples 0 --min-pulse-us 80 --max-pulse-us 65535`；这段代码的重要性在于它把“候选筛选”和“协议封装”两层边界写死了，reviewer 质疑的占位代码必须替换成这里这组真实调用才能支撑文档结论。

## 7. `rf_gateway` 用户态实现

### 4. 最后一眼只看代码

这一组兼容标题直接挂到后文的详细文件段上，便于从旧索引跳到现行正文。

### 7.1 `linux_app/main.c`

### 4.1 `linux_app/main.c`

| 函数 | 作用 | 备注 |
| --- | --- | --- |
| `print_usage()` | 帮助输出 | 说明 `--rf-input -` 等选项 |
| `hamming24()` | 24-bit Hamming 距离 | 供稳定分组使用 |
| `stable_groups_decay()` | 淘汰过期分组 | 稳定化逻辑 |
| `stable_group_find()` | 查找相近 raw code 分组 | 按 Hamming 距离并阈值合并 |
| `stable_group_alloc()` | 分配分组槽位 | 最多 `RF_STABLE_GROUP_MAX` 组 |
| `stable_group_seed()` | 初始化新分组 | 第一次命中时调用 |
| `stable_group_update()` | 更新分组最佳码和信心 | 多帧稳定化 |
| `on_rf_frame()` | 解码、过滤、去重、输出 JSON 行 | `rf_gateway` 核心 |
| `main()` | 解析参数、打开输入、启动 `rf_epoll_run()` | 进程入口 |

阅读要点：

- 发布字段是 `addr`、`key`、`conf`、`src`、`pulses`、`seq`
- 当前 `pc_sim` 没有 `master` 那套顶层 JSON envelope，也没有 MQTT 标记位
- `src` 目前来自底层 C 解码器，值是 `"c"`

### 7.2 `linux_app/rf_source.c`

| 函数 | 作用 | 备注 |
| --- | --- | --- |
| `rf_source_open()` | 打开 RF 输入源 | 在 `pc_sim` 中只支持 `stdin` |
| `rf_source_close()` | 关闭输入 | Windows / Unix 分支处理不同 |

这份文件是“`pc_sim` 不是 `/dev/rf433` 真链”的最直接证据之一。

### 7.3 `linux_app/rf_decode.c`

| 函数 | 作用 | 备注 |
| --- | --- | --- |
| `rf_decode_frame()` | 调用 C 解码实现并写标准输出结构 | 成功时填 `addr/key/source/confidence/raw_code` |

### 7.4 `linux_app/rf_decode_c.c`

| 函数/区域 | 作用 | 备注 |
| --- | --- | --- |
| `build_runs_from_frame()` | 从脉冲长度重建高低电平 runs | 解码前准备 |
| `decode_best_from_runs()` | 在候选 runs 中搜索最佳 EV1527 解 | 核心评分逻辑 |
| `rf_decode_ev1527_c()` | 尝试两种相位并选择最佳结果 | 最终对外接口 |

### 7.5 `common/rf_protocol.h`

| 常量/结构 | 作用 | 备注 |
| --- | --- | --- |
| `RF_PROTO_SYNC0/1` | `0xAA 0x55` | RF 回放帧头 |
| `rf_frame_t` | pulse 数组 + 长度 | `rf_gateway` 解码输入 |
| `rf_proto_parser_t` | 协议解析状态机 | `rf_epoll` 侧会使用 |

#### 代表性代码摘录：`rf_gateway` 用户态实现明确是 replay 消费器，不是设备节点消费者

代码来源：`project2_pc_sim/linux_app/main.c::main()` / `on_rf_frame()` / `linux_app/rf_source.c::rf_source_open()`

```c
if (strcmp(argv[i], "--rf-input") == 0 && i + 1 < argc) {
    rf_input = argv[++i];
    if (!(rf_input[0] == '-' && rf_input[1] == '\0')) {
        fprintf(stderr, "--rf-input only supports '-' in pc_sim replay mode.\n");
        return 1;
    }
}

rc = rf_decode_frame(frame, &pkt);
printf(
    "{\"addr\":\"%s\",\"key\":\"%s\",\"conf\":%.2f,\"src\":\"%s\",\"pulses\":%u,\"seq\":%u}\n",
    pkt.addr, pkt.key, pkt.confidence, pkt.source, frame->len, ctx->frame_seq
);

if (path == NULL || path[0] == '\0' || (path[0] == '-' && path[1] == '\0')) {
    return 0;
}
return -1;
```

- 为什么先看这段：它把这一层的身份讲得最清楚，只认标准输入、做 C 解码、吐简化 JSON 行。
- 看代码时要注意什么：这里没有 `master` 那套 envelope 和 MQTT 标记位；如果你想找 `/dev/rf433`、ioctl、poll 语义，那应该去 `project2_master`。

## 8. fake 验证脚本

### 8.1 `python/fake_detection_stream_test.py`

| 函数 | 作用 | 备注 |
| --- | --- | --- |
| `parse_args()` | fake detection CLI | 默认输出到 `sim_data/local_validation/vision` |
| `_load_base_frame()` | 读取背景图或生成背景 | 不依赖真实视频 |
| `_make_synthetic_background()` | 生成合成背景 | 默认路径 |
| `_make_fake_detections()` | 生成假框 | 让 Qt 页面有内容可显示 |
| `_build_payloads()` | 生成 payload 列表 | 复用 bridge payload 合同 |
| `_write_outputs()` | 写 ndjson/jpg/manifest | 做离线产物验证 |
| `_serve_payloads()` | 开 TCP 服务 | 给 Qt Vision 页直接接 |
| `main()` | 入口 | 先写文件，再按需开服务 |

依赖：

- `numpy`
- `opencv-python-headless`

推荐解释器：

- `.\.venv\Scripts\python.exe`

### 8.2 `python/fake_event_record_test.py`

| 函数 | 作用 | 备注 |
| --- | --- | --- |
| `parse_args()` | event fake CLI | 支持 `rf_confirmed` / `manual_record` |
| `_default_rf_line()` | 构造默认 RF JSON 行 | 便于脱离真实输入验证 |
| `_parse_rf_json_line()` | 校验 RF JSON 行字段 | 核对 `addr/key/conf/src` 最小合同 |
| `_build_event_payload()` | 生成 `event.json` 内容 | 本地占位协议 |
| `_build_record_done_payload()` | 生成 `record_done.json` 内容 | 本地占位协议 |
| `_write_artifacts()` | 写所有产物 | `event.json` / `record_done.json` / `mock_record.txt` |
| `main()` | 入口 | 出错时返回码 `2` |

依赖：

- Python 标准库即可

推荐解释器：

- 为了命令统一，仍建议用 `.\.venv\Scripts\python.exe`

#### 代表性代码摘录：fake 脚本复用真实合同，但只做离线验证

代码来源：`project2_pc_sim/python/fake_detection_stream_test.py::_build_payloads()` / `python/fake_event_record_test.py::_build_event_payload()`

```python
state.update_detections(frame, boxes, classes, scores, fps)
payload = bridge_server._build_payload(state.get_snapshot(include_frame=True))
payload["simulation"] = {
    "mode": "fake_detection_stream_test",
    "frame_index": frame_index,
    "frame_count": frame_count,
}

payload: dict[str, Any] = {
    "schema": "project2_pc_sim/local_event_validation/v1",
    "event_id": event_id,
    "created_at": created_at,
    "trigger": args.trigger,
}
payload["rf_confirmed"] = _parse_rf_json_line(rf_line)
```

- 为什么先看这段：它说明 fake 脚本不是随便伪造数据，而是明确复用 Vision bridge payload 和 RF JSON 最小合同来做离线验证。
- 看代码时要注意什么：这些脚本只验证字段形状和页面可消费性，不代表真实 recorder、MQTT 或板端 side effect 已经存在。

## 9. 最值得优先阅读的 12 个点

如果你时间很少，只看下面这些：

1. `qt_gui/app/main.cpp::main`
2. `qt_gui/app/main_window.cpp::setupRuntime`
3. `qt_gui/rf/rf_gateway_client.cpp::start`
4. `qt_gui/rf/rf_gateway_client.cpp::prepareRealtimeTimeline`
5. `qt_gui/rf/rf_gateway_client.cpp::startGatewayWithRealtimeInput`
6. `qt_gui/rf/rf_gateway_client.cpp::handleGatewayLine`
7. `python/wav_to_pulses.py::extract_frames`
8. `python/replay_pulse_timeline.py::_encode_frame`
9. `linux_app/main.c::on_rf_frame`
10. `qt_gui/vision/vision_page.cpp::applyVisionBridgePayload`
11. `python/wsl_vision_bridge_server.py::_build_payload`
12. `vision/rknn_pipeline.py::_load_model`

看完这 12 个点，基本就能把 `pc_sim` 当前保留下来的真实工作面描述准确。
