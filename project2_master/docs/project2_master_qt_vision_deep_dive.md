# `project2_master/qt_gui` Vision 深读

这篇文档面向要读代码、改代码、排故障的工程师。重点不是“功能介绍”，而是把 `qt_gui` 里的本地视觉主链按真实代码拆开，讲清楚：

- `VisionRuntime` 怎么启动。
- `sourceThread / aiPool / postStreamFramePool / streamThread` 怎么分工。
- MQTT publish 和 RTSP push 分别从哪里发起。
- 哪些能力已经实现，哪些只是 TODO。

本文只基于当前仓库事实，不把规划接口写成已交付能力。

下面这组标题按 `HEAD` 旧文档的原字符串与层级补回，作为兼容阅读地图；编号会和下文当前正文重复，但现有正文不删，只在其前面补回旧骨架。

## 0. 阅读地图：先读哪三段

兼容 `HEAD` 旧骨架时，仍建议按 `main.cpp -> main_window.* / dashboard_backend.* -> vision_runtime.*` 这三段读。当前这版正文把篇幅集中在 Vision 真实运行时，所以旧骨架里的 RF 页面、日志页面和状态栏 walkthrough 只保留为阅读地图，不再把它们和下文新增的 `workerLoop()` / RTSP / Vision MQTT 深挖混写。

## 1. 入口链路：`main.cpp`

对应下文当前正文的 `## 2. 入口：main.cpp`。当前文档保留了 `--vision-device`、`--vision-rtsp-url`、`--disable-vision-rtsp` 的入口事实，以及 `AppOptions` 如何把这些 CLI 约束落进 `VisionRuntime`。

## 2. 启动参数与主题：`app_options.h` / `app_palette.*`

`HEAD` 版这里同时看 `AppOptions` 和 Qt 主题初始化。当前正文仍沿用同一条启动边界，只是把篇幅从 palette 细节转移到了 Vision CLI、RTSP 开关和本地输入合法性校验。

## 3. 窗口组装：`main_window.*`

### 3.1 `setupUi()`

旧骨架里强调三页签和状态栏 UI 壳仍然存在；当前正文不重复铺开控件细节，而是把它当成已存在外壳。

### 3.2 `setupRuntime()`

对应下文当前正文的 `## 3. 组装层：MainWindow`。`rfClient_.start()` 和 `visionRuntime_.start()` 的启动关系没有改变，只是本文把解释重心放到了 Vision 侧。

### 3.3 `updateStatusBar()`

`HEAD` 版这里强调状态栏只读快照；当前正文在 `DashboardBackend`、`VisionSnapshot` 和错误路径段落里继续沿用同一边界。

### 1. 组件职责表

旧版把 `MainWindow`、`DashboardBackend`、`RFGatewayClient`、`VisionRuntime` 和各个 page 分成“组装 / 状态 / 生产 / 展示”四层。当前这版新增正文没有改掉这条责任线，只是把解释重心从 RF / UI 页转到了 `VisionRuntime` 主链。

## 4. 共享状态中枢：`common_types.h` / `dashboard_backend.*`

### 4.1 RF 写入

RF 仍通过 `RFGatewayClient` 回写后端，但这份 Vision 深读不再展开 RF 协议细节。

### 4.2 视觉写入

对应下文当前正文的 `## 4`、`## 7.7`、`## 11.3`。`VisionSnapshot`、MQTT publish log 和错误态仍统一回写 `DashboardBackend`。

### 4.3 日志和系统统计

旧骨架中的日志 / 系统统计边界没有变：视觉链只追加日志与状态，不直接操纵页面对象。

## 5. RF 链路：`rf_gateway_client.*`

### 5.1 启动与停止

### 5.2 固定路径与固定输入

### 5.3 `QProcess` 信号链

### 5.4 行解析

这四节按 `HEAD` 旧标题保留为兼容阅读入口：RF 仍是独立链路，但这份深读的新增正文不把 RF 细节和 Vision 运行时混写。

## 6. RF 页面与波形控件：`rf_status_page.*` / `waveform_widget.*`

### 6.0 `RFGatewayClient` helper 链：启动外部进程与解析 stdout

### 6.1 `RFStatusPage`

### 6.2 `WaveformWidget`

这些旧标题保留为 UI 总览锚点，提醒读者 Qt 页面对 RF / Vision 一律只读快照；本文后文只深挖 Vision 侧主链。

## 7. 视觉链路：`vision_runtime.*` / `vision_page.*`

### 7.1 `VisionRuntime`

对应下文当前正文的 `## 5` 到 `## 16`，尤其是 `workerLoop()` 七阶段、RTSP 支路和 Vision MQTT publish。

### 7.2 本地视觉链路

对应下文当前正文的 `## 6`、`## 7`、`## 9`、`## 10`。当前版本的核心改动仍是“Qt 进程内本地运行时 + sourceThread / aiPool / post-stream 支路”。

### 7.2.1 `VisionRuntime` helper 链：本地模型 / 相机 / 渲染辅助函数

当前正文把这部分内容拆散到模型解析、输入打开、RTSP frame 构造和 MQTT publish 段落里，但 helper 边界没有消失。

### 7.3 结果回传

对应 `VisionSnapshot` 更新、`publishDetection()`、`publishStreamStatus()` 和 `backend_->addLog()`。

### 7.4 `VisionPage`

页面仍只消费 `DashboardBackend` 快照，不直接碰 worker 线程对象。

## 8. 日志链路：`system_log_page.*`

当前正文通过 `## 11.3` 和多处 `backend_->addLog()` 说明日志入口，但不再单独做页面 walkthrough。

## 9. 结论：这套 UI 的真实职责边界

结论没有变：`MainWindow` 组装，`DashboardBackend` 收口，`VisionRuntime` 负责本地视觉链，RTSP / MQTT / UI 是同一帧推理结果的不同扇出。

## 2. 关键资源生命周期

`worker_`、`sourceThread`、`streamThread`、`FrameCopyPool`、`PostStreamFramePool` 的生命周期在当前正文里比 `HEAD` 展开得更细，重点见下文 `## 7` 到 `## 10`。

## 3. 数据契约与更新频率

当前正文保留了 `VisionSnapshot`、MQTT topic/payload、RTSP status publish 的事实；旧版关于刷新频率的 UI 视角没有被否定，只是被更细的运行时解释覆盖。

## 4. 最后一眼只看代码

如果只想用旧版“扫一眼代码”的阅读法，优先看下文里的 `main.cpp`、`workerLoop()`、`copyPostInferFrameToPostStreamPool()`、`MppRtspEncoder::encodeAndPush()`、`publishDetection()` 这些实码段。

## 1. 先给结论

`qt_gui` 里的视觉链是一条“Qt 进程内本地运行时”：

```text
main.cpp
  -> AppOptions
  -> MainWindow
     -> setupRuntime()
        -> VisionRuntime::start()
           -> std::thread worker_
              -> VisionRuntime::workerLoop()
                 -> 打开模型 / MQTT / 输入源
                 -> sourceThread 采帧并送 aiPool
                 -> 主循环 aiPool.get()
                    -> VisionSnapshot
                    -> Vision detection MQTT
                    -> postStreamFramePool
                 -> streamThread
                    -> MppRtspEncoder
                    -> RTSP stream status MQTT
```

最容易理解错的两点：

1. `VisionRuntime` 不是外部子进程，而是 Qt 进程内的 C++ 线程。
2. RTSP 推流拿到的是“推理后 annotated frame”，不是原始裸帧。

## 2. 入口：`main.cpp`

视觉链从 `qt_gui/app/main.cpp` 开始，但 `main.cpp` 只做参数解析和运行时组装，不做任何视觉业务。

它处理的视觉相关开关只有三个：

| 开关 | 默认值 | 含义 |
|---|---|---|
| `--vision-device` | `/dev/video9` | 视觉输入，允许 `/dev/video*` 或可读本地视频文件 |
| `--vision-rtsp-url` | `rtsp://192.168.30.26:8554/rk3568-001/cam0` | RTSP push 目标地址 |
| `--disable-vision-rtsp` | 无 | 禁用 RTSP 支路 |

这里先做了两层约束。

第一层是参数落地：

- `visionDevice`
- `visionRtspEnabled`
- `visionRtspUrl`

第二层是输入合法性：

- `isAllowedVisionDevicePath(path)` 只认 `/dev/video*`
- `isReadableVisionInputFile(path)` 只认存在、可读、普通文件
- `isAllowedVisionInputPath(path)` 在这两者之间二选一

这意味着当前代码支持两种真实输入：

- 板端摄像头设备
- 本地可读视频文件

不支持任意 URL、管道、网络流地址直接塞进 `--vision-device`。

代码来源：`project2_master/qt_gui/app/main.cpp`、`project2_master/qt_gui/core/app_options.h`
函数：`main()`、`defaultVisionRtspUrl()`、`isAllowedVisionInputPath()`
作用：定义 Vision CLI 开关、默认 RTSP URL、RTSP 开关落地方式，以及 `--vision-device` 的合法输入边界。

```cpp
inline QString defaultVisionDevicePath() {
    return QStringLiteral("/dev/video9");
}

inline QString defaultVisionRtspUrl() {
    return QStringLiteral("rtsp://192.168.30.26:8554/rk3568-001/cam0");
}

inline bool isAllowedVisionInputPath(const QString &path) {
    if (isAllowedVisionDevicePath(path)) {
        return true;
    }
    return isReadableVisionInputFile(path);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCommandLineParser parser;

    const QString defaultVisionDevice = dashboard::defaultVisionDevicePath();
    QCommandLineOption visionDeviceOption(
        QStringLiteral("vision-device"),
        QStringLiteral("Board-side vision input passed to the local runtime "
                       "(default: %1; accepts /dev/video* or a readable local video file).")
            .arg(defaultVisionDevice),
        QStringLiteral("input"),
        defaultVisionDevice
    );
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
    parser.addOption(visionDeviceOption);
    parser.addOption(visionRtspUrlOption);
    parser.addOption(disableVisionRtspOption);
    parser.process(app);

    dashboard::AppOptions options;
    options.visionDevice = parser.value(visionDeviceOption).trimmed();
    if (options.visionDevice.isEmpty()) {
        options.visionDevice = defaultVisionDevice;
    }
    options.visionRtspEnabled = !parser.isSet(disableVisionRtspOption);
    options.visionRtspUrl = parser.value(visionRtspUrlOption).trimmed();
    if (options.visionRtspEnabled && options.visionRtspUrl.isEmpty()) {
        options.visionRtspUrl = defaultVisionRtspUrl;
    }
    if (!dashboard::isAllowedVisionInputPath(options.visionDevice)) {
        QTextStream(stderr)
            << "Invalid --vision-device: " << options.visionDevice
            << " (master supports local /dev/video* devices or readable local video files)\n";
        return 1;
    }

    dashboard::MainWindow window(options);
    window.show();
    return app.exec();
}
```

这段代码把文档里的 CLI 事实全部钉死了：RTSP 不是单独配置文件控制，而是 `main()` 里直接落到 `AppOptions.visionRtspEnabled` 和 `AppOptions.visionRtspUrl`；`--disable-vision-rtsp` 只是关闭推流支路，不会让 `MainWindow` 放弃创建 `VisionRuntime`。同时 `--vision-device` 只接受 `/dev/video*` 或可读本地文件，网络 URL 不在当前实现范围内。

## 3. 组装层：`MainWindow`

`MainWindow` 构造函数同时实例化两条生产链：

- `RFGatewayClient rfClient_`
- `VisionRuntime visionRuntime_`

视觉相关的关键点在 `setupRuntime()`：

- 先记系统日志。
- 记一条视觉链接入日志，说明当前使用的 `visionDevice`。
- 调 `visionRuntime_.start()`。

这说明 `MainWindow` 只负责：

- 把运行时挂起来。
- 把页面绑到 `DashboardBackend`。

它不负责：

- 采帧。
- 推理。
- MQTT 发布。
- RTSP 编码。

代码来源：`project2_master/qt_gui/app/main_window.cpp`
函数：`MainWindow::MainWindow()`、`setupRuntime()`
作用：把 `DashboardBackend`、`RFGatewayClient`、`VisionRuntime` 组装到同一个 Qt 进程里，并在窗口层触发运行时启动。

```cpp
MainWindow::MainWindow(const AppOptions &options, QWidget *parent)
    : QMainWindow(parent),
      options_(options),
      rfClient_(&backend_, options_, this),
      visionRuntime_(&backend_, options_) {
    setupUi();
    setupRuntime();
}

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

这段装配代码说明 `VisionRuntime` 不是外部守护进程，也不是懒加载插件，而是 `MainWindow` 构造阶段就被实例化、在 `setupRuntime()` 里直接 `start()`。所以后面所有 `workerLoop()`、`sourceThread`、`streamThread` 都属于当前 Qt 进程内部线程，而不是另一套独立服务。

## 4. 状态中心：`DashboardBackend`

视觉链最终不会直接操作页面对象，而是统一回写 `DashboardBackend::updateVisionState()`。

当前视觉快照结构是：

| 字段 | 含义 |
|---|---|
| `frame` | UI 展示的 `QImage` |
| `detections` | 已格式化好的字符串列表 |
| `fps` | 推理 FPS |
| `statusReported` | 是否有有效视觉状态 |
| `modelLoaded` | 模型是否已加载 |
| `cameraOnline` | 输入侧是否在线 |
| `frameCount` | 主循环累计处理帧数 |
| `errorMsg` | 当前错误文本 |

页面是纯消费者：

- `VisionPage` 每 33ms 拉一次 `VisionSnapshot`
- `SystemLogPage` 每 1000ms 拉一次日志和系统状态
- 状态栏每 2000ms 拉一次汇总

所以 UI 从来不碰 `sourceThread` 或 `streamThread` 的内部对象。

## 5. `VisionRuntime` 类本身很薄

`qt_gui/vision/vision_runtime.h` 里的类成员很少：

- `DashboardBackend *backend_`
- `AppOptions options_`
- `std::atomic<bool> running_`
- `std::thread worker_`

真正的复杂度全部被压进 `workerLoop()`。

`start()` 和 `stop()` 的行为很简单：

- `start()` 用 `running_.exchange(true)` 做幂等保护，然后起 `worker_`
- `stop()` 把 `running_` 置 `false`，再 `join()`

这意味着整条视觉链的生命周期只有一个主 worker 线程入口。

## 6. `vision_runtime.cpp` 顶部常量先决定了运行时轮廓

当前代码里的关键常量如下：

| 常量 | 值 | 含义 |
|---|---|---|
| `kCaptureWidth` | `640` | 摄像头采集宽 |
| `kCaptureHeight` | `480` | 摄像头采集高 |
| `kCaptureBuffers` | `4` | V4L2 缓冲数 |
| `kStreamWidth` | `640` | RTSP 目标宽 |
| `kStreamHeight` | `540` | RTSP 目标高 |
| `kAiWorkerThreads` | `1` | RKNN worker 数 |
| `kAiQueueSize` | `4` | AI 队列长度 |
| `kPostStreamQueueSize` | `4` | post-stream 队列长度 |
| `kPoolPreallocCount` | `6` | 通用 frame pool 预分配数 |
| `kPoolCachedCount` | `8` | 通用 frame pool 缓存上限 |
| `kOverlayPoolPreallocCount` | `2` | overlay pool 预分配数 |
| `kOverlayPoolCachedCount` | `4` | overlay pool 缓存上限 |
| `kDefaultFpsNum` | `30` | 默认 FPS 分子 |
| `kDefaultFpsDen` | `1` | 默认 FPS 分母 |
| `kDefaultRtspBitrateBps` | `0` | 交给 encoder 自动估算码率 |
| `kRtspRetryDelayMs` | `3000` | RTSP 重试等待 |
| `kDeviceId` | `rk3568-001` | 设备 ID |
| `kMqttHost` | `192.168.30.26` | Vision MQTT broker 主机 |
| `kMqttPort` | `1883` | Vision MQTT broker 端口 |

两条 Vision MQTT topic 也在这里写死：

- `argi/device/rk3568-001/vision/detection`
- `argi/device/rk3568-001/stream/status`

## 7. `workerLoop()` 的七个阶段

为了读懂 `workerLoop()`，不要从中间看，要按阶段看。

### 7.1 阶段 A：解析模型和运行时选项

`workerLoop()` 一开始先把几个关键变量定下来：

- `modelPath = resolveModelPath()`
- `inputPath = options_.visionDevice.trimmed()`
- `useV4L2 = isAllowedVisionDevicePath(inputPath)`
- `rtspEnabled = options_.visionRtspEnabled`
- `rtspUrl = options_.visionRtspUrl.trimmed().isEmpty() ? defaultVisionRtspUrl() : options_.visionRtspUrl.trimmed()`

这里有两个容易忽略的事实。

第一，模型查找是多候选回退，不是单一路径：

- `applicationDirPath()/model/yolov5s_relu-640-640-rk3568.rknn`
- `applicationDirPath()/model/yolov5s-640-640.rknn`
- `DASHBOARD_LOCAL_VISION_ROOT/model/yolov5s_relu-640-640-rk3568.rknn`
- `DASHBOARD_LOCAL_VISION_ROOT/model/yolov5s-640-640.rknn`

第二，`stream_url` 是配置值，不是在线状态本身。

即使 RTSP 被禁用，或者流还没推成功，`publishDetection()` 里也会带上当前 `rtspUrl`。

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`VisionRuntime::workerLoop()`
作用：在进入真正采帧和推理之前，先锁定模型路径、输入源模式、RTSP 开关和 RTSP URL。

```cpp
void VisionRuntime::workerLoop() {
    if (backend_ == nullptr) {
        running_.store(false);
        return;
    }

#ifdef DASHBOARD_HAVE_LOCAL_VISION_RUNTIME
    const QString modelPath = resolveModelPath();
    const QString inputPath = options_.visionDevice.trimmed();
    const bool useV4L2 = isAllowedVisionDevicePath(inputPath);
    const bool rtspEnabled = options_.visionRtspEnabled;
    const QString rtspUrl = options_.visionRtspUrl.trimmed().isEmpty()
        ? defaultVisionRtspUrl()
        : options_.visionRtspUrl.trimmed();
    VisionMqttPublisher mqtt;
    QString terminalMessage;
    bool modelReady = false;
    bool inputReady = false;
    int srcWidth = 0;
    int srcHeight = 0;
    int srcFormat = RK_FORMAT_YCbCr_420_SP;
    int srcStride = 0;
    int srcVerStride = 0;
    int fpsNum = kDefaultFpsNum;
    int fpsDen = kDefaultFpsDen;
```

这里最重要的不是变量名，而是顺序。`rtspEnabled` 和 `rtspUrl` 在 `workerLoop()` 一开头就固定下来，后面不再从 UI 线程回读；`VisionMqttPublisher mqtt` 也是这个作用域里的局部对象，所以整个视觉链的 MQTT 生命周期与 `workerLoop()` 完全一致。

### 7.2 阶段 B：初始化 RKNN 和 Vision MQTT

模型路径为空时，逻辑直接失败：

- 更新 `VisionSnapshot` 错误态
- 记 `ERROR` 日志
- `running_ = false`
- 立刻返回

模型路径存在后才创建：

```text
rknnPool<rkYolov5s> aiPool(modelPath, 1, 4)
```

`aiPool.init()` 失败同样直接终止视觉链。

随后初始化本地 Vision MQTT publisher：

```text
VisionMqttPublisher mqtt;
mqtt.open("rk3568-001-vision-runtime", "192.168.30.26", 1883)
```

注意这里的语义：

- MQTT 打不开只会记 `WARN`。
- 推理不会因为 MQTT 失败而停止。
- 后续 publish 会自动降级成“尝试但不成功”。

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`VisionRuntime::workerLoop()` 初始化段、`VisionMqttPublisher::open()`
作用：完成 RKNN worker 池与 Vision MQTT publisher 的真实初始化，并把 MQTT 失败定义为“可降级”而不是“致命”。

```cpp
rknnPool<rkYolov5s> aiPool(modelPath.toStdString(), kAiWorkerThreads, kAiQueueSize);
if (aiPool.init() != 0) {
    terminalMessage = QStringLiteral("RKNN inference pool init failed");
    backend_->updateVisionState(makeStatusSnapshot(terminalMessage, false, false));
    backend_->addLog("ERROR", "VISION", terminalMessage);
    running_.store(false);
    return;
}
modelReady = true;

if (!mqtt.open(kVisionMqttClientId, kMqttHost, kMqttPort)) {
    backend_->addLog(
        "WARN",
        "VISION",
        QStringLiteral("Vision MQTT broker unavailable at %1:%2; publishes will be skipped until the broker is reachable")
            .arg(QString::fromLatin1(kMqttHost))
            .arg(kMqttPort)
    );
}

bool open(const char *clientId, const char *host, int port) {
    int rc = MOSQ_ERR_SUCCESS;

    close();
    rc = mosquitto_lib_init();
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[VISION_MQTT] mosquitto_lib_init failed: %s\n", mosquitto_strerror(rc));
        return false;
    }
    libInitialized_ = true;

    mosq_ = mosquitto_new(clientId, true, this);
    if (mosq_ == nullptr) {
        fprintf(stderr, "[VISION_MQTT] mosquitto_new failed\n");
        close();
        return false;
    }

    mosquitto_connect_callback_set(mosq_, &VisionMqttPublisher::handleConnect);
    mosquitto_disconnect_callback_set(mosq_, &VisionMqttPublisher::handleDisconnect);
    mosquitto_reconnect_delay_set(mosq_, 1u, 5u, true);

    rc = mosquitto_connect_async(mosq_, host, port, 30);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[VISION_MQTT] connect_async failed: %s\n", mosquitto_strerror(rc));
        close();
        return false;
    }

    rc = mosquitto_loop_start(mosq_);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[VISION_MQTT] loop_start failed: %s\n", mosquitto_strerror(rc));
        close();
        return false;
    }

    loopStarted_ = true;
    return true;
}
```

这一段明确区分了两类失败：`aiPool.init()` 失败会直接让视觉链退出，而 `mqtt.open()` 失败只会记 `WARN`。原因也很清楚，RKNN 是主功能依赖，MQTT 是对外输出依赖。当前设计优先保证本地推理与 UI 存活，再接受“发布端暂时不可用”的降级。

### 7.3 阶段 C：打开输入

#### 摄像头支路

当 `useV4L2 == true` 时：

1. 构造 `V4L2Capture capture(inputPath, 640, 480, 4)`
2. `capture.open()`
3. `capture.startStream()`
4. 取回 `width / height / pixelFormat / fps`
5. 用 `toRgaFormat()` 映射到 RGA 格式

如果像素格式不是当前支持集合，直接终止：

- `V4L2_PIX_FMT_YUYV`
- `V4L2_PIX_FMT_NV12`
- `V4L2_PIX_FMT_BGR24`

#### 本地视频文件支路

当输入不是 `/dev/video*` 时：

1. `describeVisionInputError()` 先检查是否存在、可读、普通文件
2. `MppDecoder decoder.open(path)`
3. 先读第一帧，拿到 `srcWidth / srcHeight`
4. 固定整条后续链路的几何尺寸

这里有一个关键设计：

- 文件输入的第一帧决定了整条链路的尺寸。
- 后续如果解码出不同尺寸，代码会直接报错停机。

原因很直接：

- `aiFramePool`
- `rgbBuffer`
- `postStreamBufferPool`
- `MppRtspEncoder`

这些对象都是按固定几何尺寸初始化的，运行中变分辨率会引发不一致。

### 7.4 阶段 D：分配 buffer 和队列

输入可用后，代码统一算出几类 buffer 大小：

- `aiFrameBytes`
- `streamFrameBytes`
- `streamOverlayBytes`
- `rgbBufferBytes`

然后建立三个池和一个队列：

- `FrameCopyPool aiFramePool`
- `FrameCopyPool postStreamBufferPool`
- `FrameCopyPool postStreamOverlayPool`
- `PostStreamFramePool postStreamFramePool`

职责分别是：

| 对象 | 用途 |
|---|---|
| `aiFramePool` | sourceThread 往 aiPool 送帧前的稳定持有缓冲 |
| `postStreamBufferPool` | annotated NV12 输出缓冲 |
| `postStreamOverlayPool` | 叠框时的 BGRA 工作面 |
| `postStreamFramePool` | 交给 RTSP 线程消费的帧队列 |

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`VisionRuntime::workerLoop()`
作用：按输入几何和固定 RTSP 输出几何预分配 AI、RTSP、UI 三条子链真正要共享的缓冲资源。

```cpp
const int streamWidth = kStreamWidth;
const int streamHeight = kStreamHeight;
const int streamStride = alignUp(streamWidth, 16);
const size_t streamFrameBytes = computeNv12Bytes(streamStride, streamHeight);
const size_t streamOverlayBytes = computeBgraBytes(streamWidth, streamHeight);
const size_t rgbBufferBytes = computeRgbBytes(srcWidth, srcHeight);
if (aiFrameBytes == 0U || streamWidth <= 0 || streamHeight <= 0 || streamStride <= 0 ||
    streamFrameBytes == 0U || streamOverlayBytes == 0U || rgbBufferBytes == 0U ||
    rgbBufferBytes > static_cast<size_t>(std::numeric_limits<int>::max())) {
    terminalMessage = QStringLiteral("Unsupported vision frame geometry %1x%2").arg(srcWidth).arg(srcHeight);
    backend_->updateVisionState(makeStatusSnapshot(terminalMessage, false, modelReady));
    backend_->addLog("ERROR", "VISION", terminalMessage);
    running_.store(false);
    return;
}
FrameCopyPool aiFramePool(aiFrameBytes, kPoolPreallocCount, kPoolCachedCount);
FrameCopyPool postStreamBufferPool(streamFrameBytes, kPoolPreallocCount, kPoolCachedCount);
FrameCopyPool postStreamOverlayPool(streamOverlayBytes, kOverlayPoolPreallocCount, kOverlayPoolCachedCount);
PostStreamFramePool postStreamFramePool(kPostStreamQueueSize);
QVector<unsigned char> rgbBuffer(static_cast<int>(rgbBufferBytes));
std::atomic<uint64_t> frameIdGenerator{1U};
bool postStreamBranchActive = rtspEnabled;
```

这段初始化把三条用途不同的内存路线拆开了：`aiFramePool` 只服务 RKNN 输入，`postStreamBufferPool` 和 `postStreamOverlayPool` 只服务 RTSP annotated frame 生成，`rgbBuffer` 只服务 UI 展示。也正因为这些对象在这里按固定几何初始化，后面一旦输入分辨率变化，代码就必须停机而不是“边跑边自适应”。

### 7.5 阶段 E：启动 `streamThread`

只有 `rtspEnabled == true` 时才起 `streamThread`。

这个线程内部维护：

- `MppRtspEncoder encoder`
- `encoderOpen`
- `streamWasOnline`
- `nextRetryAt`

它的控制流是：

1. `waitAndPop()` 等一帧 `PostStreamFrame`
2. 如果 encoder 没开，且还没到重试时间，就丢掉当前帧
3. 到了重试时间，尝试 `encoder.open(rtspUrl, ...)`
4. 成功后发 `stream/status online`
5. 每帧 `encodeAndPush(frame)`
6. 失败则发 `offline/push_failed`，关闭 encoder，等待下次重试
7. 线程退出时，如果曾经在线过，再发一次 `offline/stopped`

这里要特别注意三种 `reason`：

- `open_failed`
- `push_failed`
- `stopped`

另外还有两种状态是在其它位置发的：

- `disabled`
- `post_stream_failed`

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`VisionRuntime::workerLoop()` 里的 `streamThread` lambda
作用：以单独线程消费 `PostStreamFramePool`，串起 `MppRtspEncoder`、重试节流和 `stream/status` MQTT 发布。

```cpp
if (rtspEnabled) {
    streamThread = std::thread([&]() {
        MppRtspEncoder encoder;
        bool encoderOpen = false;
        bool streamWasOnline = false;
        auto nextRetryAt = std::chrono::steady_clock::time_point::min();

        while (true) {
            PostStreamFrame frame;
            if (!postStreamFramePool.waitAndPop(&frame)) {
                break;
            }

            const auto now = std::chrono::steady_clock::now();
            if (!encoderOpen) {
                if (now < nextRetryAt) {
                    release_frame_buffer(frame.data, frame.release_fn, frame.release_ctx);
                    continue;
                }

                if (encoder.open(
                        rtspUrl.toUtf8().constData(),
                        frame.width,
                        frame.height,
                        frame.stride,
                        frame.height,
                        fpsNum,
                        fpsDen,
                        kDefaultRtspBitrateBps) != 0) {
                    backend_->addLog("WARN", "VISION", QStringLiteral("RTSP encoder open failed; stream branch will retry"));
                    publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), rtspUrl, QStringLiteral("open_failed"));
                    nextRetryAt = now + std::chrono::milliseconds(kRtspRetryDelayMs);
                    release_frame_buffer(frame.data, frame.release_fn, frame.release_ctx);
                    continue;
                }

                encoderOpen = true;
                streamWasOnline = true;
                backend_->addLog("INFO", "VISION", QStringLiteral("RTSP push online: %1").arg(rtspUrl));
                publishStreamStatus(backend_, &mqtt, QStringLiteral("online"), rtspUrl);
            }

            if (encoder.encodeAndPush(frame) != 0) {
                backend_->addLog("WARN", "VISION", QStringLiteral("RTSP push failed; stream branch will retry"));
                publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), rtspUrl, QStringLiteral("push_failed"));
                encoder.close();
                encoderOpen = false;
                nextRetryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(kRtspRetryDelayMs);
                release_frame_buffer(frame.data, frame.release_fn, frame.release_ctx);
                continue;
            }

            release_frame_buffer(frame.data, frame.release_fn, frame.release_ctx);
        }

        if (encoderOpen || streamWasOnline) {
            publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), rtspUrl, QStringLiteral("stopped"));
        }
        encoder.close();
    });
} else {
    backend_->addLog("INFO", "VISION", QStringLiteral("RTSP push disabled by option"));
    publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), rtspUrl, QStringLiteral("disabled"));
}
```

这段代码是 RTSP 开关真正落地的位置。`main()` 只是把 `visionRtspEnabled` 放进 `AppOptions`，真正决定“起不起推流线程”的是这里的 `if (rtspEnabled)`。而 `else` 分支也没有沉默跳过，而是明确记日志并发一条 `offline/disabled` 的 retained MQTT 状态。

### 7.6 阶段 F：启动 `sourceThread`

`sourceThread` 是输入侧唯一生产者。

它循环干的事情只有五步：

1. 拿到一帧源数据
2. 生成 `captureTsUs`
3. 生成自增 `frameId`
4. 调 `copyFrameToAiPool()`
5. 如果是摄像头帧，再把原始 V4L2 buffer queue 回去

`copyFrameToAiPool()` 的含义要写清楚：

- 它不是零拷贝。
- 它一定会把源帧拷进 `aiFramePool` 里拿到的独立 buffer。
- 然后把该 buffer 的释放契约连同 `frameId`、时间戳一起交给 `aiPool.put()`。

返回值也要写清：

- `0`：成功进入 AI 池
- `1`：池忙或非致命拒绝，本帧可视为被跳过
- `< 0`：致命错误

当前 `sourceThread` 只把 `< 0` 看成 fatal。

这意味着在压力下允许跳帧，但不因为暂时拥塞直接打死整条链路。

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`VisionRuntime::workerLoop()` 里的 `sourceThread` lambda
作用：作为唯一上游生产者抓取 V4L2 或解码帧，生成 `frameId` / `captureTsUs`，并把独立拥有的 buffer 交给 RKNN 池。

```cpp
std::thread sourceThread([&]() {
    bool usePrefetchedDecodedFrame = prefetchedDecodedFrame;

    while (running_.load()) {
        const void *srcData = nullptr;
        size_t srcSize = 0U;
        int currentWidth = srcWidth;
        int currentHeight = srcHeight;
        int currentFormat = srcFormat;
        int currentStride = srcStride;
        int currentVerStride = srcVerStride;
        long long captureTsUs = nowWallTimeUs();
        V4L2Frame v4l2Frame;
        QString fatalCopyError;

        if (useV4L2) {
            if (capture.dequeueFrame(&v4l2Frame) != 0) {
                sourceError = QStringLiteral("Camera frame capture failed");
                backend_->addLog("ERROR", "VISION", sourceError);
                running_.store(false);
                break;
            }
            srcData = v4l2Frame.data;
            srcSize = static_cast<size_t>(v4l2Frame.size);
            if (v4l2Frame.capture_ts_us > 0) {
                captureTsUs = v4l2Frame.capture_ts_us;
            }
        } else {
            if (usePrefetchedDecodedFrame) {
                srcData = decodedFrameData;
                srcSize = decodedFrameSize;
                usePrefetchedDecodedFrame = false;
            } else {
                unsigned char *decodedFrame = nullptr;
                if (decoder.readFrame(&decodedFrame, &currentWidth, &currentHeight) != 0) {
                    break;
                }
                srcData = decodedFrame;
            }
            currentFormat = RK_FORMAT_YCbCr_420_SP;
            currentStride = currentWidth;
            currentVerStride = currentHeight;
        }

        const uint64_t frameId = frameIdGenerator.fetch_add(1U, std::memory_order_relaxed);
        const int aiCopyRc = copyFrameToAiPool(
            srcData,
            srcSize,
            currentWidth,
            currentHeight,
            currentFormat,
            frameId,
            captureTsUs,
            &aiPool,
            &aiFramePool
        );
        if (aiCopyRc < 0) {
            fatalCopyError = QStringLiteral("AI frame fan-out failed for %1x%2 input")
                                 .arg(currentWidth)
                                 .arg(currentHeight);
        }

        if (useV4L2 && capture.queueFrame(v4l2Frame) != 0) {
            sourceError = QStringLiteral("Camera frame queue failed");
            backend_->addLog("ERROR", "VISION", sourceError);
            running_.store(false);
            break;
        }
        if (!fatalCopyError.isEmpty()) {
            sourceError = fatalCopyError;
            backend_->addLog("ERROR", "VISION", sourceError);
            running_.store(false);
            break;
        }
    }

    aiPool.notifyGetters();
});
```

这段代码把 `sourceThread` 的边界画得很死：它只负责拿帧、标时间、入池、回队，不负责 UI、MQTT、RTSP。还有一个容易漏写的细节是 `frameIdGenerator.fetch_add()` 发生在这里，所以后面 detection 消息、RTSP 帧和日志里的 `frame_id` 都是由生产者线程统一分配的。

### 7.7 阶段 G：主线程消费 `aiPool.get()`

`workerLoop()` 自己作为单消费者，不断 `aiPool.get()`：

1. 拿到 `detect_result_group_t`
2. 拿到原始帧指针与释放函数
3. 构建 `VisionSnapshot`
4. 可选生成 RTSP annotated frame
5. 把源帧转成 RGB 供 UI 展示
6. 有检测目标时发 Vision detection MQTT
7. `backend_->updateVisionState(snapshot)`

这是整条链最关键的扇出点。

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`VisionRuntime::workerLoop()` 主循环
作用：从 `aiPool.get()` 取回推理结果后，同时向 RTSP 支路、UI 快照和 Vision MQTT 三个出口扇出。

```cpp
while (running_.load()) {
    detect_result_group_t detGroup;
    float scaleW = 0.0f;
    float scaleH = 0.0f;
    unsigned char *frameData = nullptr;
    int frameWidth = 0;
    int frameHeight = 0;
    int frameFormat = 0;
    uint64_t frameId = 0U;
    long long captureTsUs = 0LL;
    void (*releaseFn)(unsigned char *, void *) = nullptr;
    void *releaseCtx = nullptr;
    VisionSnapshot snapshot;

    memset(&detGroup, 0, sizeof(detGroup));
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

    snapshot.cameraOnline = inputReady;
    snapshot.modelLoaded = modelReady;
    snapshot.statusReported = true;
    snapshot.frameCount = ++frameCount;

    if (frameData == nullptr) {
        snapshot.errorMsg = QStringLiteral("AI pool returned an empty frame payload");
    } else {
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
                backend_->addLog(
                    "WARN",
                    "VISION",
                    QStringLiteral("RTSP post-stream frame build failed; disabling RTSP branch while keeping inference and display active")
                );
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

        if (rga_resize_convert_vaddr(
                       frameData,
                       frameWidth,
                       frameHeight,
                       frameFormat,
                       rgbBuffer.data(),
                       frameWidth,
                       frameHeight,
                       RK_FORMAT_RGB_888) == 0) {
            QImage rgbImage(
                rgbBuffer.constData(),
                frameWidth,
                frameHeight,
                frameWidth * 3,
                QImage::Format_RGB888
            );
            snapshot.detections = formatDetections(detGroup);
            snapshot.frame = renderAnnotatedFrame(rgbImage, detGroup);
            if (detGroup.count > 0) {
                publishDetection(backend_, &mqtt, rtspUrl, frameId, captureTsUs, detGroup);
            }
        }
    }

    if (frameData != nullptr) {
        release_frame_buffer(frameData, releaseFn, releaseCtx);
    }

    snapshot.fps = fps;
    backend_->updateVisionState(snapshot);
}
```

这个主循环是真正的“推理后扇出点”。一帧数据从 `aiPool.get()` 回来之后，会先尝试生成 RTSP annotated frame，再转 RGB 供 UI，最后在 `detGroup.count > 0` 时发布 detection MQTT。也正因为三个出口都挂在这里，所以 `release_frame_buffer(frameData, releaseFn, releaseCtx)` 必须放在扇出完成之后。

## 8. 为什么 UI 画框和 RTSP 画框不是一回事

当前代码有两种“画框”行为，但目标不同。

### 8.1 UI 画框

路径是：

1. `rga_resize_convert_vaddr(..., RK_FORMAT_RGB_888)`
2. 构造 `QImage rgbImage`
3. `renderAnnotatedFrame(rgbImage, detGroup)`

最终产物是给 `VisionSnapshot.frame` 用的 `QImage`。

### 8.2 RTSP 画框

路径是：

1. `copyPostInferFrameToPostStreamPool()`
2. `rga_resize_convert_vaddr(..., RK_FORMAT_BGRA_8888)`
3. 在 BGRA overlay 面上 `paintDetections()`
4. `rga_resize_to_nv12_vaddr(...)`
5. 生成 `PostStreamFrame`

最终产物不是 `QImage`，而是可供 MPP 编码的 NV12。

所以不要把 UI 画框函数和 RTSP 输出混为一个概念。

## 9. `copyPostInferFrameToPostStreamPool()` 是 RTSP 支路的关键枢纽

这个函数做了四件事：

1. 从 `postStreamBufferPool` 申请 NV12 输出缓冲。
2. 从 `postStreamOverlayPool` 申请 BGRA overlay 工作缓冲。
3. 把源帧缩放/转换到 BGRA。
4. 在 BGRA 上画框，再转回 NV12，塞进 `postStreamFramePool`。

如果你问“为什么不直接在原始帧上画”，答案是当前代码的约束不是只考虑绘制本身，而是同时考虑：

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`copyPostInferFrameToPostStreamPool()`
作用：把 `aiPool.get()` 取回的原始推理帧，构造成 RTSP 分支真正消费的 annotated NV12。

```cpp
streamBuffer = postStreamBufferPool->acquire();
if (streamBuffer == nullptr) {
    return 1;
}

overlayBuffer = postStreamOverlayPool->acquire();
if (overlayBuffer == nullptr) {
    release_frame_buffer(streamBuffer, release_pooled_buffer, postStreamBufferPool);
    return 1;
}

if (rga_resize_convert_vaddr(
        const_cast<void *>(srcData),
        width,
        height,
        srcFormat,
        overlayBuffer,
        streamWidth,
        streamHeight,
        RK_FORMAT_BGRA_8888) != 0) {
    release_frame_buffer(overlayBuffer, release_pooled_buffer, postStreamOverlayPool);
    release_frame_buffer(streamBuffer, release_pooled_buffer, postStreamBufferPool);
    return -1;
}

QImage overlayFrame(
    overlayBuffer,
    streamWidth,
    streamHeight,
    streamWidth * 4,
    QImage::Format_ARGB32
);
QPainter overlayPainter(&overlayFrame);
paintDetections(&overlayPainter, group, width, height, streamWidth, streamHeight);

if (rga_resize_to_nv12_vaddr(
        overlayBuffer,
        streamWidth,
        streamHeight,
        streamWidth,
        streamHeight,
        RK_FORMAT_BGRA_8888,
        streamBuffer,
        streamWidth,
        streamHeight,
        streamStride,
        streamHeight) != 0) {
    release_frame_buffer(overlayBuffer, release_pooled_buffer, postStreamOverlayPool);
    release_frame_buffer(streamBuffer, release_pooled_buffer, postStreamBufferPool);
    return -1;
}

PostStreamFrame frame;
frame.data = streamBuffer;
frame.size = streamBytes;
frame.width = streamWidth;
frame.height = streamHeight;
frame.stride = streamStride;
frame.format = RK_FORMAT_YCbCr_420_SP;
frame.frame_id = frameId;
frame.timestamp_us = captureTsUs;
frame.release_fn = release_pooled_buffer;
frame.release_ctx = postStreamBufferPool;
postStreamFramePool->enqueue(std::move(frame));
return 0;
```

这段构建逻辑说明 RTSP 分支拿到的从来不是 UI 截图，也不是 `QImage` 本身，而是重新编码前最后一版 NV12。`paintDetections()` 画上去的框和标签会被实际烧进后续推流画面，而 `frame.release_fn` / `release_ctx` 则保证 `streamThread` 在编码后能把这块 NV12 缓冲正确归还给池。

- 输入帧格式可能不同
- RTSP 输出要求 NV12
- Qt 叠框最方便的是 BGRA/ARGB 可见平面
- 需要把推理主链和推流支路的帧生命周期隔离开

它的错误语义也要写清：

- 返回 `1`：通常表示池忙，当前帧可能被跳过
- 返回 `< 0`：致命错误，主循环会停掉 RTSP 支路

停支路的动作是：

- `postStreamBranchActive = false`
- 记 `WARN`
- 发布 `stream/status offline, reason=post_stream_failed`
- `postStreamFramePool.stop()`

但注意：

- 推理主链不因此退出
- UI 显示不因此退出

## 10. `MppRtspEncoder` 负责的不是“拉流”，而是“编码后推流”

这个类在 vendor 目录里：

- `include/mpp_encoder_rtsp.h`
- `src/mpp_encoder_rtsp.cc`

它做的事可以分成五步。

### 10.1 `open()`

记录：

- `rtsp_url`
- `width/height`
- `hor_stride/ver_stride`
- `fps`
- `bitrate`

其中码率 `0` 不表示零码率，而是交给 `clamp_bitrate()` 自动估算：

- 估算公式近似是 `width * height * fps`
- 最小 1 Mbps
- 最大 8 Mbps

### 10.2 `initMpp()`

初始化 Rockchip MPP 编码器：

- `MPP_CTX_ENC`
- `MPP_VIDEO_CodingAVC`
- `MPP_ENC_RC_MODE_CBR`
- `prep:format = MPP_FMT_YUV420SP`

这里已经把输入格式钉死成 NV12。

### 10.3 `initRtspOutput()`

初始化 FFmpeg RTSP 输出：

- `avformat_alloc_output_context2(..., "rtsp", url)`
- 新建 `video_stream_`
- `codec_id = AV_CODEC_ID_H264`
- `format = AV_PIX_FMT_NV12`
- `time_base = 1/90000`
- `rtsp_transport = tcp`
- `muxdelay = 0`
- `pkt_size = 1200`

这说明它不是本地起一个 RTSP server，而是作为 RTSP client 往目标地址写。

### 10.4 `encodeAndPush()`

这是最核心的帧级逻辑：

1. 检查 `PostStreamFrame` 必须是 NV12。
2. 如分辨率不匹配，调用 `reopenForFrame()` 重开 encoder。
3. 把源 `PostStreamFrame` 逐行拷贝进 MPP 输入缓冲，处理 stride。
4. `mpp_frame_set_pts(frame.timestamp_us)`
5. `encode_put_frame()`
6. 循环 `encode_get_packet()`
7. 每个 packet 调 `writeMppPacket()`

这里有两个关键事实。

第一，`encodeAndPush()` 仍然会把外部 NV12 拷贝进自己的 MPP input buffer，所以它不是严格零拷贝。

第二，时间戳来自 `PostStreamFrame.timestamp_us`，而这个时间戳又来自采集或解码侧的 `captureTsUs`，不是 FFmpeg 自己瞎生。

### 10.5 `writeMppPacket()`

这个函数把 MPP 输出 packet 转成 FFmpeg `AVPacket` 并写出。

时间处理规则是：

- 第一帧的 `capture_ts_us` 作为基准点
- 后续 `pts/dts` 用“相对第一帧的微秒偏移”换算到 `1/90000`
- `duration` 默认按 FPS 推导
- 如果相邻两帧捕获时间差更合理，就用真实时间差

这让 RTSP 时间轴更接近真实采集时间。

## 11. Vision MQTT publish 是怎样接进来的

视觉侧 MQTT wrapper 也在 `vision_runtime.cpp` 内部定义，而不是复用 `linux_app/mqtt_publisher.c`。

它当前只支持：

- `open()`
- `close()`
- `isConnected()`
- `publish()`

它当前不支持：

- `subscribe()`
- message callback 处理 command
- 任意 topic router

这就决定了 Vision 侧当前只有“往外发”，没有“从 broker 收命令”。

### 11.1 `publishStreamStatus()`

流状态 payload 固定字段是：

| 字段 | 含义 |
|---|---|
| `device_id` | `rk3568-001` |
| `type` | 固定为 `stream_status` |
| `state` | `online` 或 `offline` |
| `protocol` | 固定为 `rtsp` |
| `codec` | 固定为 `h264` |
| `url` | 当前 RTSP 目标地址 |
| `reason` | 可选，描述离线原因 |

它使用 `retain = true`。

这很合理，因为订阅端更关心“最近一次流状态”。

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`publishStreamStatus()`
作用：把 RTSP 在线状态打成 retained MQTT 消息，供外部订阅端查询最近一次状态。

```cpp
void publishStreamStatus(
    DashboardBackend *backend,
    VisionMqttPublisher *mqtt,
    const QString &state,
    const QString &streamUrl,
    const QString &reason = QString()
) {
    QJsonObject payload{
        {QStringLiteral("device_id"), QString::fromLatin1(kDeviceId)},
        {QStringLiteral("type"), QStringLiteral("stream_status")},
        {QStringLiteral("state"), state},
        {QStringLiteral("protocol"), QStringLiteral("rtsp")},
        {QStringLiteral("codec"), QStringLiteral("h264")},
        {QStringLiteral("url"), streamUrl}
    };

    if (!reason.isEmpty()) {
        payload.insert(QStringLiteral("reason"), reason);
    }
    logPublishedMessage(backend, mqtt, QString::fromLatin1(kTopicStreamStatus), payload, true);
}
```

这里的 `retain = true` 不是文档推断，而是实码行为。也就是说，新订阅者即使错过了上线瞬间，仍然能从 broker 拿到最近一条 `stream/status`，这和 detection 事件流的语义明显不同。

### 11.2 `publishDetection()`

检测消息字段是：

| 字段 | 含义 |
|---|---|
| `device_id` | `rk3568-001` |
| `type` | `vision_detection` |
| `frame_id` | sourceThread 生成的帧号 |
| `ts_us` | 捕获时间戳 |
| `objects[]` | 每个目标的 `class/conf/box` |
| `stream_url` | 当前 RTSP 配置地址 |

发布条件只有一个：

- `detGroup.count > 0`

也就是说，空帧不发 detection。

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`publishDetection()`
作用：把当前帧检测结果编码成 `vision_detection` JSON，并带上 `frame_id`、`ts_us`、`stream_url` 这些跨链路关联字段。

```cpp
void publishDetection(
    DashboardBackend *backend,
    VisionMqttPublisher *mqtt,
    const QString &streamUrl,
    uint64_t frameId,
    long long captureTsUs,
    const detect_result_group_t &group
) {
    QJsonArray objects;
    for (int i = 0; i < group.count; ++i) {
        const detect_result_t &det = group.results[i];
        QJsonArray box;
        box.append(det.box.left);
        box.append(det.box.top);
        box.append(det.box.right);
        box.append(det.box.bottom);

        QJsonObject object{
            {QStringLiteral("class"), QString::fromLocal8Bit(det.name)},
            {QStringLiteral("conf"), det.prop},
            {QStringLiteral("box"), box}
        };
        objects.append(object);
    }

    QJsonObject payload{
        {QStringLiteral("device_id"), QString::fromLatin1(kDeviceId)},
        {QStringLiteral("type"), QStringLiteral("vision_detection")},
        {QStringLiteral("frame_id"), static_cast<qint64>(frameId)},
        {QStringLiteral("ts_us"), static_cast<qint64>(captureTsUs)},
        {QStringLiteral("objects"), objects},
        {QStringLiteral("stream_url"), streamUrl}
    };

    logPublishedMessage(backend, mqtt, QString::fromLatin1(kTopicVisionDetection), payload, false);
}
```

这里能看出 detection 消息不是“简单扔个类别名”，而是把框坐标、置信度、帧号、时间戳和 RTSP URL 都一起发了出去。这样外部系统既能把事件和视频流关联，也能用 `frame_id` 对齐本地日志和 UI 侧观测。

### 11.3 发布成功与日志计数

`logPublishedMessage()` 的语义很重要：

- 先把 payload 压成 compact JSON
- 调 `mqtt.publish(...)`
- 只有成功时才 `backend->addMqttPublishLog(...)`

所以 `SystemLogPage` 里显示的 MQTT 次数并不是“尝试次数”，而是“成功写入 broker 的次数”。

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`logPublishedMessage()`
作用：把“尝试发布”和“成功记日志”拆开，确保 Dashboard 上的 MQTT 计数只统计真正写进 broker 的消息。

```cpp
void logPublishedMessage(DashboardBackend *backend, VisionMqttPublisher *mqtt, const QString &topic, const QJsonObject &payload, bool retain = false) {
    const QByteArray encoded = compactJson(payload);
    const bool published = mqtt != nullptr && mqtt->publish(topic.toUtf8().constData(), encoded, retain);

    if (backend != nullptr && published) {
        backend->addMqttPublishLog(topic, QString::fromUtf8(encoded));
    }
}
```

这一层包装很关键，因为它决定了 UI 上看到的 MQTT 次数是“成功次数”而不是“调用次数”。如果 broker 断线，`publishDetection()` / `publishStreamStatus()` 仍然可以被调用，但没有成功写入时，`DashboardBackend` 不会被记一条伪成功日志。

## 12. 错误路径与降级策略

为了排故，最好按“是否停机”来记。

### 12.1 会直接导致视觉链退出

- 模型文件找不到
- `aiPool.init()` 失败
- 摄像头打开失败
- 摄像头起流失败
- 摄像头像素格式不支持
- 文件输入不存在或不可读
- `decoder.open()` 失败
- 首帧解码失败
- 解码后尺寸非法
- `sourceThread` 采集失败
- `sourceThread` queue 回摄像头帧失败
- RGB buffer 尺寸溢出

### 12.2 会停掉 RTSP 支路，但保留推理和 UI

- `copyPostInferFrameToPostStreamPool()` 返回致命错误
- `MppRtspEncoder::open()` 失败
- `MppRtspEncoder::encodeAndPush()` 失败
- 启动时明确 `--disable-vision-rtsp`

### 12.3 不会停机，但 MQTT 不可用

- `VisionMqttPublisher::open()` 失败
- broker 运行中断开
- publish 调用失败

这时表现是：

- 视觉检测仍然继续
- UI 仍然刷新
- RTSP 仍可继续，前提是它自己的依赖正常
- 只有 MQTT 日志和计数不会增长

## 13. 依赖边界和构建边界

这部分很适合拿去写部署说明。

### 13.1 Qt 目标直接编进了哪些 vendor 源文件

`qt_gui/CMakeLists.txt` 直接把这些 `.cc` 编进 `rf_dashboard_qt5`：

- `rkYolov5s.cc`
- `preprocess.cc`
- `postprocess.cc`
- `v4l2_capture.cc`
- `mpp_decoder.cc`
- `mpp_encoder_rtsp.cc`

说明这不是运行时“外挂一个独立视觉服务”，而是链接进同一 Qt 可执行文件。

### 13.2 强制依赖

- `librknnrt.so`
- `librga.so`
- `librockchip_mpp.so`
- `libavformat`
- `libavcodec`
- `libavutil`
- `libmosquitto`

少任何一类，当前配置都会构建失败，而不是偷偷降级。

### 13.3 不再接受 fake-success runtime

当前 `CMakeLists.txt` 和 `workerLoop()` 都明确表达了一个边界：

- 没有 `DASHBOARD_HAVE_LOCAL_VISION_RUNTIME` 时不再假装本地视觉可用。
- 这是“显式失败”，不是“静默禁用”。

## 14. 当前没有实现什么

这一节必须写得非常保守。

当前没有实现：

- MQTT subscribe
- MQTT command handler
- `gateway/{gateway_id}/cmd`
- recorder
- 事件触发录像
- `record_done`
- 录像完成回执元数据

所以文档不应该出现这些说法：

- “VisionRuntime 会订阅 MQTT 命令”
- “收到命令后会启动录像”
- “录像完成会发 `record_done`”

这些都不是当前代码事实。

## 15. 如果你要改代码，先从哪几处下手

### 改输入源相关

先读：

- `qt_gui/core/app_options.h`
- `qt_gui/app/main.cpp`
- `qt_gui/vision/vision_runtime.cpp` 里输入初始化与 `sourceThread`

### 改 MQTT publish 相关

先读：

- `VisionMqttPublisher`
- `logPublishedMessage()`
- `publishStreamStatus()`
- `publishDetection()`
- [mqtt_publish_tutorial_zh.md](mqtt_publish_tutorial_zh.md)

### 改 RTSP push 相关

先读：

- `copyPostInferFrameToPostStreamPool()`
- `PostStreamFramePool`
- `MppRtspEncoder`
- [rtsp_push_tutorial_zh.md](rtsp_push_tutorial_zh.md)

## 16. 一段最实用的心智模型

把当前 VisionRuntime 想成三层就不容易迷路：

第一层，输入层。

- `sourceThread`
- `V4L2Capture` / `MppDecoder`
- `aiFramePool`

第二层，推理与扇出层。

- `aiPool`
- `aiPool.get()`
- `VisionSnapshot`
- `publishDetection()`
- `copyPostInferFrameToPostStreamPool()`

第三层，输出层。

- Qt 页面消费 `VisionSnapshot`
- MQTT broker 消费 Vision publish
- RTSP 服务器消费 H.264 over RTSP

当前代码已经把这三层分得很清楚；真正没做完的不是这三层内部，而是“更上层的命令输入和录像闭环”。
