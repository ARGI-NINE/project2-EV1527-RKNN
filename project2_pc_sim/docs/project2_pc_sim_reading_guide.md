# project2_pc_sim 阅读指南

这份指南面向两类人：

- 想把 `project2_pc_sim` 跑起来做离线验证的人
- 想快速读懂 `pc_sim` 和 `master` 边界的人

它不是板端设计文档；它帮助你避免把 `pc_sim` 当成 `project2_master` 去读。

## 0. 快速索引 / 阅读地图

如果你只想先抓边界，先看下方“1. 先建立正确心智模型”和“2. 推荐阅读顺序”；这两个部分就是当前版对旧入口的兼容落点。

## 1. 契约 / 职责

`project2_pc_sim` 只负责离线 RF 回放链和 WSL -> Windows Vision bridge 展示链，不代替板端 runtime。

## 2. 生命周期

当前详细生命周期被拆到下方“3. 从启动入口往里读”“4. RF 链怎么读”“5. Vision 链怎么读”“6. fake 脚本怎么读”。

## 3. 边界条件 / ABI

最重要的边界仍是：RF 不走 `/dev/rf433` 真链，Vision 不在 Windows Qt 本地跑模型，waveform 也不是原始 pulse 真值面。

## 4. 最后一眼只看代码

如果只想抓入口，优先回看 `qt_gui/app/main.cpp`、`qt_gui/rf/rf_gateway_client.cpp`、`linux_app/main.c`、`qt_gui/vision/vision_page.cpp` 和 `python/wsl_vision_bridge_server.py`。

## 1. 先建立正确心智模型

读代码前，请先把下面 5 句话钉死：

1. `project2_pc_sim` 不是板端 runtime。
2. RF 当前是 `WAV -> 候选帧 -> AA55 回放流 -> rf_gateway(stdin) -> Qt`。
3. Vision 当前是 `WSL bridge -> TCP JSON -> Windows Qt`。
4. 这里没有 `/dev/rf433` 真链，也没有真实 MQTT / RTSP 验收链。
5. `project2_master` 才是主实现和主验收面。

如果这 5 句不先立住，后面很容易把 Qt 页面的展示状态误读成“真实硬件链已经打通”。

## 2. 推荐阅读顺序

### 第一轮：先抓边界，不钻细节

按这个顺序读：

1. [../README.md](../README.md)
2. [pc_sim_architecture.md](pc_sim_architecture.md)
3. [ev1527_truth_mapping.md](ev1527_truth_mapping.md)
4. [project2_pc_sim_function_index.md](project2_pc_sim_function_index.md)

这一轮的目标只有一个：

- 看清 `pc_sim` 到底保留了哪些链
- 看清哪些链是明确没有的

### 第二轮：按子系统读代码

如果你更关心“怎么跑起来”，先读：

1. `qt_gui/app/main.cpp`
2. `qt_gui/rf/rf_gateway_client.cpp`
3. `python/wav_to_pulses.py`
4. `python/replay_pulse_timeline.py`
5. `linux_app/main.c`
6. `qt_gui/vision/vision_page.cpp`
7. `python/wsl_vision_bridge_server.py`

如果你更关心“UI 里每个页面到底接了什么”，先读：

1. `qt_gui/app/main_window.cpp`
2. `qt_gui/rf/rf_status_page.cpp`
3. `qt_gui/vision/vision_page.cpp`
4. `qt_gui/log/system_log_page.cpp`
5. `qt_gui/core/dashboard_backend.cpp`

如果你更关心“和 master 差在哪”，交叉读：

1. `project2_pc_sim/README.md`
2. `project2_master/README.md`
3. `project2_pc_sim/linux_app/main.c`
4. `project2_master/linux_app/main.c`
5. `project2_pc_sim/qt_gui/vision/vision_page.cpp`
6. `project2_master/qt_gui/vision/vision_runtime.cpp`

## 3. 从启动入口往里读

### 3.1 Qt 启动入口

从 `qt_gui/app/main.cpp` 开始。这里能立刻回答几个最重要的问题：

- Qt 支持哪些启动参数
- Vision 默认是不是启用
- Windows 侧 Python 怎么传给 RF 链

当前事实是：

- `--gateway`、`--wav-input`、`--python-bin` 都是外部传入路径
- `--vision-port` 默认是 `0`
- `--vision-port <= 0` 时，Vision bridge 在 Qt 侧被视为禁用
- 当前没有 `--disable-vision` 这个开关

#### 代码锚点：fixed-chain 的开关都在 Qt CLI 参数里

代码来源：`project2_pc_sim/qt_gui/app/main.cpp::main()`

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
```

- 为什么先看这段：它能最快确认 `pc_sim` 的固定链不是仓库内部写死，而是 Qt 通过外部路径把 RF 和 Vision 两条链拼起来。
- 真实参数和默认值：`--gateway`、`--wav-input`、`--python-bin` 都只声明了值名 `path`，没有 CLI 默认值，对应 `AppOptions.gatewayPath / wavPath / pythonBin` 默认就是空字符串；`--wav-loop` 是布尔开关，默认 `false`；`--wav-speed` 默认 `1.0`；`--vision-host` 默认 `127.0.0.1`；`--vision-port` 默认 `0`，明确表示不启用 bridge。
- 真实字段和重要性：`parser.process(app)` 后，Qt 只把结果写进 `AppOptions.gatewayPath / wavPath / wavLoop / wavSpeed / pythonBin / visionHost / visionPort`；固定链能否拉起 `rf_gateway`、WAV 是否循环、Vision bridge 是否接入，都是后续模块围绕这组字段分流，不存在别的隐藏启动入口。

### 3.2 主窗口装配

然后看 `qt_gui/app/main_window.cpp`：

- `setupUi()` 创建三个页面
- `setupRuntime()` 只会启动 RF 客户端
- Vision 页不是在主窗口里手搓线程，而是页面自己管理 TCP 连接

这里能读出一个非常重要的事实：

`pc_sim` 的 Qt 主窗口不是一个板端 runtime 容器，它更像一个展示壳和流程编排器。

#### 代码锚点：主窗口只拉起 RF 客户端，Vision 页自己管理 bridge

代码来源：`project2_pc_sim/qt_gui/app/main_window.cpp::setupUi()` / `setupRuntime()`

```cpp
rfPage_ = new RFStatusPage(&backend_, tabs);
visionPage_ = new VisionPage(&backend_, options_, tabs);
logPage_ = new SystemLogPage(&backend_, tabs);

tabs->addTab(rfPage_, QStringLiteral("RF Status"));
tabs->addTab(visionPage_, QStringLiteral("Vision"));
tabs->addTab(logPage_, QStringLiteral("System Log"));

void MainWindow::setupRuntime() {
    backend_.addLog(QStringLiteral("INFO"), QStringLiteral("SYSTEM"), QStringLiteral("Qt5 frontend started"));
    rfClient_.start();
    updateStatusBar();
}
```

- 为什么先看这段：它能直接说明 Qt 主窗口只是装配页面和拉起 RF 客户端，不是把 Vision runtime 也塞进同一套启动逻辑。
- 看代码时要注意什么：`setupRuntime()` 里没有 `vision pipeline` 的本地启动代码；Vision 页的网络连接和重连逻辑都在页面类自己内部。

## 4. RF 链怎么读

### 4.1 先读 `RFGatewayClient`

`qt_gui/rf/rf_gateway_client.cpp` 是 RF 链的总入口。建议按这个顺序看函数：

1. `start()`
2. `startGatewayProcess()`
3. `prepareRealtimeTimeline()`
4. `onPrepFinished()`
5. `startGatewayWithRealtimeInput()`
6. `handleGatewayLine()`
7. `parseGatewayEventLine()`

读完后你会明确：

- Qt 并不是“直接喂 WAV 给 rf_gateway”
- Qt 先调用 `wav_to_pulses.py`
- 再调用 `replay_pulse_timeline.py`
- 再把 replay 的 `stdout` 接给 `rf_gateway` 的 `stdin`

#### 代码锚点：Qt 先做预处理，再把 replay stdout 接到 gateway stdin

代码来源：`project2_pc_sim/qt_gui/rf/rf_gateway_client.cpp::startGatewayProcess()` / `startGatewayWithRealtimeInput()`

```cpp
const QString gatewayPath = resolveGatewayPath();
if (options_.wavPath.trimmed().isEmpty()) {
    failStart(QStringLiteral("--wav-input is required in fixed-chain mode"));
    return;
}
pendingGatewayPath_ = gatewayPath;
if (!prepareRealtimeTimeline()) {
    return;
}

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
```

- 为什么先看这段：它把 `pc_sim` RF 链的真实编排顺序写得非常明确，不再停留在“WAV 回放”这种口头概括。
- 看代码时要注意什么：`rf_gateway` 被强制成 `--rf-input -`；`stable-repeat` 和 `min-publish-confidence` 是 Qt 传入的运行时策略，不是 `rf_gateway` 默认行为。

### 4.2 再读 `wav_to_pulses.py`

这个脚本只负责 Stage-1 候选帧提取，重点看：

- `extract_frames()`
- `_extract_candidate_frames()`
- `_candidate_row()`
- `write_outputs()`

你要抓住的不是所有细节，而是职责边界：

- 它从 WAV 中提取看起来像 EV1527 的候选脉冲帧
- 产出 `pulse_runtime.txt` 和 `pulse_runtime.json`
- 它不决定最终业务发布，不负责重复帧稳定化，不负责 Qt 展示

#### 代码锚点：候选帧只是滑窗筛选，不是最终业务事件

代码来源：`project2_pc_sim/python/wav_to_pulses.py::_extract_candidate_frames()`

```python
runs = _build_runs(decoder_mod, segment, smooth_window, min_run_samples)
if not runs:
    return []

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
```

- 为什么先看这段：它把 Stage-1 的真实职责暴露得很干净，就是从 run-length 窗口里挑“像 EV1527”的候选脉冲帧。
- 真实参数和字段：`_candidate_row()` 在这里吃进去的只有 `pulse`、`start_sample=sample_offset + int(runs[run_idx].start)` 和 `sample_rate`，真正写出的字段也只有 `candidate_wav_sec` 与 `pulse`；这里没有 `addr`、`key`、`conf` 之类的业务字段。
- 默认值和重要性：这个调用点本身没有额外默认值，窗口宽度固定来自 `EV1527_FRAME_PULSES = 50`；脚本 CLI 的默认筛选参数是 `--max-frames 64 --smooth-window 2 --min-run-samples 0 --min-pulse-us 80 --max-pulse-us 65535`，所以这段代码的重要性就在于它严格限定了 Stage-1 只做候选筛选，不做最终发布判定。

### 4.3 再读 `replay_pulse_timeline.py`

重点看：

- `_load_frames()`
- `_encode_frame()`
- `main()`

这一步要确认两件事：

1. 回放是按 `candidate_wav_sec` 重新排时间的
2. 输出是 AA55 协议二进制帧，不是文本 sideband

也就是说，当前 `pc_sim` 的 replay 链里没有 `FRAME_TS` / `FRAME_META` 附加侧带协议。Qt 看到的 `wav_sec` 是后续自己回填的。

#### 代码锚点：replay 只负责按时间线发 AA55 二进制包

代码来源：`project2_pc_sim/python/replay_pulse_timeline.py::_encode_frame()` / `main()`

```python
header = bytearray([0xAA, 0x55, len(norm) & 0xFF, (len(norm) >> 8) & 0xFF])
payload = bytearray()
for p in norm:
    payload.append(p & 0xFF)
    payload.append((p >> 8) & 0xFF)

crc = _crc8(bytes(header[2:]) + bytes(payload))
return bytes(header + payload + bytes([crc]))

rel_t = max(0.0, row["start_sec"] - base)
pkt = _encode_frame(row["pulse"])
timeline.append({"idx": int(row.get("idx", idx)), "wav_sec": float(row["start_sec"]), "rel_sec": rel_t, "packet": pkt})
```

- 为什么先看这段：它能一眼验证 replay 输出真的是协议帧，而不是 reviewer 提到的那种“文档说有 sideband、代码里其实没有”的情况。
- 看代码时要注意什么：`wav_sec` 只作为时间排序依据存在于脚本内部；发到 `stdout` 的只有二进制 `packet`，没有额外文本元数据。

### 4.4 最后读 `linux_app/main.c`

重点函数：

- `main()`
- `on_rf_frame()`
- `stable_group_find()`
- `stable_group_update()`

读这个文件时最容易犯的误解是：

- 误以为它在模拟 `/dev/rf433` 驱动消费

实际不是。当前 `pc_sim/linux_app/rf_source.c` 明确把输入限制成：

```text
--rf-input -
```

也就是只接受标准输入。它消费的是回放流，不是真实设备节点。

#### 代码锚点：`rf_gateway` 在 `pc_sim` 里明确拒绝设备路径

代码来源：`project2_pc_sim/linux_app/main.c::main()` / `linux_app/rf_source.c::rf_source_open()`

```c
if (strcmp(argv[i], "--rf-input") == 0 && i + 1 < argc) {
    rf_input = argv[++i];
    if (!(rf_input[0] == '-' && rf_input[1] == '\0')) {
        fprintf(stderr, "--rf-input only supports '-' in pc_sim replay mode.\n");
        return 1;
    }
}

int rf_source_open(const char *path) {
    if (path == NULL || path[0] == '\0' || (path[0] == '-' && path[1] == '\0')) {
        return 0;
    }
    return -1;
}
```

- 为什么先看这段：它是 `pc_sim` 不等于 `/dev/rf433` 真链的硬证据，直接堵住“是不是在模拟驱动消费”的误读。
- 看代码时要注意什么：这里只接受标准输入；哪怕你传了文件路径或设备路径，也会被显式拒绝。

### 4.5 RF 页面如何显示

最后再看：

- `qt_gui/rf/rf_status_page.cpp`
- `qt_gui/core/rf_utils.cpp`

这里要特别记住：

- 页面上的 `WAV Sec` 是回放候选时间
- 页面上的 waveform 是 `addr -> 合成 EV1527 波形`
- 这不是原始 pulse 逐样本验证视图

如果你要验证真实脉冲语义，请回到：

- `pulse_runtime.json`
- `rf_gateway` JSON 输出
- `ev1527_truth_mapping.md`

#### 代码锚点：Qt 会本地回填 `wav_sec`，并按地址合成展示波形

代码来源：`project2_pc_sim/qt_gui/rf/rf_gateway_client.cpp::handleGatewayLine()`

```cpp
if (event.frameSeq > 0 && event.candidateWavSec < 0.0) {
    const int idx = static_cast<int>(event.frameSeq);
    if (replayWavSecByIdx_.contains(idx)) {
        event.candidateWavSec = replayWavSecByIdx_.value(idx, -1.0);
    }
}

const uint32_t rawCode = parseRawCode(event.address);
backend_->addRFEvent(event);
backend_->updateWaveform(buildWaveformFromRawCode(rawCode));
```

- 为什么先看这段：它把 RF 页面上两个最容易被误读的字段都说明白了，`wav_sec` 是 Qt 回填，waveform 是按地址合成。
- 看代码时要注意什么：这个页面不是原始脉冲示波器；如果要看真脉冲，只能回到 `pulse_runtime.json` 和 `rf_gateway` 输出本身。

## 5. Vision 链怎么读

### 5.1 先读 Qt Vision 页面

从 `qt_gui/vision/vision_page.cpp` 开始，顺序如下：

1. `setupVisionBridge()`
2. `attemptVisionBridgeConnect()`
3. `onVisionBridgeReadyRead()`
4. `handleVisionBridgeLine()`
5. `applyVisionBridgePayload()`
6. `refresh()`

这样读最容易看清 Qt 在 Vision 链里的真实角色：

- 只是 TCP 客户端
- 只是 payload 解析者
- 只是 UI 投影层

它不负责：

- 打开视频源
- 解析 WSL 路径
- 跑 RKNN 模型

#### 代码锚点：Qt Vision 页只解 payload，不负责模型与采集

代码来源：`project2_pc_sim/qt_gui/vision/vision_page.cpp::applyVisionBridgePayload()`

```cpp
if (obj.contains(QStringLiteral("fps"))) {
    remoteFps_ = obj.value(QStringLiteral("fps")).toDouble(remoteFps_);
}
if (obj.contains(QStringLiteral("camera_online"))) {
    remoteCameraOnline_ = obj.value(QStringLiteral("camera_online")).toBool(remoteCameraOnline_);
}
if (obj.contains(QStringLiteral("model_loaded"))) {
    remoteModelLoaded_ = obj.value(QStringLiteral("model_loaded")).toBool(remoteModelLoaded_);
}

const QByteArray jpegData = QByteArray::fromBase64(encoded.toLatin1());
if (decoded.loadFromData(jpegData, "JPG") || decoded.loadFromData(jpegData, "JPEG")) {
    remoteFrameImage_ = decoded;
}
```

- 为什么先看这段：它精确展示了 Qt Vision 页只是在吃 bridge 发来的字段和 JPEG，不负责任何底层视觉运行时。
- 看代码时要注意什么：页面端读取的是 `fps/camera_online/model_loaded/frame_jpeg_b64` 这些合同字段；字段长什么样，以 bridge 端组包为准。

### 5.2 再读 WSL bridge

然后读 `python/wsl_vision_bridge_server.py`，重点看：

- `_resolve_source()`
- `_resolve_model_path()`
- `_build_payload()`
- `main()`

这一步重点确认：

- `--source` 是强制参数
- `--model` 不传时会默认找 `project2_pc_sim` 父目录下的 `yolov5s.onnx`
- 发送给 Qt 的是每行一个 JSON 快照
- 画面是用 JPEG base64 编码进 payload

#### 代码锚点：Vision 合同的真实来源是 bridge 端组包

代码来源：`project2_pc_sim/python/wsl_vision_bridge_server.py::_build_payload()` / `main()`

```python
return {
    "ts": time.time(),
    "fps": float(snapshot.get("fps", 0.0)),
    "camera_online": bool(snapshot.get("camera_online", False)),
    "model_loaded": bool(snapshot.get("model_loaded", False)),
    "frame_count": int(snapshot.get("frame_count", 0)),
    "error": str(snapshot.get("error_msg", "") or ""),
    "detections": det_structs,
    "frame_width": frame_w,
    "frame_height": frame_h,
    "frame_jpeg_b64": _encode_frame(overlay),
}

payload = _build_payload(pipeline.state.get_snapshot(include_frame=True))
blob = (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
```

- 为什么先看这段：它把 Qt Vision 页真正依赖的字段合同一次性列全了，也直接说明传输格式是逐行 JSON。
- 看代码时要注意什么：只要 bridge 还在按这套字段发包，Qt 页就能工作；这和 `master` 板端的本地 `VisionRuntime` 是两套不同实现路径。

### 5.3 最后读视觉线程管线

再读 `vision/rknn_pipeline.py`，重点看：

- `VisionPipeline.start()`
- `_capture_loop()`
- `_inference_loop()`
- `_postprocess_loop()`
- `_load_model()`

这一步你应该得出一个务实结论：

当前 `pc_sim` 的视觉链重点不是“复刻 master 的本地运行时结构”，而是“在 WSL 里把模型/视频处理跑起来，并把结果通过稳定的 bridge 协议送到 Windows Qt”。

## 6. fake 脚本怎么读

### 6.1 `fake_detection_stream_test.py`

先读它是为了理解两个东西：

1. Qt Vision 页现在真正依赖的 payload 结构是什么
2. 哪些视觉验证可以不依赖真实 RKNN 环境

建议重点看：

- `_build_payloads()`
- `_write_outputs()`
- `_serve_payloads()`

这个脚本非常适合做“Qt Vision 页还能不能显示”的最低成本验证。

### 6.2 `fake_event_record_test.py`

这个脚本不在运行链中，但它明确了一个很重要的本地交接面：

- `event.json`
- `record_done.json`
- `mock_record.txt`

建议重点看：

- `_parse_rf_json_line()`
- `_build_event_payload()`
- `_build_record_done_payload()`

它告诉你：

- `pc_sim` 现在能本地约定什么样的触发形状
- 但这不等于真实 recorder、MQTT 或 master-side side effect 已存在

## 7. 读文档时最容易踩的坑

### 坑 1：把 UI 文案当成真实运行能力

例如 `System Log` 里有 MQTT 区块，不代表已经有真实 MQTT 上报链。

### 坑 2：把 Vision 页当成“Windows 侧本地视觉运行时”

当前不是。Windows 侧只负责 bridge 消费和显示。

### 坑 3：把 RF waveform 当成真实原始脉冲

当前不是。它是根据 `addr` 合成出来的展示波形。

### 坑 4：把 `rf_gateway` 当成 `/dev/rf433` 用户态消费器

在 `pc_sim` 里不是。它是回放流消费者。

### 坑 5：把 fake 脚本当成验收脚本

它们只是离线合同验证脚本，不是最终验收脚本。

## 8. 对离线验证使用者的最短路径

如果你的目标是“最快确认工程还活着”，按下面做：

1. 用仓库内 `.\.venv\Scripts\python.exe` 跑 `python/fake_detection_stream_test.py`
2. 确认 `sim_data/local_validation/vision` 里生成了 `manifest.json` 和 `annotated_frame_*.jpg`
3. 用 `--serve --port 17656` 起 fake bridge
4. 启 Qt，显式传 `--vision-port 17656`
5. 再用一份 WAV 启 RF 回放链

这样你能同时验证：

- Qt 是否能启动
- RF 链是否能形成事件
- Vision 页是否能吃桥接 payload

而不用先折腾真实 WSL RKNN 环境。

## 9. 对代码阅读者的最短路径

如果你的目标是“30 分钟内搞明白当前工程边界”，按下面读：

1. `README.md`
2. `qt_gui/app/main.cpp`
3. `qt_gui/rf/rf_gateway_client.cpp`
4. `linux_app/main.c`
5. `qt_gui/vision/vision_page.cpp`
6. `python/wsl_vision_bridge_server.py`
7. `project2_master/README.md`

读完之后，你应该能用一句话准确描述它：

**`project2_pc_sim` 是 `project2_master` 的离线配套模拟工程，只保留 RF WAV 回放链和 WSL vision bridge 链，用来验证字段合同、UI 消费和局部脚本行为，而不是替代 master 板端运行时。**
