# project2_pc_sim 架构说明

这份文档只描述 `project2_pc_sim` 当前保留的可运行架构，不描述 `project2_master` 的板端真实 runtime。

下面 6 个英文二级标题按 `HEAD` 旧文档的原字符串补回，作为兼容旧骨架；后面的现有中文正文保持不删，只在它前面补一层阅读地图。

## Positioning

旧骨架里的定位仍然成立：`project2_pc_sim` 是离线模拟 / 验证工程，不是把 `project2_master` 原样搬到 PC。当前正文下方的 `## 1. 架构定位` 只是把同一个定位讲得更具体。

## RF Chain

旧骨架里的 RF 链仍对应当前正文的 `## 3. RF 架构`：`WAV -> wav_to_pulses.py -> replay_pulse_timeline.py -> rf_gateway(stdin) -> Qt RF / Log`。补回旧标题只是为了兼容阅读顺序，不改变下文现有的角色拆分和 payload 合同。

## Vision Chain

旧骨架里的 Vision 链仍对应当前正文的 `## 4. Vision 架构`：`WSL source + model -> wsl_vision_bridge_server.py -> TCP JSON -> Windows Qt Vision page`。这说明 `pc_sim` 仍是 bridge consumer，而不是 board-side local runtime。

## Qt Frontend Surface

当前 Qt 前端仍只有 `RF Status`、`Vision`、`System Log` 三个展示面；对应下文 `## 6. Qt 在整个架构中的职责`。补回这个标题是为了保留 `HEAD` 旧文档的 UI 外壳骨架。

## Cleanup Boundary

旧骨架里的清理边界仍可直接映射到下文 `## 5. fake 验证层在架构里的位置` 和 `## 8. 当前明确不属于架构的一部分`：直接 Qt 本地 MP4 播放、独立 profiling 产物、验证残留脚本都不应被误写成保留架构的一部分。

## Acceptance Boundary

旧骨架里的验收边界仍和下文 `## 9. 与 project2_master 的架构关系` 一致：`project2_master` 是真实 runtime 和验收基线，`project2_pc_sim` 只是辅助模拟和离线验证环境。

## 1. 架构定位

`project2_pc_sim` 的目标不是“把 master 整体搬到 PC 上”，而是：

- 在 PC 上保留一条可重复的 RF 回放链
- 在 WSL + Windows 上保留一条可观察的视觉 bridge 链
- 让 Qt 页面、字段合同和局部脚本可以离线验证

它属于“辅助模拟工程”，不是“主运行时工程”。

## 2. 当前总览

当前整个工程只保留两条业务相关链和一个 UI 展示壳：

```text
                 +---------------------------+
                 |      Windows Qt GUI       |
                 | RF Status / Vision / Log  |
                 +------------+--------------+
                              |
          +-------------------+-------------------+
          |                                       |
          v                                       v
  RF WAV replay chain                    WSL vision bridge chain
```

更具体地说：

```text
RF:
WAV
-> wav_to_pulses.py
-> replay_pulse_timeline.py
-> rf_gateway(stdin)
-> Qt RF / Log

Vision:
WSL source + model
-> VisionPipeline
-> wsl_vision_bridge_server.py
-> TCP JSON
-> Qt Vision / Log
```

## 3. RF 架构

### 3.1 数据流

```text
WAV file
-> Stage-1 candidate extraction
-> pulse_runtime.json / pulse_runtime.txt
-> timed replay by candidate_wav_sec
-> AA55 binary packets on stdout
-> rf_gateway --rf-input -
-> decoded JSON lines
-> Qt RF event projection
```

### 3.2 角色拆分

| 组件 | 角色 | 备注 |
| --- | --- | --- |
| `python/wav_to_pulses.py` | 从 WAV 抽候选 EV1527 脉冲帧 | 不做最终业务发布 |
| `python/replay_pulse_timeline.py` | 按原始候选时间回放脉冲帧 | 产出 AA55 二进制流 |
| `linux_app/rf_gateway` | 解码回放流并输出 JSON 行 | 输入固定是 `stdin` |
| `qt_gui/rf/rf_gateway_client.cpp` | 总编排器 | 管进程、管路径、管 JSON 解析、管 `wav_sec` 回填 |
| `qt_gui/rf/rf_status_page.cpp` | UI 展示 | 展示最近解码、事件历史、合成 waveform |

### 3.3 关键设计取舍

#### 取舍 1：RF 输入不接 `/dev/rf433`

`pc_sim` 中的 `rf_gateway` 是回放流消费者，而不是板端驱动消费者。

证据在：

- `linux_app/rf_source.c`
- `linux_app/main.c`

当前 `--rf-input` 只支持：

```text
-
```

也就是标准输入。

#### 取舍 2：Qt 自己编排预处理与回放

RF 链不是单个进程完成的，而是由 Qt 进程编排多个子进程：

1. `wav_to_pulses.py`
2. `replay_pulse_timeline.py`
3. `rf_gateway`

这样做的结果是：

- UI 能更方便地管理启动错误
- Qt 能提前读 `pulse_runtime.json`
- Qt 能按 `seq` 回填 `wav_sec`

来源：`project2_pc_sim/qt_gui/rf/rf_gateway_client.cpp`，函数：`RFGatewayClient::startGatewayProcess()`，作用：固定走 `WAV -> 预处理 -> replay -> rf_gateway(stdin)` 这条链，而不是让 Qt 直接打开任意 RF 输入。

```cpp
void RFGatewayClient::startGatewayProcess() {
    if (backend_ == nullptr) {
        return;
    }
    if (gatewayProcess_.state() == QProcess::Running) {
        return;
    }

    lastStartError_.clear();
    firstRfTimeoutMs_ = kMinFirstRfTimeoutMs;

    auto failStart = [this](const QString &msg) {
        lastStartError_ = msg;
        if (backend_ != nullptr) {
            backend_->addLog(QStringLiteral("ERROR"), QStringLiteral("RF"), msg);
        }
    };

    const QString gatewayPath = resolveGatewayPath();
    if (gatewayPath.isEmpty()) {
        failStart(QStringLiteral("rf_gateway executable not found; pass --gateway with a valid path"));
        return;
    }

    if (options_.wavPath.trimmed().isEmpty()) {
        failStart(QStringLiteral("--wav-input is required in fixed-chain mode"));
        return;
    }

    QString replaySpeedError;
    if (!resolveReplaySpeed(options_.wavSpeed, nullptr, &replaySpeedError)) {
        failStart(replaySpeedError);
        return;
    }

    pendingGatewayPath_ = gatewayPath;
    if (!prepareRealtimeTimeline()) {
        return;
    }
}
```

这段入口代码说明了 `pc_sim` 的 RF 链是“固定编排链”而不是“自由注入链”：

- 没有 `--wav-input` 就直接失败。
- `pendingGatewayPath_ = gatewayPath` 后并不会立刻启动 `rf_gateway`，而是先去做 `prepareRealtimeTimeline()`。
- 所以 Qt 在这里承担的是 orchestrator，不是简单地“拉起一个可执行文件”。

来源：`project2_pc_sim/qt_gui/rf/rf_gateway_client.cpp`，函数：`RFGatewayClient::startGatewayWithRealtimeInput()`，作用：把 replay 进程和 `rf_gateway --rf-input -` 真正接起来。

```cpp
void RFGatewayClient::startGatewayWithRealtimeInput(const QString &gatewayPath) {
    if (backend_ == nullptr) {
        return;
    }

    auto failStart = [this](const QString &msg) {
        lastStartError_ = msg;
        if (backend_ != nullptr) {
            backend_->addLog(QStringLiteral("ERROR"), QStringLiteral("RF"), msg);
        }
    };

    const QString replayScript = resolveTimelineReplayPath();
    if (replayScript.isEmpty()) {
        failStart(QStringLiteral("python/replay_pulse_timeline.py not found"));
        return;
    }

    const QString pythonBin = resolvePythonBin();
    if (pythonBin.isEmpty()) {
        failStart(QStringLiteral("Python interpreter not found; pass --python-bin"));
        return;
    }

    double replaySpeed = 0.0;
    QString replaySpeedError;
    if (!resolveReplaySpeed(options_.wavSpeed, &replaySpeed, &replaySpeedError)) {
        failStart(replaySpeedError);
        return;
    }

    if (!gatewayConnected_) {
        QObject::connect(&gatewayProcess_, &QProcess::readyReadStandardOutput, context_, [this]() {
            onGatewayStdout();
        });
        QObject::connect(&gatewayProcess_, &QProcess::readyReadStandardError, context_, [this]() {
            onGatewayStderr();
        });
        QObject::connect(
            &gatewayProcess_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            context_,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                onGatewayFinished(exitCode, exitStatus);
            }
        );
        gatewayConnected_ = true;
    }

    if (!replayConnected_) {
        QObject::connect(&replayProcess_, &QProcess::readyReadStandardOutput, context_, [this]() {
            onReplayStdout();
        });
        QObject::connect(&replayProcess_, &QProcess::readyReadStandardError, context_, [this]() {
            onReplayStderr();
        });
        QObject::connect(
            &replayProcess_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            context_,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                onReplayFinished(exitCode, exitStatus);
            }
        );
        replayConnected_ = true;
    }

    QStringList gatewayArgs;
    gatewayArgs << QStringLiteral("--rf-input") << QStringLiteral("-");
    gatewayArgs << QStringLiteral("--stable-repeat") << QStringLiteral("1");
    gatewayArgs << QStringLiteral("--min-publish-confidence") << QStringLiteral("0.92");

    gatewayStdoutBuffer_.clear();
    gatewayStderrBuffer_.clear();
    replayStderrBuffer_.clear();

    gatewayProcess_.setProgram(gatewayPath);
    gatewayProcess_.setArguments(gatewayArgs);
    gatewayProcess_.setProcessChannelMode(QProcess::SeparateChannels);

    replayProcess_.setProgram(pythonBin);
    QStringList replayArgs = {
        replayScript,
        QStringLiteral("--pulse-json"),
        runtimePulseJsonPath_,
        QStringLiteral("--speed"),
        QString::number(replaySpeed, 'f', 3)
    };
    if (options_.wavLoop) {
        replayArgs << QStringLiteral("--loop");
    }
    replayProcess_.setArguments(replayArgs);
    replayProcess_.setProcessChannelMode(QProcess::SeparateChannels);

    gatewayProcess_.start();
    if (!gatewayProcess_.waitForStarted(4000)) {
        failStart(QString("rf_gateway startup failed: %1").arg(gatewayProcess_.errorString()));
        return;
    }

    replayProcess_.start();
    if (!replayProcess_.waitForStarted(4000)) {
        failStart(
            QString("Realtime WAV replay startup failed: %1 (python=%2)")
                .arg(replayProcess_.errorString(), pythonBin)
        );
        gatewayProcess_.terminate();
        (void)gatewayProcess_.waitForFinished(1000);
        return;
    }

    lastStartError_.clear();
    awaitingFirstRf_ = true;
    firstRfTimeoutMs_ = computeFirstRfTimeoutMs(replaySpeed);
    firstRfTimer_.start(firstRfTimeoutMs_);

    backend_->updateSerialStatus(true, QStringLiteral("proc://rf_gateway/stdin"));
    backend_->addLog(
        QStringLiteral("INFO"),
        QStringLiteral("RF"),
        QString("Starting realtime WAV replay: frames=%1 speed=%2 loop=%3 first_event_timeout_ms=%4")
            .arg(timelineFrameCount_)
            .arg(QString::number(replaySpeed, 'f', 3))
            .arg(options_.wavLoop ? QStringLiteral("on") : QStringLiteral("off"))
            .arg(firstRfTimeoutMs_)
    );
    backend_->addLog(
        QStringLiteral("INFO"),
        QStringLiteral("RF"),
        QString("Starting rf_gateway: %1 %2").arg(gatewayPath, gatewayArgs.join(' '))
    );
}
```

这里把数据路径写得非常死：

- `gatewayArgs` 里明确是 `--rf-input -`，也就是只吃标准输入。
- `replayProcess_` 输出的是 AA55 二进制流，`gatewayProcess_` 则把它当 stdin 消费。
- Qt 记录的串口状态其实是一个进程内伪路径：`proc://rf_gateway/stdin`，它不是板侧 `/dev/rf433`。

#### 取舍 3：waveform 是展示辅助，不是真值面

Qt RF 页显示的波形来自：

```text
event.address
-> parseRawCode()
-> buildWaveformFromRawCode()
```

这是一种“把解码结果可视化”的方式，不是“保留原始脉冲验收视图”的方式。

### 3.4 当前 RF 输出合同

`linux_app/main.c::on_rf_frame()` 成功时会输出单行 JSON，大致形状如下：

```json
{"addr":"0x35A1BC","key":"12","conf":0.94,"src":"c","pulses":50,"seq":7}
```

Qt 侧会再补充：

- `wav_sec`，当 `seq` 能在预读的 `pulse_runtime.json` 里找到对应项时

因此，Qt 日志里看到的 “RF replay report” 是本地派生结果，不是来自真实 MQTT 上报。

来源：`project2_pc_sim/linux_app/main.c`，函数：`on_rf_frame()`，作用：在 replay 模式下把一次成功解码直接写成单行 JSON，不包 envelope、不做 MQTT。

```c
static int on_rf_frame(const rf_frame_t *frame, void *user) {
    app_ctx_t *ctx = (app_ctx_t *)user;
    rf_decoded_packet_t pkt;
    int rc = 0;

    if (ctx == NULL || frame == NULL) {
        return -1;
    }

    ctx->frames_total++;
    ctx->frame_seq++;
    rc = rf_decode_frame(frame, &pkt);
    if (rc != 0) {
        if (rc == RF_DECODE_RC_NO_FRAME) {
            ctx->decode_no_frame++;
        } else {
            ctx->decode_err++;
        }
        return 0;
    }
    ctx->decode_ok++;

    if (pkt.confidence < ctx->min_publish_confidence) {
        ctx->low_conf_drop++;
        return 0;
    }

    if (ctx->stable_repeat > 1u) {
        int idx = -1;
        rf_stable_group_t *g = NULL;
        stable_groups_decay(ctx);
        idx = stable_group_find(ctx, pkt.raw_code);
        if (idx < 0) {
            idx = stable_group_alloc(ctx);
            if (idx >= 0) {
                stable_group_seed(
                    &ctx->stable_groups[idx],
                    pkt.raw_code,
                    pkt.confidence,
                    ctx->frame_seq
                );
            }
            ctx->stable_drop++;
            return 0;
        }
        g = &ctx->stable_groups[idx];
        stable_group_update(g, pkt.raw_code, pkt.confidence, ctx->frame_seq);
        if (g->hits < ctx->stable_repeat) {
            ctx->stable_drop++;
            return 0;
        }
        pkt.raw_code = g->best_code & 0xFFFFFFu;
        snprintf(pkt.addr, sizeof(pkt.addr), "0x%06X", pkt.raw_code & 0xFFFFFFu);
        snprintf(pkt.key, sizeof(pkt.key), "%u", (unsigned)(pkt.raw_code & 0x0Fu));
        if (g->best_conf > pkt.confidence) {
            pkt.confidence = g->best_conf;
        }
    }

    if (
        ctx->has_last_code &&
        pkt.raw_code == ctx->last_code &&
        (ctx->frame_seq - ctx->last_publish_seq) < (uint32_t)ctx->publish_gap
    ) {
        ctx->dup_drop++;
        return 0;
    }

    printf(
        "{\"addr\":\"%s\",\"key\":\"%s\",\"conf\":%.2f,\"src\":\"%s\",\"pulses\":%u,\"seq\":%u}\n",
        pkt.addr,
        pkt.key,
        pkt.confidence,
        pkt.source,
        frame->len,
        (unsigned)ctx->frame_seq
    );
    ctx->last_code = pkt.raw_code;
    ctx->has_last_code = 1;
    ctx->last_publish_seq = ctx->frame_seq;
    ctx->published++;
    return 0;
}
```

跟 `project2_master` 对比，这里最重要的差异是：

- 输出根对象里没有 `type/topic/mqtt_published/payload`。
- 输出合同就是一条扁平 JSON 行：`addr/key/conf/src/pulses/seq`。
- `seq` 是本地 replay 帧序号，不是 driver `drv_seq`。

来源：`project2_pc_sim/qt_gui/rf/rf_gateway_client.cpp`，函数：`parseGatewayEventLine()` / `handleGatewayLine()`，作用：Qt 消费 replay 模式下的扁平 JSON 行，并按 `seq -> wav_sec` 映射回填。

```cpp
bool RFGatewayClient::parseGatewayEventLine(const QString &line, RFEvent *event) const {
    if (event == nullptr) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    const QJsonObject obj = doc.object();
    const QString address = scalarJsonString(obj.value(QStringLiteral("addr")));
    const QString key = scalarJsonString(obj.value(QStringLiteral("key")));
    const QString source = scalarJsonString(obj.value(QStringLiteral("src")));
    const QJsonValue confValue = obj.value(QStringLiteral("conf"));

    if (address.isEmpty() || key.isEmpty() || source.isEmpty() || !confValue.isDouble()) {
        return false;
    }

    event->timestamp = QDateTime::currentDateTime();
    event->address = address;
    event->key = key;
    event->confidence = confValue.toDouble();
    event->source = source;
    event->frameSeq = -1;
    event->candidateWavSec = -1.0;

    const QJsonValue seqValue = obj.value(QStringLiteral("seq"));
    if (seqValue.isDouble()) {
        event->frameSeq = static_cast<qint64>(seqValue.toDouble(-1.0));
    }
    const QJsonValue wavSecValue = obj.value(QStringLiteral("wav_sec"));
    if (wavSecValue.isDouble()) {
        event->candidateWavSec = wavSecValue.toDouble(-1.0);
    }

    return true;
}
```

```cpp
if (source == QStringLiteral("GATEWAY")) {
    RFEvent event;
    if (!parseGatewayEventLine(text, &event)) {
        backend_->incrementParseError();
        backend_->addLog(
            QStringLiteral("WARN"),
            QStringLiteral("RF"),
            QString("Invalid rf_gateway JSON line: %1").arg(text)
        );
        return;
    }

    if (event.frameSeq > 0 && event.candidateWavSec < 0.0) {
        const int idx = static_cast<int>(event.frameSeq);
        if (replayWavSecByIdx_.contains(idx)) {
            event.candidateWavSec = replayWavSecByIdx_.value(idx, -1.0);
        }
    }

    const uint32_t rawCode = parseRawCode(event.address);

    backend_->addRFEvent(event);
    backend_->updateWaveform(buildWaveformFromRawCode(rawCode));
    backend_->addLog(QStringLiteral("INFO"), QStringLiteral("RF"), text);

    QJsonObject payloadObj;
    payloadObj.insert(QStringLiteral("addr"), event.address);
    payloadObj.insert(QStringLiteral("key"), event.key);
    payloadObj.insert(QStringLiteral("conf"), event.confidence);
    payloadObj.insert(QStringLiteral("src"), event.source);
    if (event.candidateWavSec >= 0.0) {
        payloadObj.insert(QStringLiteral("wav_sec"), event.candidateWavSec);
    }
    backend_->addLog(
        QStringLiteral("INFO"),
        QStringLiteral("RF"),
        QString("RF replay report generated locally; not published via MQTT: %1")
            .arg(QString::fromUtf8(QJsonDocument(payloadObj).toJson(QJsonDocument::Compact)))
    );
    return;
}
```

这也解释了为什么 `pc_sim` 文档里一直要把“Qt 本地补 `wav_sec`”单独拎出来：这个字段不是 gateway 原生必带，而是 Qt 结合 `pulse_runtime.json` 再回填出来的。

## 4. Vision 架构

### 4.1 数据流

```text
WSL-side source
-> cv2.VideoCapture
-> VisionPipeline capture/inference/postprocess
-> bridge payload
-> TCP socket
-> Qt Vision page
```

### 4.2 角色拆分

| 组件 | 角色 | 备注 |
| --- | --- | --- |
| `vision/rknn_pipeline.py` | WSL 侧视觉线程管线 | 负责采帧、推理、后处理 |
| `python/wsl_vision_bridge_server.py` | WSL 侧桥接服务 | 把快照打成 JSON 行推给客户端 |
| `qt_gui/vision/vision_page.cpp` | Windows 侧 bridge 客户端 | 负责连接、解析、显示 |
| `qt_gui/log/system_log_page.cpp` | 视觉状态的二次展示面 | 显示 model/FPS/error |

### 4.3 关键设计取舍

#### 取舍 1：视觉运行时放在 WSL 侧

当前 `pc_sim` 不在 Windows Qt 进程里本地跑视觉 runtime，而是：

- WSL 负责视频输入与模型处理
- Windows Qt 负责看结果

这样做的直接后果是：

- Windows Qt 侧不需要本地打开视频源
- 视觉输入路径和模型路径都在 bridge 侧决议

#### 取舍 2：Qt 只吃 bridge payload

当前 Qt Vision 页只关心 payload 里的这些信息：

- `fps`
- `camera_online`
- `model_loaded`
- `frame_count`
- `error`
- `detections`
- `frame_width`
- `frame_height`
- `frame_jpeg_b64`

因此，如果你只想验证 Qt Vision 页，完全可以不启动真实 RKNN，而改用 `fake_detection_stream_test.py --serve`。

#### 取舍 3：`--source` 必须显式给出

`python/wsl_vision_bridge_server.py` 的 `--source` 是强制参数。这个约束非常重要，因为它明确了：

- 当前视觉链没有“默认视频源自动发现”
- 当前视觉链的真值面在 WSL 侧，不在 Qt 侧

### 4.4 当前 payload 合同

bridge 发给 Qt 的 payload 形状大致如下：

```json
{
  "ts": 1710000000.0,
  "fps": 4.0,
  "camera_online": true,
  "model_loaded": true,
  "frame_count": 12,
  "error": "",
  "detections": [
    {"label":"person","score":0.94,"x1":100,"y1":80,"x2":220,"y2":340}
  ],
  "frame_width": 1280,
  "frame_height": 720,
  "frame_jpeg_b64": "<base64 jpeg>"
}
```

Qt 不要求 bridge 提供文件路径，不要求 bridge 提供原始帧句柄，只需要这个 JSON 结构。

来源：`project2_pc_sim/python/wsl_vision_bridge_server.py`，函数：`_build_payload()`，作用：把 WSL 侧视觉快照打成 Qt 能直接消费的 JSON payload。

```python
def _build_payload(snapshot: dict[str, Any]) -> dict[str, Any]:
    det_structs = _format_detections(snapshot)
    overlay = _render_overlay(snapshot.get("frame"), det_structs)
    frame_h, frame_w = (0, 0) if overlay is None else (int(overlay.shape[0]), int(overlay.shape[1]))
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
```

来源：`project2_pc_sim/python/wsl_vision_bridge_server.py`，主循环发送段，作用：把 payload 编成 UTF-8 NDJSON，逐行推给 TCP 客户端。

```python
if clients:
    now = time.monotonic()
    summary = pipeline.state.get_snapshot(include_frame=False)
    if _snapshot_changed(summary, last_frame, last_error, last_heartbeat, now):
        payload = _build_payload(pipeline.state.get_snapshot(include_frame=True))
        blob = (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
        stale: list[socket.socket] = []
        for conn in clients:
            try:
                conn.sendall(blob)
            except socket.timeout:
                continue
```

所以 WSL bridge 的合同不是“共享内存画面句柄”，而就是“逐行 UTF-8 JSON + base64 JPEG”。

来源：`project2_pc_sim/qt_gui/vision/vision_page.cpp`，函数：`handleVisionBridgeLine()` / `applyVisionBridgePayload()`，作用：Qt 从 TCP 收到每一行 JSON 后解析并投影到本地状态。

```cpp
void VisionPage::handleVisionBridgeLine(const QString &line) {
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (backend_ != nullptr) {
            backend_->addLog(
                QStringLiteral("WARN"),
                QStringLiteral("VISION"),
                QString("WSL vision bridge message parse failed: %1").arg(trimmed)
            );
        }
        return;
    }

    applyVisionBridgePayload(doc.object());
}

void VisionPage::applyVisionBridgePayload(const QJsonObject &obj) {
    if (obj.contains(QStringLiteral("fps"))) {
        remoteFps_ = obj.value(QStringLiteral("fps")).toDouble(remoteFps_);
    }
    if (obj.contains(QStringLiteral("camera_online"))) {
        remoteCameraOnline_ = obj.value(QStringLiteral("camera_online")).toBool(remoteCameraOnline_);
    }
    if (obj.contains(QStringLiteral("model_loaded"))) {
        remoteModelLoaded_ = obj.value(QStringLiteral("model_loaded")).toBool(remoteModelLoaded_);
    }
    if (obj.contains(QStringLiteral("frame_count"))) {
        remoteFrameCount_ = obj.value(QStringLiteral("frame_count")).toInt(remoteFrameCount_);
    }
    if (obj.contains(QStringLiteral("error"))) {
        remoteError_ = obj.value(QStringLiteral("error")).toString(remoteError_);
    }

    remoteFrameImage_ = QImage();
    if (obj.contains(QStringLiteral("frame_jpeg_b64"))) {
        const QString encoded = obj.value(QStringLiteral("frame_jpeg_b64")).toString();
        if (!encoded.isEmpty()) {
            const QByteArray jpegData = QByteArray::fromBase64(encoded.toLatin1());
            QImage decoded;
            if (decoded.loadFromData(jpegData, "JPG") || decoded.loadFromData(jpegData, "JPEG")) {
                remoteFrameImage_ = decoded;
            } else {
                remoteError_ = QStringLiteral("Failed to decode bridge frame");
            }
        }
    }

    remoteDetections_.clear();
    remoteBoxes_.clear();

    if (obj.contains(QStringLiteral("frame_width"))) {
        remoteFrameWidth_ = obj.value(QStringLiteral("frame_width")).toInt(remoteFrameWidth_);
    }
    if (obj.contains(QStringLiteral("frame_height"))) {
        remoteFrameHeight_ = obj.value(QStringLiteral("frame_height")).toInt(remoteFrameHeight_);
    }

    const QJsonArray dets = obj.value(QStringLiteral("detections")).toArray();
    int maxX = 0;
    int maxY = 0;
    for (const QJsonValue &v : dets) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject detObj = v.toObject();
        RemoteDetectionBox box;
        box.label = detObj.value(QStringLiteral("label")).toString(QStringLiteral("obj"));
        box.score = detObj.value(QStringLiteral("score")).toDouble(0.0);
        box.x1 = detObj.value(QStringLiteral("x1")).toInt(0);
        box.y1 = detObj.value(QStringLiteral("y1")).toInt(0);
        box.x2 = detObj.value(QStringLiteral("x2")).toInt(0);
        box.y2 = detObj.value(QStringLiteral("y2")).toInt(0);
        maxX = qMax(maxX, qMax(box.x1, box.x2));
        maxY = qMax(maxY, qMax(box.y1, box.y2));
        remoteBoxes_.append(box);
        remoteDetections_.append(
            QString("%1 %2 [%3,%4,%5,%6]")
                .arg(box.label)
                .arg(QString::number(box.score, 'f', 2))
                .arg(box.x1)
                .arg(box.y1)
                .arg(box.x2)
                .arg(box.y2)
        );
    }

    if (!remoteBoxes_.isEmpty()) {
        if (remoteFrameWidth_ <= 0) {
            remoteFrameWidth_ = maxX;
        }
        if (remoteFrameHeight_ <= 0) {
            remoteFrameHeight_ = maxY;
        }
    }

    hasRemoteVision_ = true;
    remoteVisionUpdateMs_ = QDateTime::currentMSecsSinceEpoch();
    refresh();
}
```

Qt 侧消费逻辑同样很克制：

- 先按行 parse JSON。
- 再按字段名更新本地状态，不要求 bridge 提供更多上下文。
- `frame_jpeg_b64` 是真正的视频画面输入，`detections` 是叠框元数据输入，两者完全靠 payload 对齐。

## 5. fake 验证层在架构里的位置

`pc_sim` 里有两个假的本地验证脚本，它们不属于主运行链，但很重要。

### 5.1 `fake_detection_stream_test.py`

它站在 vision bridge 协议旁边：

```text
synthetic frame + fake detections
-> VisionPipelineState snapshot
-> same _build_payload() contract
-> local files and optional TCP serve
```

它的意义是：

- 验证 Qt Vision 页是否还能消费当前 payload 形状
- 验证离线开发时 UI 是否退化
- 不把验证门槛绑死在 RKNN 环境上

来源：`project2_pc_sim/python/fake_detection_stream_test.py`，函数：`_build_payloads()`，作用：复用 bridge 真合同，批量构造一组假的视觉 payload。

```python
def _build_payloads(args: argparse.Namespace) -> list[dict]:
    frame_count = max(1, int(args.frame_count))
    fps = max(0.1, float(args.fps))
    base_frame = _load_base_frame(args)
    state = VisionPipelineState()
    state.set_model_status(True)
    state.set_camera_status(True)

    payloads = []
    for frame_index in range(frame_count):
        frame = _make_frame_variant(base_frame, frame_index, frame_count)
        boxes, classes, scores = _make_fake_detections(frame.shape, frame_index, frame_count)
        state.update_detections(frame, boxes, classes, scores, fps)
        payload = bridge_server._build_payload(state.get_snapshot(include_frame=True))
        payload["simulation"] = {
            "mode": "fake_detection_stream_test",
            "frame_index": frame_index,
            "frame_count": frame_count,
            "uses_source_image": bool(args.source_image),
        }
        payloads.append(payload)
    return payloads
```

这段代码最重要的点不是“造假”，而是它直接调用了 `bridge_server._build_payload(state.get_snapshot(include_frame=True))`。也就是说 fake 流复用的是和真 bridge 同一份 payload 结构。

来源：`project2_pc_sim/python/fake_detection_stream_test.py`，函数：`_write_outputs()` / `_send_payloads()`，作用：同一批 fake payload 既能落地成 `bridge_stream.ndjson`，也能按真 bridge 的逐行 TCP 形式发给 Qt。

```python
def _write_outputs(payloads: list[dict], out_dir: Path, args: argparse.Namespace) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)

    ndjson_path = out_dir / "bridge_stream.ndjson"
    with ndjson_path.open("w", encoding="utf-8", newline="\n") as handle:
        for payload in payloads:
            handle.write(json.dumps(payload, ensure_ascii=False, separators=(",", ":")))
            handle.write("\n")

    manifest = {
        "mode": "fake_detection_stream_test",
        "frame_count": len(payloads),
        "fps": max(0.1, float(args.fps)),
        "source_image": str(Path(args.source_image).resolve()) if args.source_image else "",
        "bridge_host": bridge_server._effective_bind_host(args.host),
        "bridge_port": int(args.port),
        "serve_enabled": bool(args.serve),
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    for index, payload in enumerate(payloads):
        encoded = str(payload.get("frame_jpeg_b64", "") or "")
        if not encoded:
            continue
        frame_path = out_dir / f"annotated_frame_{index:03d}.jpg"
        frame_path.write_bytes(base64.b64decode(encoded.encode("ascii")))

def _send_payloads(conn: socket.socket, payloads: list[dict], fps: float, loop: bool) -> None:
    interval = 1.0 / max(0.1, fps)
    while True:
        for payload in payloads:
            blob = (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
            conn.sendall(blob)
            time.sleep(interval)
```

所以 fake 层在架构上的真实位置是“合同复用验证器”：

- 写文件时验证的是离线 NDJSON 形状。
- 开 `--serve` 时验证的是 Qt 在线 TCP 消费形状。
- 两条路径共用的仍然是同一个 payload schema，而不是另一套测试专用协议。

### 5.2 `fake_event_record_test.py`

它站在“未来录制链交接面”的旁边：

```text
rf_confirmed or manual_record
-> event.json
-> record_done.json
-> mock_record.txt
```

它的意义是：

- 定一个当前可落地的本地事件交接形状
- 但不假装自己已经有真实 recorder / MQTT / master-side integration

## 6. Qt 在整个架构中的职责

Qt 在 `pc_sim` 中承担 3 类职责：

### 6.1 编排职责

- 启动 RF 预处理脚本
- 启动 RF 回放脚本
- 启动并消费 `rf_gateway`

### 6.2 消费职责

- 消费 RF JSON 行
- 消费 Vision bridge JSON 行

### 6.3 展示职责

- 把 RF decode 结果投影到表格、状态和 waveform
- 把 Vision payload 投影到图片、检测列表和状态
- 把局部系统状态写到日志页

Qt 不承担：

- 板端设备访问
- WSL 视频源解析
- 真实 MQTT / RTSP 验收链

## 7. 依赖边界

### 7.1 RF 依赖边界

- `wav_to_pulses.py` 和 `replay_pulse_timeline.py` 只依赖 Python 标准库
- `requirements-rf.txt` 当前为空，这是刻意设计

### 7.2 Vision 依赖边界

- `numpy`
- `opencv-python-headless`
- RKNN wheel 需要单独安装到 WSL 环境

### 7.3 Windows 侧推荐解释器

Windows 侧建议统一使用：

```text
.\.venv\Scripts\python.exe
```

它至少应该承担：

- Qt `--python-bin`
- `fake_detection_stream_test.py`
- `fake_event_record_test.py`

## 8. 当前明确不属于架构的一部分

以下能力不应写进 `pc_sim` 的当前架构承诺：

- `/dev/rf433` 真链
- 板端 `VisionRuntime`
- 真 MQTT 验收
- 真 RTSP 验收
- Qt 本地直接播放 MP4 或摄像头
- 原始脉冲逐样本验收视图

## 9. 与 `project2_master` 的架构关系

最准确的关系描述是：

**`project2_pc_sim` 复用了部分协议、UI 形状和解码逻辑，但它是从 `project2_master` 旁边切出来的离线辅助模拟工程，不是 master 的等价运行时。**

更具体地说：

- `master` 负责板端真实输入、真实设备访问、真实上报链和验收基线
- `pc_sim` 负责在 PC 上重建一小部分可验证链路

对比：

| 维度 | `project2_master` | `project2_pc_sim` |
| --- | --- | --- |
| RF 输入源 | `/dev/rf433` | WAV 回放流 |
| Vision 运行位置 | Qt 进程内本地 runtime | WSL 侧 bridge |
| 上报面 | MQTT / RTSP 真链 | 日志和 bridge 验证 |
| 用途 | 主运行时 | 离线验证与阅读 |

因此，架构讨论时应该说：

- `master` 是主工程
- `pc_sim` 是配套模拟工程

而不应该说：

- `pc_sim` 是 `master` 的 PC 版 runtime
