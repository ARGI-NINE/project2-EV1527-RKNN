# RF433 IoT Gateway (master)

这份文档只描述 `project2_master` 当前源码已经落地、并且可以直接从代码证明的 RF433 网关事实。项目整体定位已经调整为“基于 RK3568 的多源事件感知与视频留证网关”，但本页只聚焦板侧 RF 事件输入、`stdout JSON envelope`、MQTT publish，以及它和 Qt / Vision 的真实边界。

## Current Facts

- `project2_master/linux_app/main.c` 已实现板侧 `rf_gateway` 主程序，RF 输入默认来自 `--rf-input /dev/rf433`。
- `rf_gateway` 的核心输出不是“只发 MQTT”，而是“先形成一条结构化协议消息，再同时服务本地 `stdout` 消费与 MQTT publish 结果标记”。
- Qt RF 页面不是订阅 broker 回流更新，而是直接启动 `rf_gateway` 子进程并解析它的 `stdout`。
- Vision 也会发布 MQTT 消息，但它走的是 `project2_master/qt_gui/vision/vision_runtime.cpp` 内部的 Qt 进程内发布路径，不复用 RF 的 `stdout envelope` 实现。
- 当前仓库中没有可证实的 MQTT command 订阅执行链，也没有 GPIO 干接点输入实现；这些只能作为后续扩展预留。

## Scope

本页只覆盖 `project2_master` 中与 IoT 相关的已实现链路：

- `/dev/rf433 -> rf_gateway -> stdout JSON -> Qt`
- `rf_gateway -> MQTT publish`
- `VisionRuntime -> MQTT stream_status / vision_detection`

本页不把“未来计划”写成“当前已完成”，也不把 `pc_sim` 的模拟路径当成板侧真实路径。

## Boundary With pc_sim

`project2_master` 描述的是 RK3568 板侧真实集成路径：Linux 驱动导出 `/dev/rf433`，用户态 `rf_gateway` 读取设备、解码 EV1527、输出结构化事件，再由 Qt 侧消费。

`project2_pc_sim` 描述的是 PC 侧回放与模拟：它可以帮助验证解析逻辑、界面联动和部分消息契约，但不能替代板侧 `serdev` 驱动、真实 `/dev/rf433`、真实 MQTT 连接状态，或者 RK3568 上的实际视觉运行时。

## Validation Boundary

这份文档以当前源码为准，强调的是“静态可证实事实”：

- 已实现：RF433 驱动到用户态事件输出、Qt 侧 `stdout` 消费、RF MQTT publish、Vision MQTT 状态与检测发布。
- 未实现：MQTT command 控制链、GPIO 干接点事件链、事件前后录像闭环、通过 RF 做开门授权或控制闸机。

因此，本文会明确写“哪些字段、topic、函数调用链来自真实代码”，也会明确写“哪些能力当前仓库里没有”。

## 一句话定位

`project2_master` 当前最稳定、最清晰的一条 IoT 主链是：

`/dev/rf433 -> linux_app/main.c(rf_gateway) -> stdout JSON envelope -> qt_gui/rf/rf_gateway_client.cpp -> DashboardBackend`

同时，`rf_gateway` 会把同一份 RF 业务 `payload` 尝试发布到 MQTT；VisionRuntime 则在 Qt 进程内独立发布 `stream_status` 与 `vision_detection`。

## 现有实现与边界

| 项目 | 当前状态 | 代码事实 |
| --- | --- | --- |
| RF433 驱动输入 | 已实现 | `linux_driver/rf433_drv.c` 导出 `/dev/rf433`，用户态从该设备读取帧 |
| RF 用户态解码与上报 | 已实现 | `linux_app/main.c` 解码 EV1527，输出 `device_status`、`rf_stats`、`rf_event` |
| RF Qt 页面消费 | 已实现 | `qt_gui/rf/rf_gateway_client.cpp` 启动 `rf_gateway`，按行解析 `stdout` JSON |
| RF MQTT publish | 已实现 | `emit_protocol_message()` 调 `mqtt_publisher_publish()`，topic 根为 `argi/device/rk3568-001` |
| Vision MQTT 发布 | 已实现 | `qt_gui/vision/vision_runtime.cpp` 发布 `stream/status` 与 `vision/detection` |
| MQTT command | 未实现 | 仓库中没有命令订阅、命令解析、命令执行调用链 |
| GPIO 干接点输入 | 未实现 | 仓库中没有对应 Linux GPIO 事件驱动和用户态接入逻辑 |
| 事件录像闭环 | 未实现 | 当前没有可证明的 `record_done` 输出与前后录像缓存实现 |

## RF 路径为什么是这套写法

当前代码把 RF 主链明确分成三层，这样职责边界很清楚：

1. 共享脉冲协议层  
   只负责“驱动与用户态如何交换一帧 RF 脉冲”。这一层关心的是帧结构、长度、CRC、脉冲数组，不关心 `addr`、`key`、`conf` 这种业务语义。

2. Linux 驱动层  
   `linux_driver/rf433_drv.c` 负责把串口侧采样结果整理成 `/dev/rf433` 可读帧。对用户态来说，驱动导出的是“原始脉冲帧”，不是已经解码好的 EV1527 业务对象。

3. 用户态网关层  
   `linux_app/main.c` 负责 EV1527 解码、稳定重复确认、低置信度过滤、重复事件抑制、JSON 组包、MQTT publish 和 `stdout` 协议输出。

这种分层有两个直接好处：

- 驱动 ABI 保持简单稳定。驱动只需要保证脉冲帧可读，不需要把 EV1527、未来其他 433MHz 协议、或者业务 topic 绑死在内核里。
- 用户态更容易演进。你可以在 `main.c` 里调整 `stable_repeat`、去重窗口、MQTT topic、JSON 字段，而不需要改驱动 ABI。

对本项目定位来说，这也符合“旁路事件感知与视频留证网关”的边界：RF433/EV1527 在这里是一个事件源适配器，用于识别自有授权 RF 节点、做重复帧确认和误触发过滤、输出结构化事件；它不是开门授权控制器，也不是为了替代原有门禁主控。

## `stdout JSON envelope` 和 MQTT 的关系

先看 `project2_master/linux_app/main.c` 顶部定义的真实常量：

```c
#define MQTT_BROKER_HOST "192.168.30.26"
#define MQTT_BROKER_PORT 1883
#define MQTT_DEVICE_ID "rk3568-001"
#define MQTT_TOPIC_ROOT "argi/device/rk3568-001"
#define MQTT_TOPIC_STATUS "status"
#define MQTT_TOPIC_RF_EVENT "rf/event"
#define MQTT_TOPIC_RF_STATS "rf/stats"
```

这几行已经把当前板侧 RF 网关的 MQTT 身份写死得很清楚：

- 设备 ID 是 `rk3568-001`
- topic 根路径是 `argi/device/rk3568-001`
- RF 事件 topic 尾段是 `rf/event`
- RF 统计 topic 尾段是 `rf/stats`

因此，`rf_event` 最终完整 topic 就是 `argi/device/rk3568-001/rf/event`。这不是文档推测，而是 `MQTT_TOPIC_ROOT` 与 `MQTT_TOPIC_RF_EVENT` 在代码中的真实拼接结果。

下面这条 JSON 不是文档臆造的示意字段集合，而是把 `build_rf_event_payload()` 与 `build_protocol_line()` 两层结构展开后的典型运行结果。字段集合、字段层级和 topic 命名都严格对应源码；只有数值会随每次真实 RF 帧变化：

```json
{
  "type": "rf_event",
  "topic": "argi/device/rk3568-001/rf/event",
  "mqtt_published": true,
  "payload": {
    "device_id": "rk3568-001",
    "type": "rf_event",
    "rf_input": "/dev/rf433",
    "addr": "0x12ABCD",
    "key": "1",
    "conf": 0.9825,
    "confidence": 0.9825,
    "src": "c",
    "source": "c",
    "seq": 37,
    "drv_seq": 105,
    "timestamp_ns": 1719561234567890,
    "decode_us": 184,
    "mqtt_connected": true,
    "pulse_count": 8,
    "pulse_us": [321, 963, 318, 964, 319, 321, 318, 965]
  }
}
```

这条结构在源码里的真实调用链是：

`on_rf_frame() -> build_rf_event_payload(...) -> emit_protocol_message(...) -> build_protocol_line() -> fprintf(stdout, "%s\\n", line)`

这里要特别分清两个布尔状态：

- `payload.mqtt_connected` 来自 `build_rf_event_payload()` 内部的 `mqtt_publisher_is_connected(&ctx->mqtt)`，表示“构造 payload 时 MQTT 连接是否在线”。
- 外层 `mqtt_published` 来自 `emit_protocol_message()` 中 `mqtt_publisher_publish()` 的返回结果，表示“这一条消息本次 publish 是否真的成功”。

这两个字段不是重复字段，而是在表达两个不同阶段的事实：前者是连接状态，后者是这次发送动作的结果。

### 这层 envelope 实际是怎么拼出来的

代码来源：`project2_master/linux_app/main.c`

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

这段代码把 `stdout JSON envelope` 的真实语义写得非常明确：

1. `build_protocol_line()` 负责把根对象固定成四层字段：`type`、`topic`、`mqtt_published`、`payload`。
2. `topic` 不是调用方直接传完整字符串，而是由 `MQTT_TOPIC_ROOT` 和 `subtopic` 通过 `"%s/%s"` 拼成完整路径。
3. `mqtt_published` 不是字符串，而是 `json_bool(mqtt_published)` 写入的 JSON 布尔值。
4. `emit_protocol_message()` 的实际执行顺序是：  
   先检查连接并尝试 `mqtt_publisher_publish()`，再用这次发送结果组装 envelope，最后把整条 line 输出到 `stdout`。

这意味着 Qt 看到的 `mqtt_published` 不是“理论上应该能发”，而是“这一次 publish 调用是否成功”。同时也意味着 Qt 页面并不依赖 broker 回流：即使没有 broker 回流订阅，Qt 仍然可以只靠 `stdout` 这条本地协议线拿到 RF 事件、topic 和发送结果。

还有一个容易看错的细节：如果 MQTT 没连上，`emit_protocol_message()` 不会调用 `mqtt_publisher_publish()`，但只要 `build_protocol_line()` 成功，`stdout` 仍会输出完整 envelope。这就是为什么板侧 GUI 在离线时仍然可以显示 RF 事件，只是 `mqtt_published` 会是 `false`。

### `rf_event.payload` 也来自真实成帧代码

代码来源：`project2_master/linux_app/main.c`

```c
static int build_rf_event_payload(
    char *payload,
    size_t capacity,
    const app_ctx_t *ctx,
    const rf_frame_t *frame,
    const rf_decoded_packet_t *pkt,
    const rf_decode_last_call_stats_t *call_stats,
    uint64_t timestamp_ns,
    uint32_t drv_seq
) {
    size_t offset = 0u;
    uint16_t i = 0u;

    if (
        payload == NULL ||
        ctx == NULL ||
        frame == NULL ||
        pkt == NULL ||
        call_stats == NULL
    ) {
        return -1;
    }

    if (
        appendf(
            payload,
            capacity,
            &offset,
            "{"
            "\"device_id\":\"%s\","
            "\"type\":\"rf_event\","
            "\"rf_input\":\"%s\","
            "\"addr\":\"%s\","
            "\"key\":\"%s\","
            "\"conf\":%.4f,"
            "\"confidence\":%.4f,"
            "\"src\":\"%s\","
            "\"source\":\"%s\","
            "\"seq\":%u,"
            "\"drv_seq\":%u,"
            "\"timestamp_ns\":%llu,"
            "\"decode_us\":%llu,"
            "\"mqtt_connected\":%s,"
            "\"pulse_count\":%u,"
            "\"pulse_us\":[",
            MQTT_DEVICE_ID,
            ctx->rf_input,
            pkt->addr,
            pkt->key,
            pkt->confidence,
            pkt->confidence,
            pkt->source,
            pkt->source,
            (unsigned)ctx->frame_seq,
            (unsigned)drv_seq,
            (unsigned long long)timestamp_ns,
            (unsigned long long)call_stats->total_us,
            json_bool(mqtt_publisher_is_connected(&ctx->mqtt)),
            (unsigned)frame->len
        ) != 0
    ) {
        return -1;
    }

    for (i = 0u; i < frame->len; ++i) {
        if (appendf(payload, capacity, &offset, "%s%u", (i == 0u) ? "" : ",", (unsigned)frame->pulse[i]) != 0) {
            return -1;
        }
    }

    if (appendf(payload, capacity, &offset, "]}") != 0) {
        return -1;
    }

    return 0;
}
```

这段代码说明 `payload` 里的每一个关键字段都能追溯到真实数据来源：

- `device_id` 来自编译期常量 `MQTT_DEVICE_ID`
- `rf_input` 来自当前用户态实际打开的输入路径 `ctx->rf_input`
- `addr`、`key`、`conf`、`source` 来自 RF 解码结果 `rf_decoded_packet_t`
- `seq` 是用户态帧序号 `ctx->frame_seq`
- `drv_seq` 和 `timestamp_ns` 是驱动层跟随帧带上来的序号与时间戳
- `decode_us` 是最近一次解码统计 `call_stats->total_us`
- `pulse_count` 与 `pulse_us[]` 直接来自当前原始帧 `rf_frame_t`

这里还保留了两组兼容字段：

- `conf` 与 `confidence`
- `src` 与 `source`

这说明当前用户态 payload 已经承担了上层字段兼容职责。驱动层并不知道这些字段名，也不关心 JSON；这些都是用户态网关根据业务需求组装的。

更关键的是，`pulse_us[]` 不是文档示意数组，而是下面这段真实循环逐个写入的：

```c
for (i = 0u; i < frame->len; ++i) {
    if (appendf(payload, capacity, &offset, "%s%u", (i == 0u) ? "" : ",", (unsigned)frame->pulse[i]) != 0) {
        return -1;
    }
}
```

所以 `pulse_count` 与 `pulse_us[]` 可以用于事后核对这次 RF 事件到底来自哪一帧原始脉冲，而不是只看一个抽象后的地址码。

`payload` 也不是“只要读到一帧就一定上报”。真正的触发链在 `on_rf_frame()`，代码如下：

```c
static int on_rf_frame(const rf_frame_t *frame, uint64_t timestamp_ns, uint32_t drv_seq, void *user) {
    app_ctx_t *ctx = (app_ctx_t *)user;
    rf_decoded_packet_t pkt;
    rf_decode_last_call_stats_t call_stats;
    char payload[JSON_PAYLOAD_CAPACITY];
    int rc = 0;

    if (ctx == NULL || frame == NULL) {
        return -1;
    }

    if (ctx->has_drv_seq && drv_seq > ctx->drv_seq_prev) {
        const uint32_t gap = drv_seq - ctx->drv_seq_prev - 1u;
        if (gap > 0u) {
            if (ctx->drv_drop > (0xFFFFFFFFu - gap)) {
                ctx->drv_drop = 0xFFFFFFFFu;
            } else {
                ctx->drv_drop += gap;
            }
        }
    }
    ctx->drv_seq_prev = drv_seq;
    ctx->has_drv_seq = 1;

    ctx->frames_total++;
    ctx->frame_seq++;
    rc = rf_decode_frame(frame, &pkt);
    memset(&call_stats, 0, sizeof(call_stats));
    rf_decode_get_last_call_stats(&call_stats);
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

    if (build_rf_event_payload(payload, sizeof(payload), ctx, frame, &pkt, &call_stats, timestamp_ns, drv_seq) != 0) {
        fprintf(stderr, "[RF_JSON] failed to assemble rf event payload\n");
        return 0;
    }

    (void)emit_protocol_message(ctx, "rf_event", MQTT_TOPIC_RF_EVENT, payload, 0);
    ctx->last_code = pkt.raw_code;
    ctx->has_last_code = 1;
    ctx->last_publish_seq = ctx->frame_seq;
    return 0;
}
```

这里能直接看出当前 RF 事件发布前的真实处理顺序：

1. 统计驱动序号缺口，累计 `drv_drop`
2. 调 `rf_decode_frame()` 把脉冲帧解码成 EV1527 结果
3. 统计 `decode_no_frame` / `decode_err` / `decode_ok`
4. 低于 `min_publish_confidence` 直接丢弃
5. 如果开启 `stable_repeat`，则进入稳定分组确认，未达到确认次数不发布
6. 如果命中重复码与发布间隔窗口，则做重复抑制
7. 只有通过以上过滤后，才构造 `rf_event.payload`
8. 最后调用 `emit_protocol_message()` 生成 MQTT publish 结果和 `stdout` envelope

因此，`rf_event` 在当前项目中不是“串口一有脉冲就直接发”，而是一个经过解码、置信度过滤、重复确认、去重之后的结构化 RF 事件。

## Qt 侧具体消费了什么

Qt 侧不是去订阅 MQTT broker，也不是直接读 `/dev/rf433`。它消费的是 `rf_gateway` 这个子进程的 `stdout` 协议行。

代码来源：`project2_master/qt_gui/rf/rf_gateway_client.cpp`

```cpp
QStringList RFGatewayClient::buildGatewayArgs() const {
    QStringList args;
    args << "--rf-input" << resolvedRfInputPath();
    return args;
}

void RFGatewayClient::startGateway() {
    if (backend_ == nullptr) {
        return;
    }

    const QString gatewayPath = resolveGatewayPath();
    const QString rfInputPath = resolvedRfInputPath();
    if (gatewayPath.isEmpty()) {
        backend_->updateSerialStatus(false);
        backend_->addLog(
            "ERROR",
            "SYSTEM",
            QString("未找到固定路径 rf_gateway: %1（板侧版本不允许 gateway 路径注入）")
                .arg(QFileInfo(fixedGatewayPath()).absoluteFilePath())
        );
        return;
    }

    gateway_ = new QProcess(context_);
    gateway_->setProcessChannelMode(QProcess::SeparateChannels);
    gatewayStdoutBuffer_.clear();
    gatewayStderrBuffer_.clear();

    QObject::connect(gateway_, &QProcess::readyReadStandardOutput, context_, [this]() {
        if (gateway_ == nullptr) {
            return;
        }

        drainProtocolBuffer(gateway_->readAllStandardOutput());
    });

    QObject::connect(gateway_, &QProcess::readyReadStandardError, context_, [this]() {
        if (gateway_ == nullptr) {
            return;
        }

        appendDiagnosticChunk(gateway_->readAllStandardError());
    });
}
```

这段代码说明了三件事：

- Qt 启动 `rf_gateway` 时会显式传 `--rf-input <resolved path>`，也就是板侧实际要读的 RF 设备。
- `stdout` 和 `stderr` 被分开处理。`stdout` 走协议解析，`stderr` 走诊断日志。
- RF 页面的主数据源是 `readyReadStandardOutput` 触发后的 `drainProtocolBuffer()`，不是 MQTT 订阅回环。

真正的协议解析代码如下：

```cpp
bool RFGatewayClient::parseProtocolEnvelope(
    const QString &line,
    QString *type,
    QString *topic,
    bool *mqttPublished,
    QJsonObject *payload
) const {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);
    const QJsonObject root = doc.object();

    if (type == nullptr || topic == nullptr || mqttPublished == nullptr || payload == nullptr) {
        return false;
    }
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    *type = scalarJsonString(root.value(QStringLiteral("type")));
    *topic = scalarJsonString(root.value(QStringLiteral("topic")));
    *mqttPublished = root.value(QStringLiteral("mqtt_published")).toBool(false);
    if (type->isEmpty() || !root.value(QStringLiteral("payload")).isObject()) {
        return false;
    }

    *payload = root.value(QStringLiteral("payload")).toObject();
    return true;
}

void RFGatewayClient::handleProtocolLine(const QString &line) {
    QString type;
    QString topic;
    QJsonObject payload;
    bool mqttPublished = false;

    if (backend_ == nullptr) {
        return;
    }

    if (!parseProtocolEnvelope(line, &type, &topic, &mqttPublished, &payload)) {
        backend_->incrementParseError();
        backend_->addLog("WARN", "RF", QString("Invalid rf_gateway protocol JSON: %1").arg(line));
        return;
    }

    if (type == QStringLiteral("rf_event")) {
        RFEvent event;
        QVector<int> pulses;

        if (!parseRFEventPayload(payload, &event, &pulses)) {
            backend_->incrementParseError();
            backend_->addLog("WARN", "RF", QString("Invalid rf_event payload: %1").arg(line));
            return;
        }

        backend_->updateSerialStatus(true, scalarJsonString(payload.value(QStringLiteral("rf_input"))).isEmpty()
            ? resolvedRfInputPath()
            : scalarJsonString(payload.value(QStringLiteral("rf_input"))));
        backend_->addRFEvent(event, pulses);
        backend_->addLog("INFO", "RF", line);
        if (mqttPublished && !topic.isEmpty()) {
            backend_->addMqttPublishLog(topic, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        }
        return;
    }

    if (type == QStringLiteral("device_status")) {
        const QString rfInputPath = scalarJsonString(payload.value(QStringLiteral("rf_input")));
        const bool rfOnline = payload.value(QStringLiteral("rf_online")).toBool(false);
        const int crcErrors = payload.value(QStringLiteral("driver_crc_err")).toInt(-1);
        const int driverDropFrames = payload.value(QStringLiteral("app_drv_drop")).toInt(-1);

        backend_->updateSerialStatus(rfOnline, rfInputPath.isEmpty() ? resolvedRfInputPath() : rfInputPath);
        backend_->updateProtocolStats(crcErrors, -1, driverDropFrames);
        backend_->addLog("INFO", "RF", line);
        if (mqttPublished && !topic.isEmpty()) {
            backend_->addMqttPublishLog(topic, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        }
        return;
    }

    if (type == QStringLiteral("rf_stats")) {
        const int crcErrors = payload.value(QStringLiteral("driver_crc_err")).toInt(-1);
        const int parseErrors =
            payload.value(QStringLiteral("decode_no_frame")).toInt(0) +
            payload.value(QStringLiteral("decode_err")).toInt(0);
        const int driverDropFrames = payload.value(QStringLiteral("drv_drop")).toInt(-1);

        if (payload.contains(QStringLiteral("driver_online"))) {
            backend_->updateSerialStatus(
                payload.value(QStringLiteral("driver_online")).toBool(false),
                scalarJsonString(payload.value(QStringLiteral("rf_input"))).isEmpty()
                    ? resolvedRfInputPath()
                    : scalarJsonString(payload.value(QStringLiteral("rf_input")))
            );
        }
        backend_->updateProtocolStats(crcErrors, parseErrors, driverDropFrames);
        backend_->addLog("INFO", "RF", line);
        if (mqttPublished && !topic.isEmpty()) {
            backend_->addMqttPublishLog(topic, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        }
        return;
    }

    backend_->incrementParseError();
    backend_->addLog("WARN", "RF", QString("Unknown rf_gateway protocol type: %1").arg(line));
}
```

这段代码把 Qt 侧消费模型讲得很直白：

- `parseProtocolEnvelope()` 先拆根对象：`type`、`topic`、`mqtt_published`、`payload`
- `handleProtocolLine()` 再按 `type` 分发给 `rf_event`、`device_status`、`rf_stats`
- `rf_event` 会进一步调用 `parseRFEventPayload()`，把 `addr`、`key`、`conf`、`pulse_us[]` 等字段转成 `RFEvent`
- `mqttPublished && !topic.isEmpty()` 时，Qt 才会把这次 MQTT 发送记到发布日志里

这也说明为什么 RF 页面、系统日志、MQTT 发布日志虽然是不同界面元素，但实际上都由同一条 `rf_gateway stdout` 主线驱动。MQTT 在这里是“旁路上报”，不是 GUI 的唯一数据源。

## 与 Vision 的关系

Vision 在当前仓库里同样属于“事件感知与状态上报”的一部分，但它和 RF 不是同一条实现链：

- RF：`linux_app/main.c` 用户态网关进程产出 `stdout envelope`，Qt 作为子进程消费者读取
- Vision：`qt_gui/vision/vision_runtime.cpp` 在 Qt 进程内部直接形成 JSON，并直接调用 `VisionMqttPublisher`

两条链最后都会更新 `DashboardBackend`，但中间实现形式完全不同。理解这一点很重要，因为这决定了：

- RF 路径的本地契约是“按行 JSON 协议”
- Vision 路径的本地契约是“Qt 进程内对象与回调”
- 两者都可以发 MQTT，但只有 RF 这条链额外维护了 `stdout JSON envelope`

### Vision 的状态与检测消息是 Qt 进程内直接发的

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`

```cpp
constexpr char kDeviceId[] = "rk3568-001";
constexpr char kMqttHost[] = "192.168.30.26";
constexpr int kMqttPort = 1883;
constexpr char kTopicVisionDetection[] = "argi/device/rk3568-001/vision/detection";
constexpr char kTopicStreamStatus[] = "argi/device/rk3568-001/stream/status";

void logPublishedMessage(DashboardBackend *backend, VisionMqttPublisher *mqtt, const QString &topic, const QJsonObject &payload, bool retain = false) {
    const QByteArray encoded = compactJson(payload);
    const bool published = mqtt != nullptr && mqtt->publish(topic.toUtf8().constData(), encoded, retain);

    if (backend != nullptr && published) {
        backend->addMqttPublishLog(topic, QString::fromUtf8(encoded));
    }
}

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

这段代码和 RF 路径形成了非常鲜明的对照：

- Vision 直接在 Qt 进程里构造 `QJsonObject`
- Vision 没有 `build_protocol_line()`，也没有 `stdout` 根 envelope
- Vision MQTT topic 直接是完整字符串：`argi/device/rk3568-001/stream/status`、`argi/device/rk3568-001/vision/detection`
- Vision 只在 publish 成功时记 MQTT 发布日志，不会像 RF 那样额外输出一条 `stdout` 协议行

RTSP 在线/离线状态也是在 VisionRuntime 内部直接发出的，代码同样是实际实现：

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
```

以及带检测结果的 MQTT 发布：

```cpp
snapshot.detections = formatDetections(detGroup);
snapshot.frame = renderAnnotatedFrame(rgbImage, detGroup);
if (detGroup.count > 0) {
    publishDetection(backend_, &mqtt, rtspUrl, frameId, captureTsUs, detGroup);
}
```

也就是说，Vision 当前的 MQTT 语义是：

- RTSP 分支上线、失败、停止、禁用时发 `stream_status`
- 检测结果存在时发 `vision_detection`
- 本地显示帧和 MQTT 检测消息都由 Qt 进程内的同一份检测结果驱动

这正是“多源事件感知网关”的当前代码形态：RF 是外部子进程事件源，Vision 是进程内视觉事件源，它们都会汇入同一个板侧 dashboard 与 MQTT 输出体系，但它们的本地消息契约并不相同。

## 建议阅读顺序

如果要继续追这部分实现，建议按下面顺序读：

1. `project2_master/linux_app/main.c`  
   先把 `MQTT_TOPIC_*`、`build_protocol_line()`、`build_rf_event_payload()`、`on_rf_frame()` 读透。

2. `project2_master/qt_gui/rf/rf_gateway_client.cpp`  
   再确认 Qt 是如何启动 `rf_gateway`、怎样读取 `stdout`、怎样把 `rf_event`/`rf_stats` 转成界面状态。

3. `project2_master/qt_gui/vision/vision_runtime.cpp`  
   对照 RF 路径，理解 Vision 的 MQTT 发布、RTSP 状态发布、检测结果发布为什么是另一套进程内实现。

4. `project2_master/docs/project2_iot_design.md`
5. `project2_master/docs/project2_shared_protocol_deep_dive.md`
6. `project2_master/docs/project2_master_driver_deep_dive.md`
7. `project2_master/docs/project2_master_userland_deep_dive.md`
8. `project2_master/docs/project2_master_reading_guide_zh.md`

读完上面三处源码再回来看这些文档，会更容易分清楚：

- 哪些是驱动 ABI
- 哪些是用户态 JSON 契约
- 哪些是 Qt 本地消费逻辑
- 哪些是 Vision 独立的 MQTT / RTSP 发布链
