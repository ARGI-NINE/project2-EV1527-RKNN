# `project2_master` MQTT Publish 教程

这篇教程专门讲当前仓库里的 MQTT publish，重点放在 Vision 侧，因为当前板端视觉链已经具备本地发布能力，而 subscribe、MQTT command、record_done、事件录像闭环都还没有实现。

如果你对 MQTT 还不熟，先别急着读代码，先把概念补齐。

## 1. 先讲概念：MQTT 到底是什么

MQTT 是一种轻量级发布订阅协议，最适合这种“设备侧不停往外发状态和事件”的场景。

里面只有四个最基本概念。

### 1.1 Broker

Broker 是消息中转站。

在当前 Vision 代码里，broker 地址写死为：

- Host: `192.168.30.26`
- Port: `1883`

也就是说，Qt 进程内部的 `VisionRuntime` 不是自己存消息，而是连到这个 broker，然后往主题上发 JSON。

### 1.2 Topic

Topic 就是消息路径。

当前 Vision 代码只发两个 topic：

- `argi/device/rk3568-001/vision/detection`
- `argi/device/rk3568-001/stream/status`

你可以把它理解成两个独立频道：

- detection 频道讲“我这帧看到了什么”
- stream status 频道讲“RTSP 推流当前是否在线”

### 1.3 Publisher 和 Subscriber

Publisher 只负责发。

Subscriber 只负责收。

当前 Vision 代码只有 publisher，没有 subscriber。这个边界必须写清：

- 已实现：Vision publish
- 未实现：Vision subscribe
- 未实现：MQTT command 消费
- 未实现：`gateway/{gateway_id}/cmd`

### 1.4 Retain

retain 的意思是“让 broker 保留最后一条该 topic 消息，供后来订阅者立即拿到”。

当前代码里：

- `vision/detection` 用 `retain = false`
- `stream/status` 用 `retain = true`

这个选择很合理：

- 检测事件是瞬时事件，不适合保留成“最后一条事实”
- 流状态更像设备状态，保留最后一条是有意义的

## 2. 当前仓库里其实有两套 MQTT publish

这件事很容易搞混，所以单列出来。

### 2.1 RF 侧 MQTT publish

RF 侧在 `project2_master/linux_app/`：

- `mqtt_publisher.c`
- `main.c`

它会：

- 向 broker 发布 RF 相关消息
- 同时把一份 JSON envelope 打到 stdout
- 由 `qt_gui/rf/rf_gateway_client.cpp` 消费 stdout

也就是说，RF 链是：

```text
rf_gateway
  -> MQTT publish
  -> stdout JSON envelope
  -> Qt 读取 stdout
```

来源文件：`project2_master/linux_app/main.c`  
函数：`build_protocol_line()`、`emit_protocol_message()`  
作用：RF 网关先把业务 payload 包成统一的本地 JSON envelope，再在 MQTT 已连接时把同一份 payload 旁路发布到 broker。

```c
static int build_protocol_line(
    char *line,
    size_t line_capacity,
    const char *type,
    const char *subtopic,
    int mqtt_published,
    const char *payload_json
) {
    size_t offset = 0u;

    if (
        line == NULL ||
        type == NULL ||
        subtopic == NULL ||
        payload_json == NULL
    ) {
        return -1;
    }

    if (
        appendf(
            line,
            line_capacity,
            &offset,
            "{\"type\":\"%s\",\"topic\":\"%s/%s\",\"mqtt_published\":%s,\"payload\":%s}",
            type,
            MQTT_TOPIC_ROOT,
            subtopic,
            json_bool(mqtt_published),
            payload_json
        ) != 0
    ) {
        return -1;
    }

    return 0;
}

static int emit_protocol_message(
    app_ctx_t *ctx,
    const char *type,
    const char *subtopic,
    const char *payload_json,
    int retain
) {
    char line[JSON_LINE_CAPACITY];
    int publish_rc = MQTT_PUBLISHER_ERR_NO_CONN;
    int mqtt_published = 0;

    if (ctx == NULL || type == NULL || subtopic == NULL || payload_json == NULL) {
        return -1;
    }

    if (mqtt_publisher_is_connected(&ctx->mqtt)) {
        publish_rc = mqtt_publisher_publish(&ctx->mqtt, subtopic, payload_json, retain);
        mqtt_published = (publish_rc == 0);
    }

    if (build_protocol_line(line, sizeof(line), type, subtopic, mqtt_published, payload_json) != 0) {
        fprintf(stderr, "[RF_JSON] failed to assemble protocol line for %s\n", type);
        return -1;
    }

    fprintf(stdout, "%s\n", line);

    if (publish_rc != 0 && publish_rc != MQTT_PUBLISHER_ERR_NO_CONN) {
        fprintf(
            stderr,
            "[MQTT] publish %s/%s failed: %s\n",
            MQTT_TOPIC_ROOT,
            subtopic,
            mqtt_publisher_error_string(publish_rc)
        );
    }

    return mqtt_published ? 0 : publish_rc;
}
```

这段代码把 RF 路径的真实边界写得很清楚：`stdout` 和 MQTT 不是二选一，而是对同一份 `payload_json` 做两条输出。  
`build_protocol_line()` 把根对象固定成 `type/topic/mqtt_published/payload` 四段，所以 Qt 侧读到的是 envelope，而不是裸 payload。  
`emit_protocol_message()` 先尝试 `mqtt_publisher_publish()`，再把 publish 结果回填进 `mqtt_published` 字段，最后无论 MQTT 成功与否都把 JSON 行写到 `stdout`。这就是为什么 RF 页面不依赖 broker 在线也能继续工作。

### 2.2 Vision 侧 MQTT publish

Vision 侧在 `project2_master/qt_gui/vision/vision_runtime.cpp`。

它不会走 stdout envelope，而是：

```text
VisionRuntime
  -> VisionMqttPublisher
  -> broker
  -> DashboardBackend 记录成功发布日志
```

这篇教程的重点是第二套。

## 3. Vision MQTT 的类和函数关系

Vision MQTT 相关代码都在 `vision_runtime.cpp` 里，关系非常集中：

```text
VisionMqttPublisher
  -> open()
  -> close()
  -> isConnected()
  -> publish()

logPublishedMessage()
  -> compactJson()
  -> mqtt.publish()
  -> backend.addMqttPublishLog()

publishStreamStatus()
publishDetection()
```

如果你只想快速抓主线，先读这四块：

1. `VisionMqttPublisher`
2. `logPublishedMessage()`
3. `publishStreamStatus()`
4. `publishDetection()`

## 4. `VisionMqttPublisher` 是一个很小的 publish-only wrapper

它没有做复杂抽象，本质上就是把 mosquitto 的初始化和发布动作包起来。

### 4.1 `open()`

`open()` 里面做了这些动作：

1. `mosquitto_lib_init()`
2. `mosquitto_new(clientId, true, this)`
3. 注册 connect/disconnect callback
4. `mosquitto_reconnect_delay_set(1, 5, true)`
5. `mosquitto_connect_async(host, port, 30)`
6. `mosquitto_loop_start()`

来源文件：`project2_master/qt_gui/vision/vision_runtime.cpp`  
函数：`VisionMqttPublisher::open()`  
作用：创建 mosquitto 客户端、注册连接状态回调，并启动后台网络循环。

```cpp
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

`open()` 没有注册 `message` 回调，也没有 `subscribe()`，这里只做 publish 所需的最小连接动作。  
`close()` 被 `open()` 开头先调用一次，说明作者显式把它设计成“重复 open 时先清理旧连接”的接口，而不是只允许单次初始化。

当前 client id 写死为：

- `rk3568-001-vision-runtime`

这说明 VisionRuntime 以独立 MQTT client 身份接入 broker。

### 4.2 `handleConnect()` 和 `handleDisconnect()`

它们只维护一个状态位：

- `connected_`

connect 成功时：

- `connected_ = true`

disconnect 或 connect 失败时：

- `connected_ = false`

来源文件：`project2_master/qt_gui/vision/vision_runtime.cpp`  
函数：`VisionMqttPublisher::handleConnect()`、`VisionMqttPublisher::handleDisconnect()`  
作用：mosquitto 后台线程把连接状态回写到 `connected_`，供发布路径快速判定。

```cpp
static void handleConnect(struct mosquitto *mosq, void *userdata, int rc) {
    VisionMqttPublisher *publisher = static_cast<VisionMqttPublisher *>(userdata);
    (void)mosq;
    if (publisher == nullptr) {
        return;
    }
    publisher->connected_.store(rc == 0, std::memory_order_relaxed);
    if (rc != 0) {
        fprintf(stderr, "[VISION_MQTT] connect failed: %s\n", mosquitto_connack_string(rc));
    }
}

static void handleDisconnect(struct mosquitto *mosq, void *userdata, int rc) {
    VisionMqttPublisher *publisher = static_cast<VisionMqttPublisher *>(userdata);
    (void)mosq;
    if (publisher == nullptr) {
        return;
    }
    publisher->connected_.store(false, std::memory_order_relaxed);
    if (rc != 0) {
        fprintf(stderr, "[VISION_MQTT] disconnected unexpectedly: %s\n", mosquitto_strerror(rc));
    }
}
```

这里回调做的事非常收敛：只有状态位维护和错误日志。  
因为没有 `mosquitto_message_callback_set()` 之类的注册动作，所以当前 Vision MQTT 真的是单向输出，不存在“收命令后驱动运行时”的隐藏路径。

这里没有任何 subscribe 行为，也没有 `on_message` 回调，所以别把它想象成双向 MQTT 客户端。

### 4.3 `publish()`

`publish()` 很保守：

- 如果 `mosq_ == nullptr` 或 `!isConnected()`，直接返回 `false`
- 否则调用 `mosquitto_publish(...)`
- QoS 当前固定为 `0`
- retain 由上层调用者传入

这意味着：

- 没连上 broker 时不会阻塞主链
- 也不会抛异常式中止视觉线程
- 只是不成功而已

来源文件：`project2_master/qt_gui/vision/vision_runtime.cpp`  
函数：`VisionMqttPublisher::publish()`  
作用：在运行时快速判定连接状态，然后用 QoS 0 把编码后的 JSON 发出去。

```cpp
bool publish(const char *topic, const QByteArray &payload, bool retain = false) {
    int rc = MOSQ_ERR_NO_CONN;

    if (mosq_ == nullptr || !isConnected()) {
        return false;
    }
    rc = mosquitto_publish(
        mosq_,
        nullptr,
        topic,
        payload.size(),
        payload.constData(),
        0,
        retain
    );
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[VISION_MQTT] publish %s failed: %s\n", topic, mosquitto_strerror(rc));
        return false;
    }
    return true;
}
```

这段实现没有重试队列，也没有本地缓存，失败就直接返回 `false`。  
因此页面里看到的 MQTT 统计只能表示“成功发出去的次数”，不能等同于“应发次数”。

### 4.4 `close()`

关闭顺序是：

1. `mosquitto_disconnect()`
2. `mosquitto_loop_stop(..., true)`
3. `mosquitto_destroy()`
4. `mosquitto_lib_cleanup()`
5. `connected_ = false`

来源文件：`project2_master/qt_gui/vision/vision_runtime.cpp`  
函数：`VisionMqttPublisher::close()`  
作用：按 mosquitto 生命周期逆序停掉网络线程并释放库级资源。

```cpp
void close() {
    if (mosq_ != nullptr) {
        if (loopStarted_) {
            (void)mosquitto_disconnect(mosq_);
            (void)mosquitto_loop_stop(mosq_, true);
        }
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }

    if (libInitialized_) {
        mosquitto_lib_cleanup();
        libInitialized_ = false;
    }

    loopStarted_ = false;
    connected_.store(false, std::memory_order_relaxed);
}
```

这里不是简单 `destroy`，而是先停 loop 再销毁句柄，最后才做 `mosquitto_lib_cleanup()`。  
配合析构函数里的 `close()`，说明 `VisionMqttPublisher` 负责完整的资源回收闭环。

## 5. Vision MQTT 在运行时什么时候初始化

发生在 `VisionRuntime::workerLoop()` 里，而且是在模型初始化成功后、输入源打开前后这一段早期阶段。

顺序是：

1. 先解析模型路径
2. 初始化 `aiPool`
3. 记一条“模型已加载”日志
4. `mqtt.open("rk3568-001-vision-runtime", kMqttHost, kMqttPort)`

来源文件：`project2_master/qt_gui/vision/vision_runtime.cpp`  
函数：`VisionRuntime::workerLoop()`  
作用：在模型初始化完成后拉起 Vision MQTT 发布器，但把它设计成“可退化的旁路能力”。

```cpp
rknnPool<rkYolov5s> aiPool(modelPath.toStdString(), kAiWorkerThreads, kAiQueueSize);
if (aiPool.init() != 0) {
    backend_->updateVisionState(makeStatusSnapshot(QStringLiteral("RKNN model init failed"), false, false));
    backend_->addLog("ERROR", "VISION", QStringLiteral("VisionRuntime model init failed: %1").arg(modelPath));
    running_.store(false);
    return;
}

backend_->addLog("INFO", "VISION", QStringLiteral("VisionRuntime model loaded: %1").arg(modelPath));
if (!mqtt.open("rk3568-001-vision-runtime", kMqttHost, kMqttPort)) {
    backend_->addLog("WARN", "VISION", QStringLiteral("Vision MQTT publisher is disabled; broker connection was not established"));
}
```

这段顺序很关键：先保证推理模型可用，再尝试 MQTT。  
如果 broker 不可达，代码只记一条 `WARN`，不会终止 `workerLoop()`，因此 MQTT 在当前实现里属于“尽力而为输出”，不是视觉主链的前置条件。

如果 `open()` 失败，当前代码只会：

- 记一条 `WARN`
- 内容大意是“Vision MQTT publisher is disabled”

不会：

- 停掉视觉链
- 阻止摄像头打开
- 阻止推理和 UI
- 阻止 RTSP 分支

这就是当前 MQTT 的真实优先级：重要输出，但不是本地推理的生死依赖。

来源文件：`project2_master/qt_gui/vision/vision_runtime.cpp`  
函数：`VisionRuntime::workerLoop()`  
作用：运行时退出前显式关闭 MQTT，和前面的 `open()` 形成成对调用。

```cpp
if (!sourceError.isEmpty()) {
    backend_->updateVisionState(makeStatusSnapshot(sourceError, false, modelReady));
} else if (!pipelineError.isEmpty()) {
    backend_->updateVisionState(makeStatusSnapshot(pipelineError, inputReady, modelReady));
} else {
    backend_->updateVisionState(makeStatusSnapshot(QStringLiteral("Vision runtime stopped"), false, modelReady));
}
mqtt.close();
```

所以 `workerLoop()` 里的 MQTT 相关调用点其实有四类：启动时 `mqtt.open()`，运行中 `publishStreamStatus()`，检测结果 `publishDetection()`，退出时 `mqtt.close()`。

## 6. 当前 Vision 代码只发两个 topic

### 6.1 `argi/device/rk3568-001/vision/detection`

这是目标检测事件主题。

常量定义：

- `kTopicVisionDetection = "argi/device/rk3568-001/vision/detection"`

触发条件：

- `detGroup.count > 0`

换句话说，没有目标时不发 detection。

来源文件：`project2_master/qt_gui/vision/vision_runtime.cpp`  
函数：`VisionRuntime::workerLoop()`  
作用：检测消息不是采帧时就发，而是 AI 推理结果拿到后、并且 `detGroup.count > 0` 时才触发。

```cpp
snapshot.detections = formatDetections(detGroup);
snapshot.frame = renderAnnotatedFrame(rgbImage, detGroup);
if (detGroup.count > 0) {
    publishDetection(backend_, &mqtt, rtspUrl, frameId, captureTsUs, detGroup);
}
```

这证明 detection 发布点挂在“后处理完成”之后。  
也就是说，`VisionRuntime` 先把本地 UI 需要的 `snapshot` 和标注图做出来，再把同一帧的检测结果转成 MQTT 事件。

### 6.2 `argi/device/rk3568-001/stream/status`

这是 RTSP 流状态主题。

常量定义：

- `kTopicStreamStatus = "argi/device/rk3568-001/stream/status"`

触发条件不是“每帧都发”，而是状态节点变化时发：

- RTSP 被禁用时发 `offline/disabled`
- encoder 打开失败时发 `offline/open_failed`
- 推流成功上线时发 `online`
- 编码或写流失败时发 `offline/push_failed`
- RTSP 支路构帧失败时发 `offline/post_stream_failed`
- 线程退出时发 `offline/stopped`

## 7. detection 消息是怎么拼出来的

### 7.1 发布时机

发布发生在 `aiPool.get()` 之后。

完整顺序是：

1. `aiPool.get()` 拿到 `detect_result_group_t`
2. UI 侧需要的 `VisionSnapshot` 开始构建
3. 如果 RTSP 分支开启，先尝试生成 annotated frame
4. 把结果转成 RGB、构造 `QImage`
5. 如果 `detGroup.count > 0`，调用 `publishDetection()`

这里有一个很重要的事实：

- detection publish 是“推理后”的动作
- 不是 `sourceThread` 采到帧就发
- 不是 RTSP 成功后才发

也就是说，检测消息与 RTSP 支路并不是强绑定关系。

### 7.2 payload 字段

`publishDetection()` 构造的 payload 结构如下：

| 字段 | 类型 | 含义 |
|---|---|---|
| `device_id` | string | 固定为 `rk3568-001` |
| `type` | string | 固定为 `vision_detection` |
| `frame_id` | int64 | 帧号 |
| `ts_us` | int64 | 捕获时间戳，微秒 |
| `objects` | array | 检测目标数组 |
| `stream_url` | string | 当前 RTSP 配置地址 |

`objects[]` 里每个对象有：

| 字段 | 类型 | 含义 |
|---|---|---|
| `class` | string | 检测类别名 |
| `conf` | number | 置信度 |
| `box` | array[4] | `[left, top, right, bottom]` |

来源文件：`project2_master/qt_gui/vision/vision_runtime.cpp`  
函数：`publishDetection()`  
作用：把 `detect_result_group_t` 展开成 MQTT detection payload，并交给统一发布入口。

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

这里 `objects` 数组的每一项都直接来自 `detect_result_t`：类别名走 `det.name`，置信度走 `det.prop`，框坐标走 `det.box`。  
最后一行把 `retain` 固定成 `false`，这和前面“事件型消息不保留”的结论是一一对应的，不是文档主观推断。

### 7.3 `stream_url` 字段不要误读

很多人看到 detection payload 里有 `stream_url`，会误以为：

- “这说明 RTSP 一定在线”

这是错的。

当前代码只是把当前配置的 `rtspUrl` 填进去，并不先检查 RTSP 是否成功推上去。

所以正确理解应该是：

- `stream_url` 是“关联流地址配置”
- 不是“当前一定可播放”的保证

## 8. stream status 消息是怎么拼出来的

`publishStreamStatus()` 的字段固定比 detection 少很多：

| 字段 | 类型 | 含义 |
|---|---|---|
| `device_id` | string | 固定为 `rk3568-001` |
| `type` | string | 固定为 `stream_status` |
| `state` | string | `online` 或 `offline` |
| `protocol` | string | 固定为 `rtsp` |
| `codec` | string | 固定为 `h264` |
| `url` | string | 当前 RTSP 地址 |
| `reason` | string | 可选，离线原因 |

来源文件：`project2_master/qt_gui/vision/vision_runtime.cpp`  
函数：`publishStreamStatus()`  
作用：构造 RTSP 状态消息，并统一走 `logPublishedMessage()` 发布。

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

这里的 `reason` 字段是按需插入的，所以 `online` 时 payload 更短，离线时才补原因。  
最后一行把 `retain` 固定成 `true`，正好解释了为什么后来订阅者通常能立刻拿到最新流状态。

这个主题用 `retain = true`。

因此，如果 broker 正常工作，后来订阅这个 topic 的消费者通常能立即看到最新一条流状态，而不需要等下一次事件。

来源文件：`project2_master/qt_gui/vision/vision_runtime.cpp`  
函数：`VisionRuntime::workerLoop()`  
作用：RTSP 分支在多个状态节点调用 `publishStreamStatus()`，这些调用点才是 topic 真正被发出的地方。

```cpp
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

if (encoder.encodeAndPush(frame) != 0) {
    backend_->addLog("WARN", "VISION", QStringLiteral("RTSP push failed; stream branch will retry"));
    publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), rtspUrl, QStringLiteral("push_failed"));
    encoder.close();
    encoderOpen = false;
    nextRetryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(kRtspRetryDelayMs);
    release_frame_buffer(frame.data, frame.release_fn, frame.release_ctx);
    continue;
}

if (encoderOpen || streamWasOnline) {
    publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), rtspUrl, QStringLiteral("stopped"));
}
```

来源文件：`project2_master/qt_gui/vision/vision_runtime.cpp`  
函数：`VisionRuntime::workerLoop()`  
作用：补上另外两个离线状态调用点，分别对应禁用 RTSP 和后处理支路故障。

```cpp
backend_->addLog("INFO", "VISION", QStringLiteral("RTSP push disabled by option"));
publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), rtspUrl, QStringLiteral("disabled"));

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
```

这样把 `open_failed`、`online`、`push_failed`、`stopped`、`disabled`、`post_stream_failed` 六个状态都对上了源码调用点，而不是只靠文字罗列。

## 9. Vision MQTT publish 和 UI 日志是怎么接起来的

Vision 侧没有像 RF 链那样输出 `mqtt_published` 字段，而是走本地日志统计。

核心函数是 `logPublishedMessage()`：

1. 先 `compactJson(payload)`
2. 再调用 `mqtt.publish(topic, encoded, retain)`
3. 只有成功时才 `backend->addMqttPublishLog(topic, payload)`

来源文件：`project2_master/qt_gui/vision/vision_runtime.cpp`  
函数：`compactJson()`、`logPublishedMessage()`  
作用：把 JSON 编码、MQTT publish 和 UI 侧成功日志统计串成一个公共入口。

```cpp
QByteArray compactJson(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

void logPublishedMessage(DashboardBackend *backend, VisionMqttPublisher *mqtt, const QString &topic, const QJsonObject &payload, bool retain = false) {
    const QByteArray encoded = compactJson(payload);
    const bool published = mqtt != nullptr && mqtt->publish(topic.toUtf8().constData(), encoded, retain);

    if (backend != nullptr && published) {
        backend->addMqttPublishLog(topic, QString::fromUtf8(encoded));
    }
}
```

这里最值得注意的是 `published` 的判定。  
只有 `mqtt->publish(...)` 返回 `true`，`DashboardBackend` 才会记一条 `source = "MQTT"` 的日志，所以页面计数反映的是“确认成功 publish 的次数”，不是“调用过发布函数的次数”。

随后 `DashboardBackend` 会：

- 把日志记成 `source = "MQTT"`
- 递增 `mqttLogCount_`

页面上看到的“MQTT 上报次数”来自这里。

所以这项统计的真实含义是：

- 成功发布到 broker 的次数

它不是：

- 尝试发布次数
- 理论应发布次数
- detection 命中次数

## 10. 故障路径要怎么理解

### 10.1 broker 没连上

表现：

- `publish()` 返回 `false`
- 不会记成功发布日志
- MQTT 次数不会增加

不会发生：

- 视觉线程退出
- RTSP 线程退出
- UI 停止刷新

### 10.2 publish 失败

mosquitto 返回错误时：

- stderr 会有 `[VISION_MQTT] publish ... failed`
- 这条消息不会进入 `DashboardBackend` 的 MQTT 成功日志

这也是为什么页面上的 MQTT 计数可能比你预期小。

### 10.3 stream status 不一定都能被看到

因为 stream status 也是通过同一个 `VisionMqttPublisher` 发的，所以如果 MQTT 本身没连上，即使 RTSP 状态在本地发生了变化：

- 代码仍会尝试发送
- 但 broker 侧不一定能收到

本地日志上你仍能看到 VISION 类别日志，例如：

- RTSP push online
- RTSP encoder open failed
- RTSP push failed

## 11. 当前没有实现什么

这节最重要，因为 MQTT 很容易被文档写过头。

当前没有实现：

- `mosquitto_subscribe()`
- 任何 Vision 侧入站消息处理
- `gateway/{gateway_id}/cmd`
- MQTT command 解析与执行
- recorder 命令通道
- `record_done` 发布

所以以下说法都是错误的：

- “设备已经支持 MQTT 控制命令”
- “可以通过 broker 控制开始录像”
- “视觉链会在录像完成后上报 `record_done`”

这些都不是当前代码事实。

## 12. 当前 topic 命名和未来统一 topic 规划不是一回事

当前 Vision 代码里真正实现的是：

- `argi/device/rk3568-001/vision/detection`
- `argi/device/rk3568-001/stream/status`

仓库其它文档里提到过建议统一成：

- `gateway/{gateway_id}/event`
- `gateway/{gateway_id}/status`
- `gateway/{gateway_id}/cmd`
- `gateway/{gateway_id}/record_done`

这只能写成规划或 TODO，不能写成“当前已落地”。

## 13. 读代码时最值得跟的几处断点

如果你调试的是 publish 行为，建议从这几处下手：

1. `VisionRuntime::workerLoop()` 里 `mqtt.open(...)`
2. `publishDetection(...)`
3. `publishStreamStatus(...)`
4. `logPublishedMessage(...)`
5. `DashboardBackend::addMqttPublishLog(...)`

如果你调试的是页面统计，继续看：

6. `DashboardBackend::snapshotSystemStats()`
7. `SystemLogPage::refresh()`

## 14. 一个最短的判断标准

如果你想判断“Vision MQTT 现在到底做没做成”，最短判断标准只有三个：

1. `VisionRuntime` 已成功启动，并在主循环里拿到了 `detGroup`。
2. broker 连接已建立，`VisionMqttPublisher::isConnected()` 为真。
3. `DashboardBackend` 里能看到 `source = "MQTT"` 的 `PUB ...` 日志。

少任一项，都不能算“MQTT publish 真正打通”。
