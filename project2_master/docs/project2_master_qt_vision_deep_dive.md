# `project2_master/qt_gui` 深读：Qt / vision 启动链路

本文只讲 `project2_master/qt_gui` 里的真实 master 侧代码，不讲 `pc sim`、vendor、生成文件，也不把离线回放当成主链路。

先把总链路摆出来：

```text
main.cpp
  -> AppOptions / Dark Palette
  -> MainWindow
     -> DashboardBackend
     -> RFGatewayClient
     -> VisionRuntime
     -> RFStatusPage / VisionPage / SystemLogPage
     -> WaveformWidget
```

你要先抓住的点是：
- `main.cpp` 只负责进程入口、参数校验和窗口启动，不做业务。
- `MainWindow` 只负责组装 UI 和启动运行时，不做协议解析和推理。
- `DashboardBackend` 是唯一的共享状态中枢，负责跨线程读写隔离。
- `RFGatewayClient` 和 `VisionRuntime` 是两条生产链路，前者吃外部进程 stdout，后者吃本地摄像头帧。
- 各个 `Page` 不是主动生产数据，而是定时从 `backend` 拉快照。
- `WaveformWidget` 只是画脉冲，不负责解析 RF。

## 0. 阅读地图：先读哪三段

先把这三段当作顺序导航，不要从中间跳：
- `main.cpp` -> 入口约束、参数、主题和主窗口创建。
- `common_types.h` / `dashboard_backend.*` -> snapshot 的形状、写入点、读取点。
- `rf_gateway_client.*` / `vision_runtime.*` -> 两条生产链路，分别看外部进程 stdout 和本地摄像头帧。
如果只想先建立全局，先看 `0`、`4`、`9`。

先按这个顺序读，不要反着啃：

1. UI 入口：先看 `main.cpp`，再看 `MainWindow::setupUi()` / `setupRuntime()`。这段的作用是先把窗口壳子、页签、状态栏、定时器拼起来。
2. 共享状态：先看 `common_types.h`，再看 `dashboard_backend.*`。这段的作用是先认清 snapshot 的形状，再看谁写、谁读。
3. RF / vision runtime：先看 `rf_gateway_client.*`，再看 `vision_runtime.*`。这段的作用是把两条生产链路拆开看，一个吃外部进程 stdout，一个吃本地摄像头帧。

你要抓住的点是：
- 不是页面自己拉业务数据，而是后台先写，页面定时读。
- 不是先啃控件细节，而是先看 `DashboardBackend` 怎么把状态收口。
- 不是把 RF 和 vision 当成一条链路，而是两条链路分别生产，再汇进同一个状态中枢。

## 1. 入口链路：`main.cpp`

代码先看入口：

```cpp
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("rf_dashboard_qt5"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt5 C++ Dashboard for RF Gateway + Vision monitor"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QString defaultRfInput = dashboard::defaultRFInputPath();
    const QString defaultVisionDevice = dashboard::defaultVisionDevicePath();

    parser.addOption(rfInputOption);
    parser.addOption(visionDeviceOption);
    parser.process(app);

    dashboard::AppOptions options;
    ...
    dashboard::applyDarkPalette(app);

    dashboard::MainWindow window(options);
    window.show();
    return app.exec();
}
```

这段的作用是把整个进程边界钉死。

依赖：
- `QApplication` 提供 Qt Widgets 事件循环。
- `QCommandLineParser` 负责启动参数解析。
- `AppOptions` 提供运行时配置承载。
- `applyDarkPalette()` 统一视觉风格。
- `MainWindow` 是真正的 UI 入口。

输入：
- `argc/argv`
- `--rf-input`
- `--vision-device`

输出：
- 一个已经完成参数约束、主题初始化和主窗口构造的 Qt 应用。

去向：
- 参数通过后，进入 `MainWindow`。
- `window.show()` 后进入 `app.exec()` 事件循环。

为什么这样设计：
- `main` 只做最早期的硬约束。它不碰 RF 协议，也不碰视觉推理。
- `--rf-input` 在 master 侧不是自由配置，而是固定路径约束。
- `--vision-device` 默认是本地 `/dev/video9`，但也允许可读的本地视频文件；两者都走同一个板侧本地运行时，不存在可接受的 stub 成功路径。

你要抓住的点是：
- `main.cpp` 的职责不是“启动所有功能”，而是“把能不能启动、从哪里启动”先判断清楚。
- 这个入口已经把 master 侧的运行边界定住了：RF 用固定输入，视觉默认用本地摄像头，也允许切到可读本地视频文件。

## 2. 启动参数与主题：`app_options.h` / `app_palette.*`

先看 `AppOptions`：

```cpp
struct AppOptions {
    QString rfInput;
    QString visionDevice;
};

inline QString defaultRFInputPath() {
    return QStringLiteral("/dev/rf433");
}

inline QString defaultVisionDevicePath() {
    return QStringLiteral("/dev/video9");
}

inline bool isAllowedVisionDevicePath(const QString &path) {
    return path.startsWith(QStringLiteral("/dev/video"));
}

inline bool isReadableVisionInputFile(const QString &path) {
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.isReadable();
}

inline bool isAllowedVisionInputPath(const QString &path) {
    if (isAllowedVisionDevicePath(path)) {
        return true;
    }
    return isReadableVisionInputFile(path);
}
```

这段的作用是把启动参数收口成两个字段。

依赖：
- `QString`
- `QFileInfo`

输入：
- `main.cpp` 解析后的命令行参数。

输出：
- `rfInput`
- `visionDevice`

去向：
- `MainWindow` 构造函数。
- `RFGatewayClient` 和 `VisionRuntime` 的初始化。

为什么这样设计：
- 这里没有业务逻辑，只有配置载体。
- 默认路径和允许规则都放在这里，避免入口和运行时到处散落常量。
- `isAllowedVisionDevicePath()` 和 `isAllowedVisionInputPath()` 的分工是“设备路径快速判定”和“启动前总入口校验”。

再看主题：

```cpp
void applyDarkPalette(QApplication &app) {
    app.setStyle(QStringLiteral("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(30, 30, 30));
    ...
    app.setPalette(palette);
    app.setStyleSheet(QStringLiteral(...));
}
```

这段的作用是把整个 Qt 应用的视觉基调统一掉。

依赖：
- `QApplication`
- `QPalette`
- `QColor`

输入：
- 当前应用实例。

输出：
- 统一的 Fusion 样式、深色调色板和控件样式表。

去向：
- 所有页面、表格、按钮、状态栏、文本框都继承这一套风格。

为什么这样设计：
- 这个工程的 UI 是仪表盘，不是文档编辑器。统一深色主题能让状态、波形、图像、日志更容易区分。
- 主题在 `MainWindow` 创建前设置，避免页面先按默认风格渲染再被切换。

你要抓住的点是：
- `AppOptions` 管配置，`applyDarkPalette()` 管外观。
- 这两者都属于“启动期公共约束”，不是业务模块。

## 3. 窗口组装：`main_window.*`

先看构造关系：

```cpp
MainWindow::MainWindow(const AppOptions &options, QWidget *parent)
    : QMainWindow(parent),
      options_(options),
      rfClient_(&backend_, options_, this),
      visionRuntime_(&backend_, options_) {
    setupUi();
    setupRuntime();
}
```

这段的作用是把所有运行时对象在一个地方组装起来。

依赖：
- `DashboardBackend`
- `RFGatewayClient`
- `VisionRuntime`
- 三个页面类

输入：
- `AppOptions`
- `QWidget *parent`

输出：
- 一个已经完成对象绑定的主窗口实例。

去向：
- 页面只读 `backend_`。
- `rfClient_` 和 `visionRuntime_` 写 `backend_`。

为什么这样设计：
- `backend_` 先作为成员存在，后面的 client/runtime 才有共享状态可写。
- `rfClient_` 明确拿到 `this` 作为 QObject 上下文，方便 `QProcess` 信号槽生命周期管理。
- `visionRuntime_` 不挂 Qt 信号槽，而是单独线程运行，避免把推理线程和 UI 线程缠在一起。

### 3.1 `setupUi()`

`setupUi()` 的职责是把页面和状态栏摆出来：

```cpp
auto *tabs = new QTabWidget(this);
rfPage_ = new RFStatusPage(&backend_, tabs);
visionPage_ = new VisionPage(&backend_, tabs);
logPage_ = new SystemLogPage(&backend_, tabs);
tabs->addTab(rfPage_, QStringLiteral("RF 状态"));
tabs->addTab(visionPage_, QStringLiteral("视觉监控"));
tabs->addTab(logPage_, QStringLiteral("系统日志"));
setCentralWidget(tabs);

statusLabel_ = new QLabel(QStringLiteral("系统启动中..."), this);
statusBar()->addPermanentWidget(statusLabel_);
QObject::connect(&statusTimer_, &QTimer::timeout, this, [this]() { updateStatusBar(); });
statusTimer_.start(2000);
```

这段的作用是把 UI 组装成“一个窗口、三个页签、一个状态栏”。

依赖：
- `QTabWidget`
- `QStatusBar`
- `QTimer`

输入：
- `backend_` 的只读指针。

输出：
- `RFStatusPage`
- `VisionPage`
- `SystemLogPage`
- 状态栏定时刷新

去向：
- 所有页面都通过 `backend_` 拉快照。
- 状态栏 2 秒刷新一次。

为什么这样设计：
- 页面不是互相通信，而是统一从 `backend_` 读。
- 这种结构把“生产数据”和“展示数据”分开，页面刷新不会反向影响运行时。

### 3.2 `setupRuntime()`

```cpp
backend_.addLog("INFO", "SYSTEM", QStringLiteral("Qt5 前端已启动"));
backend_.addLog("INFO", "VISION", QStringLiteral("板侧本地视觉链路已接入，默认使用 %1").arg(options_.visionDevice));

rfClient_.start();
visionRuntime_.start();
updateStatusBar();
```

这段的作用是让 UI 启动后立刻进入可观测状态。

依赖：
- `DashboardBackend::addLog()`
- `RFGatewayClient::start()`
- `VisionRuntime::start()`

输入：
- 当前 `options_`

输出：
- 启动日志
- RF 采集链路启动
- 视觉运行时启动

去向：
- 日志页可立即看到启动记录。
- RF 页和视觉页随后台状态变化刷新。

为什么这样设计：
- 先写日志，再拉起运行时，能让页面最早看到“发生了什么”。
- `updateStatusBar()` 立刻跑一次，是为了避免空白窗口。

### 3.3 `updateStatusBar()`

```cpp
const SystemStats stats = backend_.snapshotSystemStats();
const RFSnapshot rf = backend_.snapshotRF();
const VisionSnapshot vision = backend_.snapshotVisionState();

statusLabel_->setText(
    vision.statusReported
        ? QString("运行 %1s | RF帧 %2 | Vision FPS %3 | CPU %4%")
        : QString("运行 %1s | RF帧 %2 | Vision 未接入 | CPU %3%")
);
```

这段的作用是把三个后台快照压成一行状态。

依赖：
- `SystemStats`
- `RFSnapshot`
- `VisionSnapshot`

输入：
- 后端快照。

输出：
- 状态栏文本。

去向：
- 仅展示给用户。

为什么这样设计：
- 状态栏不直接读线程内部变量，只读快照。
- `vision.statusReported` 不是“成功”，而是“视觉线程已经回写过状态”。这一点要和 `modelLoaded`、`cameraOnline` 区分开。

你要抓住的点是：
- `MainWindow` 是胶水层，不是业务层。
- 页面和运行时都不直接碰彼此，统一经过 `DashboardBackend`。

### 1. 组件职责表

先看这张表，再回头看 3.1 / 3.2 / 3.3。

这段的作用是把边界卡死，避免后面读代码时把“谁负责生产”看成“谁负责展示”。

| 组件 | 这段的作用是 | 输入 | 输出 | 不是它负责的 |
| --- | --- | --- | --- | --- |
| `MainWindow` | 组装 UI、拉起 runtime、维持状态栏节奏 | `AppOptions`、`DashboardBackend` | 页签、状态栏、启动调用 | 不是协议解析，也不是模型推理 |
| `DashboardBackend` | 共享状态中枢，负责线程安全快照 | RF 事件、视觉快照、日志、系统统计 | `RFSnapshot` / `VisionSnapshot` / `SystemStats` / `LogEntry` | 不是设备 IO，也不是 UI 绘制 |
| `RFGatewayClient` | 外部 `rf_gateway` 进程适配器 | 固定程序路径、`--rf-input`、stdout | RF 状态、`RFEvent`、日志 | 不是页面刷新器，也不是 RF 协议本体 |
| `VisionRuntime` | 本地摄像头 + RKNN worker | `visionDevice`、本地模型、摄像头帧 | `VisionSnapshot`、视觉日志 | 不是 Qt 主线程任务，也不是页面对象 |
| 各 `Page` | 只读快照渲染层 | `backend_->snapshot*()` | 控件文本、表格、图像 | 不是主动生产业务状态 |

你要抓住的点是：
- `MainWindow` 只负责把东西接起来，不负责把数据算出来。
- `DashboardBackend` 不是缓存一份数据这么简单，它是整个 UI 的状态收口点。
- `Page` 的职责是“读快照并渲染”，不是“直接摸运行时对象”。

## 4. 共享状态中枢：`common_types.h` / `dashboard_backend.*`

先看数据契约：

```cpp
struct RFEvent {
    QDateTime timestamp;
    QString address;
    QString key;
    double confidence = 0.0;
    QString source;
    qint64 frameSeq = -1;
    qint64 decodeUs = -1;
};

struct VisionSnapshot {
    QImage frame;
    QStringList detections;
    double fps = 0.0;
    bool statusReported = false;
    bool modelLoaded = false;
    bool cameraOnline = false;
    int frameCount = 0;
    QString errorMsg;
};
```

这段的作用是定义页面、运行时和后端之间共享的数据形状。

依赖：
- Qt 基础类型。

输入：
- RF 解析结果。
- 视觉运行时结果。
- 系统统计结果。

输出：
- 可被 UI 快照读取的数据结构。

去向：
- `RFSnapshot`
- `VisionSnapshot`
- `SystemStats`
- `LogEntry`

为什么这样设计：
- 这些结构只描述“状态”，不包含复杂行为。
- 页面和运行时如果直接共享原始对象，会把线程安全问题放大。

再看后端：

```cpp
class DashboardBackend {
public:
    void updateSerialStatus(bool online, const QString &port = QString());
    void addRFEvent(const RFEvent &event, const QVector<int> &pulses);
    void updateWaveform(const QVector<int> &pulses);
    void incrementCrcError();
    void incrementParseError();
    void updateProtocolStats(int crcErrors, int parseErrors, int driverDropFrames);
    void incrementDrop();

    void updateVisionState(const VisionSnapshot &snapshot);
    void setVisionOffline(const QString &message);

    void addLog(const QString &level, const QString &source, const QString &message);
    void addMqttPublishLog(const QString &topic, const QString &payload);
    void clearLogs();

    RFSnapshot snapshotRF() const;
    VisionSnapshot snapshotVisionState() const;
    QVector<LogEntry> queryLogs(const QString &sourceFilter, int limit) const;
    SystemStats snapshotSystemStats();
};
```

这段的作用是把所有跨线程写入和 UI 读取都收口到一个 mutex 保护对象里。

依赖：
- `QMutex`
- `QElapsedTimer`
- `/proc/stat`
- `/proc/meminfo`

输入：
- `RFGatewayClient` 的解析结果。
- `VisionRuntime` 的运行时结果。
- 页面按钮动作。

输出：
- 可查询的快照。

去向：
- `RFStatusPage`
- `VisionPage`
- `SystemLogPage`
- `MainWindow::updateStatusBar()`

为什么这样设计：
- 后端是共享状态中枢，不是业务引擎。
- 写入端只负责更新内存中的最新状态和有限历史。
- 读取端只拿快照，不直接触碰运行时内部对象。

几个关键点要单独拎出来：

### 4.1 RF 写入

`addRFEvent()` 会同时更新：
- `lastDecode_`
- `waveform_`
- `eventHistory_`
- `frameCount_`

并且会限制：
- `waveform_` 最多 `kMaxWaveform = 1024`
- `eventHistory_` 最多 `kMaxHistory = 200`

这段的作用是把“最新一次解码”与“历史轨迹”同时保留。

为什么这样设计：
- RF 页要看最近一次解码，也要看历史表和波形。
- 只保留有限历史，避免 UI 刷新时无限增长。

### 4.2 视觉写入

`updateVisionState()` 会把视觉快照整体写入，并强制标记 `statusReported = true`。

这句话要特别注意：
- `statusReported` 表示“视觉运行时已经写过状态”
- 不等于 `cameraOnline = true`
- 也不等于 `modelLoaded = true`

这段的作用是给页面一个“有状态可读”的标记。

### 4.3 日志和系统统计

`addLog()` 会追加时间戳、级别、来源和消息，并把日志数限制在 `kMaxLogs = 500`。

`queryLogs()` 是按来源过滤后倒序取最新日志。

`snapshotSystemStats()` 会按节流策略读取：
- `/proc/stat` 算 CPU 占用
- `/proc/meminfo` 算内存占用

节流间隔是 1500ms，不是每次刷新都读文件。

你要抓住的点是：
- `DashboardBackend` 不是“缓存一份数据”这么简单，它是整个 UI 的状态交换层。
- 页面看的是快照，运行时写的是状态，真正的耦合点只有 backend。

## 5. RF 链路：`rf_gateway_client.*`

先看这个类的定位：

```cpp
class RFGatewayClient {
public:
    RFGatewayClient(DashboardBackend *backend, const AppOptions &options, QObject *context);
    ~RFGatewayClient();

    void start();
    void stop();
};
```

这段的作用不是“解析 RF 协议”，而是“把外部 `rf_gateway` 进程接进来”。

依赖：
- `QProcess`
- `DashboardBackend`
- `AppOptions`

输入：
- 应用目录中的 `rf_gateway`
- `--rf-input`

输出：
- 标准输出行
- 进程生命周期事件
- 转写成后端状态和日志的结果

去向：
- `DashboardBackend`

为什么这样设计：
- RF 侧是外部进程驱动，不是 Qt 内部对象直接采集。
- 这样可以把硬件采集、协议解析和 UI 完全分离。

### 5.1 启动与停止

`start()` 只做一件事：调用 `startGateway()`。

`stop()` 会：
- kill 外部进程
- 等待收尾
- `deleteLater()`
- 将串口/链路状态置为离线

这段的作用是把生命周期收口。

### 5.2 固定路径与固定输入

```cpp
QString fixedGatewayPath() {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/rf_gateway");
}

QString RFGatewayClient::resolvedRfInputPath() const {
    const QString defaultPath = defaultRFInputPath();
    if (options_.rfInput.isEmpty() || options_.rfInput == defaultPath) {
        return defaultPath;
    }
    return defaultPath;
}
```

这段的作用是把 master 侧 RF 入口固定住。

依赖：
- `QCoreApplication::applicationDirPath()`
- `defaultRFInputPath()`

输入：
- 当前运行目录
- 启动参数

输出：
- `rf_gateway` 的绝对路径
- 实际使用的 RF 输入路径

去向：
- `QProcess::setProgram()`
- `QProcess::setArguments()`

为什么这样设计：
- 这里不是开放式配置，而是强约束启动。
- `resolvedRfInputPath()` 是启动链的一环：它先收口 `options_.rfInput`，再被 `buildGatewayArgs()` 复用；master 侧不接受任意 RF 输入路径。

### 5.3 `QProcess` 信号链

`startGateway()` 里最关键的是这几个连接：

```cpp
QObject::connect(gateway_, &QProcess::started, context_, ...);
QObject::connect(gateway_, &QProcess::readyReadStandardOutput, context_, ...);
QObject::connect(gateway_, &QProcess::readyReadStandardError, context_, ...);
QObject::connect(gateway_, &QProcess::errorOccurred, context_, ...);
QObject::connect(gateway_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), context_, ...);
```

这段的作用是把进程事件转成后端事件。

输入：
- 外部进程状态
- stdout 行
- stderr 诊断文本

输出：
- `updateSerialStatus()`
- `addLog()`
- `updateProtocolStats()`
- `addRFEvent()`
- `appendDiagnosticChunk()/takeBufferedDiagnostics()`
- `incrementParseError()`

去向：
- `DashboardBackend`

为什么这样设计：
- 这里用的是 Qt 的事件回调，不是轮询。
- 进程启动、退出和输出行都变成明确的状态更新，页面不需要知道进程细节。

### 5.4 行解析

`handleProtocolLine()` 按 JSON envelope 的 `type` 字段处理：

- 非法 JSON / 缺少 `payload`
  - `incrementParseError()`
  - 记一条 `Invalid rf_gateway protocol JSON`
- `type == "rf_event"`
  - `parseRFEventPayload()` 解析 `addr/key/conf/src/seq/decode_us/pulse_us[]`
  - `updateSerialStatus(true, rf_input)`
  - `addRFEvent()` 写入事件和真实波形
  - `addLog("INFO", "RF", line)`
  - `mqtt_published == true` 时额外记 `addMqttPublishLog()`
- `type == "device_status"`
  - 从 `payload` 读取 `rf_online`、`driver_crc_err`、`app_drv_drop`
  - `updateSerialStatus()` 和 `updateProtocolStats()`
  - 记录 RF 日志 / MQTT 发布日志
- `type == "rf_stats"`
  - 从 `payload` 读取 `driver_crc_err`、`decode_no_frame`、`decode_err`、`drv_drop`
  - `parseErrors = decode_no_frame + decode_err`
  - 如有 `driver_online` 则同步在线状态
  - 记录 RF 日志 / MQTT 发布日志
- 未知 `type`
  - `incrementParseError()`
  - 记一条 `Unknown rf_gateway protocol type`

这段的作用是把单行 JSON envelope 变成结构化状态。

你要抓住的点是：
- `rf_gateway` 输出不是给 UI 直接看的，而是给客户端解析器看的。
- `RFGatewayClient` 负责“文本 -> 状态”，不是“文本 -> 页面”。
- 页面只认 `DashboardBackend` 的结构化快照。

## 6. RF 页面与波形控件：`rf_status_page.*` / `waveform_widget.*`

### 6.0 `RFGatewayClient` helper 链：启动外部进程与解析 stdout

这条链把“怎么启动 `rf_gateway`”和“怎么把 JSON stdout 变成事件”串在一起看：

```text
fixedGatewayPath()
  -> resolveGatewayPath()
  -> resolvedRfInputPath()
  -> buildGatewayArgs()
  -> startGateway()
  -> QProcess::setProgram()/setArguments()/start()

QProcess::readyReadStandardOutput
  -> drainProtocolBuffer()
  -> handleProtocolLine()
  -> parseProtocolEnvelope()
  -> parseRFEventPayload()
  -> updateSerialStatus()/updateProtocolStats()/addRFEvent()/addLog()
```

这条链里最关键的是：`fixedGatewayPath()` 先给出二进制的固定落点，`resolveGatewayPath()` 再验证它真的存在；`resolvedRfInputPath()` 不接受任意注入，最终只会落回 `/dev/rf433`；`buildGatewayArgs()` 只负责拼参数，不掺杂解析逻辑；`parseProtocolEnvelope()` 负责拆顶层 `type/topic/mqtt_published/payload`，`parseRFEventPayload()` 再把 `pulse_us[]` 等字段收束成 `RFEvent + QVector<int>`，最后交给 `DashboardBackend` 去做快照写入。

| 函数 | 作用 | 依赖 | 输入 | 输出 | 去向 | 为什么这样设计 |
| --- | --- | --- | --- | --- | --- | --- |
| `fixedGatewayPath()` | 给出 `rf_gateway` 的固定安装路径 | `QCoreApplication::applicationDirPath()` | 无 | 可预期的二进制路径 | `resolveGatewayPath()` | master 侧不开放任意 gateway 注入，只认包内固定落点 |
| `resolveGatewayPath()` | 校验固定路径是否真实存在且可执行 | `QFileInfo` | `fixedGatewayPath()` 的结果 | 绝对路径或空串 | `startGateway()` | 把“路径拼出来”与“路径可用”分开，失败更早暴露 |
| `resolvedRfInputPath()` | 收敛 RF 输入路径；当前实现除默认值外一律回退到固定值 | `defaultRFInputPath()`、`options_.rfInput` | 命令行/默认值 | 进程参数用的输入路径 | `buildGatewayArgs()`、`startGateway()` | 让 master 的输入面保持收口，避免页面层误以为可任意传参 |
| `buildGatewayArgs()` | 拼出 `rf_gateway` 的启动参数 | `resolvedRfInputPath()` | 当前 options | `QStringList` | `QProcess::setArguments()` | 把参数拼接独立出来，便于单独审查与测试 |
| `parseProtocolEnvelope()` | 解析顶层 JSON envelope | `QJsonDocument`、`QJsonObject` | stdout 单行文本 | `type/topic/mqtt_published/payload` | `handleProtocolLine()` | 把 stdout ABI 固定成单行 JSON，而不是让页面层认识多套文本标签 |
| `parseRFEventPayload()` | 把 `rf_event.payload` 里的真实字段还原成事件和波形 | `QJsonArray`、`QDateTime` | `payload.addr/key/conf/src/seq/decode_us/pulse_us[]` | `RFEvent`、`QVector<int>` | `handleProtocolLine()` -> `addRFEvent()` | 把 RF 事件解析边界收窄到一个 helper，避免事件写入里混进 JSON 细节 |

`startGateway()` 的实际顺序是：

```text
resolveGatewayPath()
  -> stop/cleanup old QProcess
  -> QProcess::setProcessChannelMode(SeparateChannels)
  -> connect(started/stdout/stderr/error/finished)
  -> setProgram()
  -> setArguments(buildGatewayArgs())
  -> start()
```

stdout 这一侧则是：

```text
readyReadStandardOutput
  -> append into gatewayBuffer_
  -> split by newline
  -> handleProtocolLine(line)
  -> parseProtocolEnvelope() / parseRFEventPayload()
  -> backend_->updateSerialStatus()
  -> backend_->updateProtocolStats()
  -> backend_->addRFEvent()
  -> backend_->addLog()
```

这样拆的原因很简单：
- 启动链只负责“把外部进程拉起来”，不负责理解协议。
- 解析链只负责“把 JSON stdout 变成结构化事件”，不负责创建进程。
- `DashboardBackend` 是唯一写入点，页面只读快照。

### 6.1 `RFStatusPage`

先看页面职责：

```cpp
class RFStatusPage : public QWidget {
public:
    explicit RFStatusPage(DashboardBackend *backend, QWidget *parent = nullptr);
private:
    void setupUi();
    void refresh();
};
```

这段的作用是展示 RF 状态，而不是生产 RF 数据。

依赖：
- `DashboardBackend`
- `QTimer`
- `QTableWidget`
- `WaveformWidget`

输入：
- `backend_->snapshotRF()`

输出：
- 在线/离线状态
- 帧数和错误计数
- 最近一次解码结果
- 历史事件表
- 脉冲波形图

去向：
- 用户界面

为什么这样设计：
- 页面对 backend 做定时轮询，不直接绑定运行时对象。
- 这样可以避免后台线程直接触碰 UI。

`refresh()` 的核心逻辑是：

```cpp
const RFSnapshot snapshot = backend_->snapshotRF();
statusDot_->setStyleSheet(snapshot.serialOnline ? "color: #4EC9B0;" : "color: #d9534f;");
serialLabel_->setText(...);
frameLabel_->setText(...);
waveformWidget_->setPulses(snapshot.waveform);
```

这段的作用是把 RF 快照落成可读状态。

特别要看这几个地方：
- `waveformWidget_->setPulses(snapshot.waveform)` 是 RF 页和波形控件的唯一连接点。
- 历史表来自 `snapshot.events`，不是实时追加。
- 最近一次解码用 `snapshot.hasLastDecode` 判断是否有数据。

### 6.2 `WaveformWidget`

先看它的职责：

```cpp
class WaveformWidget : public QWidget {
public:
    explicit WaveformWidget(QWidget *parent = nullptr);
    void setPulses(const QVector<int> &pulses);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    QVector<int> pulses_;
};
```

这段的作用只是画图，不解析协议。

依赖：
- `QPainter`
- `QVector<int>`

输入：
- 脉冲宽度序列

输出：
- 一个实时波形预览

去向：
- 仅绘制到当前控件上

为什么这样设计：
- 波形展示和 RF 解码是两条不同职责线。
- 这里的任务是把脉冲时序做成视觉预览，不是重建协议帧。

`setPulses()` 会把脉冲截断到 200 个并触发重绘。

`paintEvent()` 的绘制顺序很清楚：
- 先填背景
- 再画网格
- 再画边框
- 再画 HIGH/LOW 标签
- 最后按高低电平交替画线段

你要抓住的点是：
- 这里的横轴是脉冲比例，不是严格时间标尺。
- 画的是“形状预览”，不是示波器级别的精确采样重建。

## 7. 视觉链路：`vision_runtime.*` / `vision_page.*`

### 7.1 `VisionRuntime`

先看定位：

```cpp
class VisionRuntime {
public:
    VisionRuntime(DashboardBackend *backend, const AppOptions &options);
    ~VisionRuntime();
    void start();
    void stop();
private:
    void workerLoop();
};
```

这段的作用是把视觉链路放到独立线程里跑。

依赖：
- `DashboardBackend`
- `AppOptions`
- `std::thread`
- `std::atomic<bool>`

输入：
- `options_.visionDevice`
- 本地摄像头帧
- RKNN 模型

输出：
- `VisionSnapshot`
- 视觉日志

去向：
- `DashboardBackend`

为什么这样设计：
- 视觉推理是阻塞型工作，不能放 UI 线程。
- 这里不用 Qt 信号槽驱动帧循环，而是直接用工作线程，逻辑更直观，也更适合持续抓帧。

先看线程控制：

```cpp
void VisionRuntime::start() {
    if (backend_ == nullptr || running_.exchange(true)) {
        return;
    }
    worker_ = std::thread(&VisionRuntime::workerLoop, this);
}

void VisionRuntime::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}
```

这段的作用是把生命周期和线程状态绑定在一起。

### 7.2 本地视觉链路

`workerLoop()` 在本地启用时，顺序是这样的：

1. 记录启动日志，解析模型路径，初始化 `rkYolov5s`。
2. 按 `options_.visionDevice` 分支：
   - `/dev/video*` 走 `V4L2Capture::open()/startStream()`
   - 可读本地视频文件走 `MppDecoder::open()`，并先 decode 一帧拿到真实几何尺寸
3. 基于真实输入尺寸分配 `FrameCopyPool`、`StreamFramePool` 和 RGB buffer。
4. 起 `streamThread`，专门消费 RTSP 推流队列。
5. 起 `sourceThread`，统一把相机帧或解码帧做双路 fan-out：
   - 一路 `copyFrameToAiPool()` 送到 `rknnPool`
   - 一路 `copyFrameToStreamPool()` 送到 `streamThread`
6. 主循环 `aiPool.get()` 取回 AI 结果，把 NV12/YUYV 帧转成 RGB，渲染框图，必要时发布 detection MQTT。
7. 组装 `VisionSnapshot`，调用 `updateVisionState(snapshot)`。
8. 退出时 join `sourceThread` / `streamThread`，并关闭 `V4L2Capture` 或 `MppDecoder`。

这段的作用是把“本地相机或本地视频文件 -> 统一产帧 -> AI/RTSP 双路 fan-out -> 推理快照回传”串成一个闭环。

依赖：
- `frame_pools.h`
- `mpp_decoder.h`
- `mpp_encoder_rtsp.h`
- `preprocess.h`
- `rkYolov5s.hpp`
- `v4l2_capture.h`
- `rga.h`

输入：
- 摄像头设备，例如 `/dev/video9`
- 可读的本地视频文件，例如板侧 MP4
- 模型文件，例如 `model/yolov5s_relu-640-640-rk3568.rknn`

输出：
- 带框图像
- 检测列表
- FPS
- 摄像头/模型状态
- RTSP 推流状态
- 错误文本

去向：
- `DashboardBackend::updateVisionState()`
- `DashboardBackend::addLog()`

为什么这样设计：
- 模型加载、相机打开、视频文件打开、推流失败都被降级成状态快照，而不是让线程崩掉。
- 相机帧和 MP4 解码帧共用同一条本地 runtime，只在输入获取阶段分叉，不再维护“假的默认路径”。
- 页面只需要知道当前能不能看、看到了什么、报了什么错。

### 7.2.1 `VisionRuntime` helper 链：本地模型 / 相机 / 渲染辅助函数

这条链把“模型路径从哪来”“设备格式怎么兼容”“失败态怎么留住”“渲染风格怎么稳定”串起来看：

```text
localVisionRoot()
  -> resolveModelPath()
  -> workerLoop()
  -> makeStatusSnapshot()
  -> updateVisionState()

sleepShort()
  -> retry / backoff between failure states

toRgaFormat()
  -> V4L2Capture / RGA format mapping

colorForLabel()
  -> renderAnnotatedFrame()
```

| 函数 | 作用 | 依赖 | 输入 | 输出 | 去向 | 为什么这样设计 |
| --- | --- | --- | --- | --- | --- | --- |
| `makeStatusSnapshot()` | 把错误消息、`cameraOnline`、`modelLoaded` 封成统一快照 | `VisionSnapshot` | 状态文本 + 两个布尔值 | 可直接写回 backend 的快照 | `updateVisionState()` / `setVisionOffline()` | 失败态也要走同一套结构，页面才能稳定读 |
| `sleepShort()` | 在失败路径里短暂停一下，避免空转打爆 CPU | `std::this_thread::sleep_for` | 无 | 无 | `workerLoop()` 的重试分支 | 这里要的是轻量 backoff，不是长时间阻塞 |
| `localVisionRoot()` | 给出视觉资源根目录 | `QCoreApplication::applicationDirPath()` | 无 | 本地仓库/安装目录下的视觉根 | `resolveModelPath()` | 让模型和程序包保持同一分发边界，减少外部配置依赖 |
| `resolveModelPath()` | 从多个候选里选出可用的本地模型 | `localVisionRoot()`、`QFileInfo` | 无 | 首个存在的 `.rknn` 路径 | `workerLoop()` | 兼容不同板型和命名方式，先找得到再说 |
| `toRgaFormat()` | 把摄像头像素格式映射到 RGA 格式 | RGA / V4L2 常量 | capture 的 pixel format | RGA 目标格式码 | `workerLoop()` 的预处理分支 | 设备兼容问题先在格式层收口，避免推理阶段才爆 |
| `colorForLabel()` | 为同一个标签生成稳定颜色 | `QColor`、label 字符串 | 类别名 | 画框颜色 | `renderAnnotatedFrame()` / 视觉渲染 | 同类同色，便于人眼扫图；颜色规则固定，便于复现 |

`workerLoop()` 里这几类 helper 的分工是：

- `localVisionRoot()` / `resolveModelPath()` 先把模型定位好。
- `toRgaFormat()` 先把设备输入格式对齐到预处理链能接受的格式。
- `makeStatusSnapshot()` 在模型失败、相机失败、推理失败时统一回写状态。
- `sleepShort()` 在失败态之间留出很短的退避间隔，避免一直紧密重试。
- `colorForLabel()` 只影响渲染风格，不影响模型结果。

这样拆的原因是：
- 模型路径和板型兼容性属于运行时入口问题，应该在真正推理前解决。
- 失败态必须可读、可回写、可复用，不能靠 UI 自己猜。
- 渲染颜色只是展示层风格，不该反向污染推理逻辑。

### 7.3 结果回传

这个链路里有两个辅助函数最关键：

```cpp
QStringList formatDetections(const detect_result_group_t &group);
QImage renderAnnotatedFrame(const QImage &baseFrame, const detect_result_group_t &group);
```

`formatDetections()` 负责把检测框转成字符串列表。

`renderAnnotatedFrame()` 负责把检测框和标签画回图像。

这段的作用是把原始推理结果拆成两份：
- 一份给列表展示
- 一份给视频框展示

你要抓住的点是：
- `VisionRuntime` 不把裸检测结果直接甩给 UI。
- 它自己先把结果整理成“画过框的图像 + 文本列表”，页面只负责显示。

如果本地运行时没有启用，线程会直接回写一个不可用状态和 warning 日志。

这不是失败路径的补丁，而是明确的编译期开关语义。

### 7.4 `VisionPage`

页面职责很简单：

```cpp
class VisionPage : public QWidget {
public:
    explicit VisionPage(DashboardBackend *backend, QWidget *parent = nullptr);
private:
    void setupUi();
    void refresh();
};
```

这段的作用是展示视觉快照，不参与推理。

依赖：
- `QLabel`
- `QListWidget`
- `QTimer`
- `DashboardBackend`

输入：
- `backend_->snapshotVisionState()`

输出：
- 视频预览
- FPS
- 摄像头在线状态
- 模型加载状态
- 帧计数
- 检测结果列表

去向：
- 用户界面

为什么这样设计：
- 视觉页面是纯消费者。
- 它通过 33ms 定时刷新拿快照，和视觉线程解耦。

`refresh()` 里有几个关键分支：
- `statusReported == false` 时，页面显示“未接入/等待状态”
- `snapshot.frame` 非空时，显示图像
- `snapshot.frame` 为空时，显示错误文本或等待文本
- `snapshot.detections` 为空时，显示“无检测目标”

你要抓住的点是：
- 页面显示的不是“推理器内部状态”，而是 `VisionSnapshot` 的最后一次回写结果。
- `cameraOnline`、`modelLoaded`、`errorMsg` 的组合，比单一布尔值更有表达力。

## 8. 日志链路：`system_log_page.*`

先看定位：

```cpp
class SystemLogPage : public QWidget {
public:
    explicit SystemLogPage(DashboardBackend *backend, QWidget *parent = nullptr);
private:
    void setupUi();
    void refresh();
};
```

这段的作用是把 RF、视觉、MQTT 和系统状态聚到一页。

依赖：
- `DashboardBackend`
- `QPlainTextEdit`
- `QComboBox`
- `QTimer`

输入：
- `snapshotSystemStats()`
- `snapshotRF()`
- `snapshotVisionState()`
- `queryLogs()`

输出：
- CPU / 内存 / uptime
- MQTT 次数和最近一条 MQTT 日志
- RF 错误计数
- 视觉状态和错误
- 日志文本

去向：
- 仅 UI 展示

为什么这样设计：
- 这个页面不是“日志打印器”，而是“运行态总览页”。
- 它把系统资源、业务错误和消息日志放在同一屏，方便对链路做排障。

`refresh()` 的处理逻辑很明确：

```cpp
const QString selected = filterCombo_->currentText();
const QString sourceFilter = (selected == QStringLiteral("全部")) ? QString() : selected;

const SystemStats stats = backend_->snapshotSystemStats();
const RFSnapshot rf = backend_->snapshotRF();
const VisionSnapshot vision = backend_->snapshotVisionState();
const QVector<LogEntry> logs = backend_->queryLogs(sourceFilter, 200);
```

这段的作用是把不同来源的状态统一读出来。

几个关键点：
- MQTT 计数来自 `SystemStats::mqttCount`。
- 最近一条 MQTT 日志单独查询，避免扫描整页日志。
- `queryLogs(sourceFilter, 200)` 取的是最近 200 条。
- 页面再把日志倒回正序展示。

你要抓住的点是：
- `SystemLogPage` 依赖的是 `DashboardBackend` 的聚合能力，不依赖任何具体运行时对象。
- 它不是业务处理层，而是观测层。

## 9. 结论：这套 UI 的真实职责边界

这套代码最重要的不是“页面有几个”，而是边界切得很清楚：

- `main.cpp` 负责启动门禁。
- `MainWindow` 负责组装和生命周期。
- `DashboardBackend` 负责共享状态。
- `RFGatewayClient` 负责把外部 RF 进程接进来。
- `VisionRuntime` 负责本地摄像头 + 推理 + 回写。
- `RFStatusPage`、`VisionPage`、`SystemLogPage` 只读快照。
- `WaveformWidget` 只画波形。

不是 X，而是 Y：
- 不是页面自己去拉硬件，而是运行时写 backend，页面轮询 backend。
- 不是 UI 直接操作线程内部对象，而是所有状态先变成快照。
- 不是 RF 和视觉共享同一条处理链，而是各自生产、统一汇聚。

如果你读这份代码，只记一件事，就是这条链：

```text
输入约束(main)
  -> 运行时组装(MainWindow)
  -> 状态中枢(DashboardBackend)
  -> 两条生产链路(RFGatewayClient / VisionRuntime)
  -> 三个展示页(RFStatusPage / VisionPage / SystemLogPage)
  -> 一个纯绘图控件(WaveformWidget)
```

这就是这套 Qt / vision 仪表盘的真实运行方式。
## 2. 关键资源生命周期

先看 `QProcess`、`std::thread` 和各类 `snapshot` 的生灭关系，再看后面的页面刷新。

这段的作用是把“资源什么时候生、什么时候死、什么时候失效”卡清楚。

`QProcess` 这条线：
- `RFGatewayClient::start()` 会先 `new QProcess(context_)`，再挂 `started` / `readyReadStandardOutput` / `errorOccurred` / `finished` 四个信号。
- `stop()` 会先 `kill()`，再等 `waitForFinished(500)`，最后 `deleteLater()`。
- 不是“页面关了进程就自动消失”，而是 `MainWindow` 析构时显式停一次，`RFGatewayClient` 自己也会在析构里再停一次。
- 失效时机是 `gateway_ == nullptr`、`gatewayPath` 找不到，或者进程已经退出。

线程这条线：
- `VisionRuntime::start()` 先用 `running_.exchange(true)` 做幂等控制，再启动 `std::thread`。
- `stop()` 先把 `running_` 置回 false，再 `join()`。
- 析构函数里会再走一遍 `stop()`，所以线程生命周期和对象生命周期是绑死的。
- 不是 Qt 信号线程，而是单独 worker thread 在跑推理。

snapshot / frame 这条线：
- `VisionSnapshot::frame` 只在一帧成功完成后才有效；`RGA` 转换失败、`RKNN` 推理失败、摄像头打开失败时，快照里要么是空图，要么是错误文本。
- `DashboardBackend::updateVisionState()` 写进去的是“最新一次可展示状态”，旧快照会被下一次覆盖。
- `RFSnapshot` 不是独立资源，它是 `DashboardBackend` 当前状态的拷贝；`snapshotRF()` / `snapshotVisionState()` 拿到的都是“这一刻的快照”，下一次 backend 写入后旧快照自然就过时。
- `SystemStats` 不是每次都重算，`snapshotSystemStats()` 自带 1500ms 节流，所以页面读到的是缓存值，不是实时采样点。

你要抓住的点是：
- 不是每个对象都需要单独找“销毁点”，而是先看谁拥有它、谁负责停它。
- 不是 snapshot 自己会失效，而是 backend 写了更新值之后，旧快照自然退场。
- 不是所有页面刷新都会触发采集，很多时候只是把已经写好的快照再读一遍。

## 3. 数据契约与更新频率

先看谁写、谁读、多久刷一次，再看字段本身。

这段的作用是先把数据形状和刷新节奏分开看，不要把“字段是谁写的”误读成“字段是谁展示的”。

| 结构 | 字段谁写 | 谁读 | 多久刷新一次 |
| --- | --- | --- | --- |
| `RFEvent` | `RFGatewayClient::handleProtocolLine()` 在匹配 `type == "rf_event"` 的 JSON envelope 时一次性写入 `timestamp`、`address`、`key`、`confidence`、`source`、`frameSeq`、`decodeUs` 和真实 `pulse_us[]` | `DashboardBackend::snapshotRF()`，`RFStatusPage` 的最近解码区和历史表 | 事件驱动，每次 RF 解码命中刷新一次 |
| `VisionSnapshot` | `VisionRuntime::workerLoop()` 在每帧后写 `frame`、`detections`、`fps`、`frameCount`、`errorMsg`，`cameraOnline`、`modelLoaded`；`DashboardBackend::updateVisionState()` 统一把 `statusReported` 置真，`setVisionOffline()` 会重置离线态 | `MainWindow::updateStatusBar()`、`VisionPage::refresh()`、`SystemLogPage::refresh()` | 帧驱动，页面侧分别按 2000ms / 33ms / 1000ms 读快照，`fps` 本身大约每秒更新一次 |
| `SystemStats` | `DashboardBackend::snapshotSystemStats()` 读 `/proc/stat`、`/proc/meminfo` 并缓存；`addLog()` 在 `source == "MQTT"` 时顺手累计 `mqttCount` | `MainWindow::updateStatusBar()`、`SystemLogPage::refresh()` | 系统统计缓存约每 1500ms 刷新一次，`uptimeSec` 每次快照都会递增 |

你要抓住的点是：
- `statusReported` 不是“成功”，而是“视觉线程已经回写过状态”。
- `VisionSnapshot` 的 `frame` 不是常驻资源，它只是最近一次成功推理后的快照图。
- `SystemStats` 不是每次页面刷新都重算，它是有节流的缓存快照。

## 4. 最后一眼只看代码

先看三段代码块，别先看解释文字。

这页只看代码块，不看解释也能把链路串起来。

```cpp
// 启动链
int main(...) {
    MainWindow window(options);
    window.show();
    return app.exec();
}

MainWindow::MainWindow(...)
    : rfClient_(&backend_, options_, this),
      visionRuntime_(&backend_, options_) {
    setupUi();
    setupRuntime();
}

void MainWindow::setupRuntime() {
    rfClient_.start();
    visionRuntime_.start();
    updateStatusBar();
}
```

```cpp
// 信号链
RFGatewayClient -> QProcess stdout / finished / errorOccurred
    -> DashboardBackend::updateSerialStatus()
    -> DashboardBackend::addRFEvent()
    -> DashboardBackend::addLog()

VisionRuntime workerLoop()
    -> DashboardBackend::updateVisionState()
    -> DashboardBackend::addLog()
```

```cpp
// 状态刷新链
MainWindow     : 2000ms 读 `snapshotSystemStats()` / `snapshotRF()` / `snapshotVisionState()`
RFStatusPage   :  500ms 读 `snapshotRF()`
VisionPage     :   33ms 读 `snapshotVisionState()`
SystemLogPage  : 1000ms 读 `snapshotSystemStats()` / `snapshotRF()` / `snapshotVisionState()` / `queryLogs()`
```

你最后只要记住这三句：
- `MainWindow` 负责拉起。
- `DashboardBackend` 负责收口。
- `Page` 只负责读快照并展示。
