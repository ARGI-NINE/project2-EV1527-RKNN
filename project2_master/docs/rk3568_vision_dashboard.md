# RK3568 Vision Dashboard 总览

这篇文档只讲 `project2_master/qt_gui` 里的本地视觉运行时，不讲 `project2_pc_sim`，也不把尚未落地的录像闭环、MQTT command 输入说成已实现能力。

如果你第一次接手这条链路，先记住一句话：

`VisionRuntime` 不是一个独立守护进程，也不是一个上游 RTSP 拉流播放器；它是运行在 Qt 进程内部的本地板端运行时，负责采集或解码输入帧、做 RKNN 推理、生成 UI 快照、发布视觉 MQTT、并把推理后的 annotated frame 推到 RTSP 分支。

下面 5 个英文二级标题按 `HEAD` 旧文档的原字符串补回，作为兼容旧骨架；后面的现有中文正文继续保留新增代码讲解，不删任何内容。

## Current Code Path

当前代码路径仍是 `qt_gui/app/main.cpp -> MainWindow::setupRuntime() -> VisionRuntime::workerLoop()`，只是正文把讲解重心前移到了本地 Vision runtime 实码。`workerLoop()` 里现在明确分成两套池和两类失败边界：

- AI 主链实例化 `FrameCopyPool aiFramePool(aiFrameBytes, ...)`，只服务 `copyFrameToAiPool()` -> `rknnPool<rkYolov5s>`。
- RTSP post-stream 支路实例化 `FrameCopyPool postStreamBufferPool(streamFrameBytes, ...)`、`FrameCopyPool postStreamOverlayPool(streamOverlayBytes, ...)`、`PostStreamFramePool postStreamFramePool(kPostStreamQueueSize)`，只服务 `copyPostInferFrameToPostStreamPool()` -> `streamThread` -> `MppRtspEncoder`。

这两套池在同一段代码里初始化，但失败策略不同。`copyPostInferFrameToPostStreamPool()` 返回 `< 0` 时，代码只做四件事：`postStreamBranchActive = false`、记录 `RTSP post-stream frame build failed; disabling RTSP branch while keeping inference and display active`、发布 `publishStreamStatus(..., "offline", ..., "post_stream_failed")`、然后 `postStreamFramePool.stop()`。推理主循环、`VisionSnapshot` 回写和 UI 展示继续保留，所以 `post_stream_failed` 只关 RTSP 支路，不关本地推理主链。源码锚点：`project2_master/qt_gui/vision/vision_runtime.cpp::workerLoop()`、`copyPostInferFrameToPostStreamPool()`。

## Runtime Contract

当前 runtime 合同仍是“Qt 进程内本地 `VisionRuntime` + sourceThread 双扇出”，但需要补清 Vision MQTT 和 RF MQTT 的实码边界。

`VisionMqttPublisher` 的真实接口只有：

- `open()`：`mosquitto_lib_init()` -> `mosquitto_new()` -> `mosquitto_connect_async()` -> `mosquitto_loop_start()`
- `publish()`：在 `isConnected()` 为真时直接包一层 `mosquitto_publish()`
- `close()`：`mosquitto_disconnect()` / `mosquitto_loop_stop()` / `mosquitto_destroy()` / `mosquitto_lib_cleanup()`

也就是说，Vision 侧没有 subscribe、没有 command 通道、没有 stdout envelope。`publishStreamStatus()` 和 `publishDetection()` 都是经 `logPublishedMessage()` 直接调用 `mqtt->publish()`，成功后才 `backend->addMqttPublishLog()`。

RF 支路则不同：`project2_master/linux_app/main.c::build_protocol_line()` 会组装 `{"type","topic","mqtt_published","payload"}` 这层 stdout envelope，`emit_protocol_message()` 先尝试 `mqtt_publisher_publish()`，再 `fprintf(stdout, "%s\\n", line)` 把 envelope 交给 Qt。真正连接 broker 的底层实码在 `project2_master/linux_app/mqtt_publisher.c::mqtt_publisher_publish()`。因此，RF 是“stdout envelope + 可选实际 MQTT publish”，Vision 是“Qt 内直接 mosquitto publish”。

## UI Behaviour

旧骨架里的 UI 行为没有被推翻，只是被下文更细的运行时解释取代：

- `VisionPage` 仍只消费 `DashboardBackend::snapshotVisionState()` 的最新 `VisionSnapshot`。
- `SystemLogPage` 和状态栏仍只读 backend 快照与日志，不直接碰 `sourceThread`、`streamThread`、`MppRtspEncoder`。
- 因为 post-stream 失败只会关闭 RTSP 支路，所以 UI 仍可能继续显示本地 annotated frame 和检测列表。

## Build Notes

`project2_master/qt_gui/CMakeLists.txt` 现在就是依赖边界和 runtime 门禁本身：

- 只有在 `if(DASHBOARD_ENABLE_LOCAL_VISION_RUNTIME)` 成立时才进入本地 Vision runtime 构建。
- 先 `pkg_check_modules(AVFORMAT/AVCODEC/AVUTIL/MOSQUITTO)`，再 `find_library()` / `find_path()` 兜底找 FFmpeg 与 `libmosquitto`。
- 缺 `mosquitto.h` 或 `libmosquitto` 会 `message(FATAL_ERROR "libmosquitto is required for the board-side vision MQTT publisher")`。
- 缺 `libavformat/libavcodec/libavutil` 会 `message(FATAL_ERROR "FFmpeg libavformat/libavcodec/libavutil are required for board-side RTSP push")`。
- `target_sources()` 明确编入 `mpp_decoder.cc` 和 `mpp_encoder_rtsp.cc`。
- `target_compile_definitions()` 明确写入 `DASHBOARD_HAVE_LOCAL_VISION_RUNTIME=1` 和 `DASHBOARD_LOCAL_VISION_ROOT=...`。
- `target_link_libraries()` 明确链接 `librknnrt.so`、`librga.so`、`librockchip_mpp.so`、FFmpeg、`mosquitto` 和 `Threads::Threads`。
- 否则直接 `message(FATAL_ERROR "${DASHBOARD_LOCAL_VISION_RUNTIME_REASON}")`，不再接受“没带本地 runtime 也算支持”的 fake-success 路径。

## Validation Boundary

这份兼容旧骨架的总览仍只描述当前已落地代码，不把未实现能力写成已交付事实：

- 已落地的是 Qt 进程内本地推理、Vision detection MQTT publish、RTSP status publish、annotated RTSP push。
- 仍未落地的是 command subscribe、录像闭环、`record_done` publish。
- 旧骨架恢复后，正文仍应按“同一帧推理结果扇出到 UI / Vision MQTT / RTSP”来理解，而不是把 Vision 混成 RF 那套 stdout envelope 设计。

## 1. 建议阅读顺序

1. 先读本文，建立全局脑图。
2. 再读 [mqtt_publish_tutorial_zh.md](mqtt_publish_tutorial_zh.md)，把 Vision MQTT publish 的事实和边界看清。
3. 再读 [rtsp_push_tutorial_zh.md](rtsp_push_tutorial_zh.md)，理解 annotated frame 为什么要单独走 post-stream 分支。
4. 最后读 [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md)，按代码入口一路走到 `VisionRuntime::workerLoop()`。
5. 如果你要确认 vendor 目录到底被怎样使用，再看 [../third_party/rknn_yolov5_rk3568/README.md](../third_party/rknn_yolov5_rk3568/README.md)。

## 2. 它在整个项目里的位置

`project2_master` 里有两条彼此独立、最后汇聚到同一个 Qt 后端的生产链路：

- RF 链路：`rf_gateway` 子进程输出 JSON envelope，`RFGatewayClient` 消费 stdout。
- Vision 链路：`VisionRuntime` 在 Qt 进程内直接采相机帧或解码本地视频，然后自己做推理、推流和发布。

不要把这两条链路混成一条：

- RF MQTT publish 来自 `linux_app/mqtt_publisher.c`。
- Vision MQTT publish 来自 `qt_gui/vision/vision_runtime.cpp` 里的 `VisionMqttPublisher`。
- RF 侧有 `type/topic/mqtt_published/payload` 的 stdout envelope。
- Vision 侧没有 stdout envelope；它直接调 mosquitto 发布，并把成功发布的消息记到 `DashboardBackend` 日志里。

## 3. 一页看完整条 Vision 主链

```text
main.cpp
  -> 解析 CLI
  -> MainWindow(options)
     -> setupRuntime()
        -> visionRuntime_.start()
           -> VisionRuntime::workerLoop()
              -> 解析模型路径
              -> 初始化 RKNN aiPool
              -> 初始化 Vision MQTT publisher
              -> 打开输入
                 -> /dev/video* 走 V4L2Capture
                 -> 本地可读视频文件走 MppDecoder
              -> 建立 aiFramePool / postStreamBufferPool / postStreamOverlayPool
              -> 可选启动 streamThread
              -> 启动 sourceThread
              -> 主循环 aiPool.get()
                 -> 更新 VisionSnapshot
                 -> 有目标时发布 vision detection MQTT
                 -> RTSP 开启时生成 annotated frame 并送入 postStreamFramePool
```

这条主链里最关键的不是“用了哪些库”，而是“线程和所有权怎么拆”。

代码来源：`project2_master/qt_gui/app/main.cpp`、`project2_master/qt_gui/app/main_window.cpp`
函数：`main()`、`MainWindow::setupRuntime()`
作用：把 Vision CLI 参数落到 `AppOptions`，然后在 Qt 主窗口里真正启动 `VisionRuntime`。

```cpp
const QString defaultVisionRtspUrl = dashboard::defaultVisionRtspUrl();
QCommandLineOption visionRtspUrlOption(
    QStringLiteral("vision-rtsp-url"),
    QStringLiteral("RTSP push URL used by the local vision runtime when enabled (default: %1).")
        .arg(defaultVisionRtspUrl),
    QStringLiteral("url"),
    defaultVisionRtspUrl
);
QCommandLineOption disableVisionRtspOption(
    QStringLiteral("disable-vision-rtsp"),
    QStringLiteral("Disable the RTSP push side-branch while keeping local vision inference and display active.")
);
parser.addOption(visionRtspUrlOption);
parser.addOption(disableVisionRtspOption);
parser.process(app);

dashboard::AppOptions options;
options.visionRtspEnabled = !parser.isSet(disableVisionRtspOption);
options.visionRtspUrl = parser.value(visionRtspUrlOption).trimmed();
if (options.visionRtspEnabled && options.visionRtspUrl.isEmpty()) {
    options.visionRtspUrl = defaultVisionRtspUrl;
}

dashboard::MainWindow window(options);
window.show();

void MainWindow::setupRuntime() {
    backend_.addLog("INFO", "SYSTEM", QStringLiteral("Qt5 前端已启动"));
    backend_.addLog(
        "INFO",
        "VISION",
        QStringLiteral("板侧本地视觉链路已接入，默认使用 %1").arg(options_.visionDevice)
    );

    rfClient_.start();
    visionRuntime_.start();
    updateStatusBar();
}
```

这一段足够支撑概览里的两个关键事实：第一，RTSP URL 和 RTSP 开关都是 CLI 输入，不是运行时动态协商出来的；第二，`VisionRuntime` 是在 Qt 进程内由 `MainWindow` 直接启动的，不是旁路服务。

## 4. 四个关键执行单元

### 4.1 `sourceThread`

`sourceThread` 是上游生产者，只负责拿输入帧，然后把帧拷贝进 AI 专用池。

它的职责很克制：

- 如果输入是 `/dev/video*`，就从 `V4L2Capture` dequeue 一帧。
- 如果输入是本地文件，就从 `MppDecoder` 读一帧 NV12。
- 给每一帧分配 `frame_id` 和 `captureTsUs`。
- 调 `copyFrameToAiPool()` 把数据复制进 `aiFramePool`，再交给 `rknnPool<rkYolov5s>`。

它不做这些事：

- 不直接画框。
- 不直接发 MQTT。
- 不直接推 RTSP。
- 不直接更新 UI。

### 4.2 `aiPool`

`aiPool` 是 RKNN worker 池，当前配置固定为：

- `kAiWorkerThreads = 1`
- `kAiQueueSize = 4`

它的边界很明确：

- 输入是 `copyFrameToAiPool()` 交进来的、已经具备独立释放契约的帧。
- 输出是 `aiPool.get()` 取回的检测结果和原始帧所有权。
- `get()` 这一侧才是 UI 更新、MQTT publish、RTSP annotated frame 构建的起点。

换句话说，`aiPool` 只解决“推理”，不解决“推理后如何扇出”。

### 4.3 `postStreamFramePool`

这是 RTSP 分支专用队列，不是 AI 主链的通用队列。

它承接的是“推理后、已叠框、适合编码”的 `PostStreamFrame`：

- 格式：当前代码固定走 `RK_FORMAT_YCbCr_420_SP`，也就是 NV12。
- 内容：不是裸原始帧，而是已经画好框和标签的 annotated frame。
- 作用：把 AI 主循环和 RTSP 推流线程解耦。

一个容易忽略但很重要的事实：

- `PostStreamFramePool` 队列满了会丢最旧帧，不会卡死整条视觉链。
- 这说明 RTSP 分支天然允许“掉流帧但保住主推理链”。

### 4.4 `streamThread`

`streamThread` 只在 `visionRtspEnabled == true` 时启动。

它的职责是：

- 从 `postStreamFramePool` 等待并取出 annotated NV12 帧。
- 首帧或重连时懒加载 `MppRtspEncoder::open()`。
- 对每帧调用 `MppRtspEncoder::encodeAndPush()`。
- 把在线/离线状态通过 MQTT 发出去。

它不做这些事：

- 不参与 RKNN 推理。
- 不参与 UI 画面刷新。
- 不消费 RF 事件。

## 5. 为什么有两套池，而不是一套“大 frame pool”

当前代码故意拆成两套资源：

- `aiFramePool`
- `postStreamBufferPool` + `postStreamOverlayPool` + `postStreamFramePool`

这样拆有三个原因。

第一，生命周期不同。

- AI 输入帧在 `aiPool.put()` 后交给 RKNN worker。
- RTSP 帧在 `copyPostInferFrameToPostStreamPool()` 之后交给 `streamThread`。

第二，格式不同。

- AI 输入可能是 `YUYV`、`NV12` 或 `BGR24`。
- RTSP 分支最终固定要 NV12。
- 中间为了画框还会额外走一份 BGRA overlay buffer。

第三，故障策略不同。

- AI 主链失败通常意味着本地视觉功能失效。
- RTSP 分支失败时，代码优先选择“关闭 RTSP 支路，但保住推理和 UI”。

## 6. 当前代码里的 RTSP annotated frame 事实

这里最容易写错，所以单列出来。

当前 RTSP 推出去的不是：

- 原始摄像头裸帧。
- `VisionPage` 屏幕截图。
- Qt widget 截图。

当前 RTSP 推出去的是：

1. `aiPool.get()` 取回原始推理帧和检测结果。
2. `copyPostInferFrameToPostStreamPool()` 先把源帧转成 BGRA overlay 工作面。
3. 用 `QPainter` 在 overlay 面上画检测框和标签。
4. 再把 overlay 面转回 NV12。
5. 把 NV12 `PostStreamFrame` 放进 `postStreamFramePool`。
6. `streamThread` 取出后交给 `MppRtspEncoder` 编码并推送。

所以它是“post-infer annotated frame”，不是“原始监控流”。

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`VisionRuntime::workerLoop()` 主循环
作用：把 `aiPool.get()` 回来的推理结果同时扇出到 RTSP 支路、UI 快照和 Vision MQTT。

```cpp
if (aiPool.get(
        detGroup,
        scaleW,
        scaleH,
        &frameData,
        &frameWidth,
        &frameHeight,
        &frameFormat,
        &frameId,
        &captureTsUs,
        &releaseFn,
        &releaseCtx) != 0) {
    break;
}

if (frameData != nullptr) {
    if (postStreamBranchActive) {
        const int postStreamRc = copyPostInferFrameToPostStreamPool(
            detGroup,
            frameData,
            frameWidth,
            frameHeight,
            frameFormat,
            frameId,
            captureTsUs > 0 ? captureTsUs : nowWallTimeUs(),
            streamWidth,
            streamHeight,
            streamStride,
            &postStreamBufferPool,
            &postStreamOverlayPool,
            &postStreamFramePool
        );
        if (postStreamRc < 0) {
            postStreamBranchActive = false;
            publishStreamStatus(
                backend_,
                &mqtt,
                QStringLiteral("offline"),
                rtspUrl,
                QStringLiteral("post_stream_failed")
            );
            postStreamFramePool.stop();
        }
    }

    snapshot.detections = formatDetections(detGroup);
    snapshot.frame = renderAnnotatedFrame(rgbImage, detGroup);
    if (detGroup.count > 0) {
        publishDetection(backend_, &mqtt, rtspUrl, frameId, captureTsUs, detGroup);
    }
}
```

这段代码把“总链路”压缩成了一眼能看懂的事实：主循环先从 `aiPool.get()` 取回推理结果，再决定要不要构建 RTSP annotated frame，同时刷新 UI，并在有目标时发布 detection MQTT。RTSP、UI、MQTT 三条出口不是三套独立采集链，而是同一帧推理结果在这里做出的扇出。

## 7. 当前代码里的 MQTT publish 事实

Vision 链路当前有两个 MQTT 主题：

| 主题 | 发布条件 | retain | 说明 |
|---|---|---|---|
| `argi/device/rk3568-001/vision/detection` | `detGroup.count > 0` | `false` | 只在检测到目标时发布 |
| `argi/device/rk3568-001/stream/status` | RTSP 分支状态变化时 | `true` | 用来表达推流在线/离线及原因 |

对应 payload 也不同。

`vision/detection` 关注“这帧看到了什么”：

- `device_id`
- `type = "vision_detection"`
- `frame_id`
- `ts_us`
- `objects[]`
- `stream_url`

`stream/status` 关注“RTSP 分支现在处于什么状态”：

- `device_id`
- `type = "stream_status"`
- `state`
- `protocol = "rtsp"`
- `codec = "h264"`
- `url`
- 可选 `reason`

几个必须写清的事实：

- Vision MQTT 是 publish-only，没有 subscribe。
- 当前没有 `gateway/{gateway_id}/cmd` 的订阅实现。
- 当前没有 `record_done` 的发布实现。
- 当前没有事件录像完成通知闭环。

## 8. CLI 开关和默认值

视觉相关 CLI 开关都在 `qt_gui/app/main.cpp` 和 `qt_gui/core/app_options.h`。

| 开关 | 默认值 | 含义 |
|---|---|---|
| `--vision-device` | `/dev/video9` | 本地视觉输入；允许 `/dev/video*` 或可读本地视频文件 |
| `--vision-rtsp-url` | `rtsp://192.168.30.26:8554/rk3568-001/cam0` | RTSP push 目标地址 |
| `--disable-vision-rtsp` | 关闭标志，无默认字符串值 | 禁用 RTSP 支路，但保留本地推理和 UI |
| `--rf-input` | `/dev/rf433` | 属于同一个 Qt 程序，但这是 RF 链路参数，不是 Vision 参数 |

对 `--vision-device` 要写准：

- `/dev/video*` 走真实摄像头采集。
- 可读本地文件走 `MppDecoder`。
- 这两条支路都复用同一个 `VisionRuntime`，不是假数据 fallback。

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`VisionRuntime::workerLoop()` 里的 RTSP 分支创建逻辑
作用：把 CLI 里的 `visionRtspEnabled` 真正落实为“启动推流线程”或“仅发布 disabled 状态”。

```cpp
if (rtspEnabled) {
    streamThread = std::thread([&]() {
        MppRtspEncoder encoder;
        bool encoderOpen = false;

        while (true) {
            PostStreamFrame frame;
            if (!postStreamFramePool.waitAndPop(&frame)) {
                break;
            }

            if (!encoderOpen) {
                if (encoder.open(
                        rtspUrl.toUtf8().constData(),
                        frame.width,
                        frame.height,
                        frame.stride,
                        frame.height,
                        fpsNum,
                        fpsDen,
                        kDefaultRtspBitrateBps) != 0) {
                    publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), rtspUrl, QStringLiteral("open_failed"));
                    release_frame_buffer(frame.data, frame.release_fn, frame.release_ctx);
                    continue;
                }
                encoderOpen = true;
                publishStreamStatus(backend_, &mqtt, QStringLiteral("online"), rtspUrl);
            }
        }
    });
} else {
    backend_->addLog("INFO", "VISION", QStringLiteral("RTSP push disabled by option"));
    publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), rtspUrl, QStringLiteral("disabled"));
}
```

概览层面只需要抓住这一个结论：`--disable-vision-rtsp` 不是“停掉 VisionRuntime”，而是让 `workerLoop()` 根本不创建 `streamThread`，同时主动对外发布一条 `offline/disabled` 状态。这样本地推理和 UI 还能继续工作，只有 RTSP 支路被关闭。

## 9. 依赖边界

这条链依赖的不是一个“通用 OpenCV demo 环境”，而是非常具体的一组板端依赖：

- Qt5：UI、线程外的图像对象、日志和页面展示。
- RKNN：`rkYolov5s` + `rknnPool`，负责推理。
- RGA：格式转换、缩放、从源帧到 RGB / BGRA / NV12 的搬运。
- MPP：H.264 编码。
- FFmpeg `libavformat/libavcodec/libavutil`：RTSP 输出封装。
- `libmosquitto`：Vision MQTT publish。
- vendored model 与 headers/libs：来自 `third_party/rknn_yolov5_rk3568/`。

构建边界也要写准：

- `qt_gui/CMakeLists.txt` 在 Linux 上要求本地 Vision runtime 必须可用。
- 不再接受“没带本地 runtime 也假装成功”的 fallback。
- 没有 `libmosquitto` 会直接构建失败。
- 没有 FFmpeg 三件套也会直接构建失败。

## 10. 当前支持与当前不支持

### 10.1 当前代码明确支持

- Qt 进程内本地 `VisionRuntime`
- `/dev/video*` 摄像头输入
- 可读本地视频文件输入
- RKNN 目标检测
- `VisionSnapshot` 回写给页面
- Vision detection MQTT publish
- RTSP stream status publish
- RTSP annotated frame push

### 10.2 当前代码明确未实现

- MQTT command subscribe
- `gateway/{gateway_id}/cmd` 命令处理
- recorder
- event-triggered recorder pipeline
- `record_done` publish
- 事件前后若干秒录像留证闭环

只要文档出现下面这些表述，就算写错：

- “已经支持 command subscribe”
- “已经支持 MQTT 控制命令”
- “已经支持 record_done”
- “已经支持事件录像闭环”

## 11. 常见故障路径

### 11.1 模型没找到

`resolveModelPath()` 只会从这些候选位置找模型：

- `applicationDirPath()/model/yolov5s_relu-640-640-rk3568.rknn`
- `applicationDirPath()/model/yolov5s-640-640.rknn`
- `DASHBOARD_LOCAL_VISION_ROOT/model/...`

找不到就直接报错并停掉视觉运行时。

### 11.2 相机或视频输入打不开

- 摄像头支路失败：`capture.open()` 或 `capture.startStream()` 失败。
- 文件支路失败：文件不存在、不可读、不是 regular file，或者 `decoder.open()` / 首帧解码失败。

### 11.3 RTSP 支路失败但主链继续活着

这类是本文最重要的降级策略：

- `encoder.open()` 失败：发布 `offline/open_failed`，等待重试。
- `encodeAndPush()` 失败：发布 `offline/push_failed`，关闭 encoder，等待重试。
- `copyPostInferFrameToPostStreamPool()` 返回致命错误：发布 `offline/post_stream_failed`，停掉 post-stream 队列，但保留推理和 UI。

### 11.4 MQTT broker 没连上

`VisionMqttPublisher::open()` 失败时只会：

- 打一条 `WARN` 日志；
- 让 Vision MQTT publish 失效；
- 不会让视觉推理停止。

这表示 MQTT 是重要输出，但不是本地推理可用性的前提。

## 12. 一句话结论

本项目当前的真实 Vision 设计不是“摄像头一把梭直接推流”，而是：

先把本地输入帧送进 `sourceThread -> aiPool` 做推理，再由主循环同时扇出到 `VisionSnapshot`、Vision MQTT publish、以及 `postStreamFramePool -> streamThread -> MppRtspEncoder` 这条 annotated RTSP 支路；RTSP、MQTT、录像命令和录像完成通知并不是同一个完成度级别，后两者中的 subscribe/recorder/record_done 目前仍未实现。
