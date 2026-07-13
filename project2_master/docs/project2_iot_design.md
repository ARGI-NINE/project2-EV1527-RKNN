# project2 IoT 设计（master 实时运行口径）

这份文档站在“系统总览”的角度，解释 `project2_master` 当前到底实现到了哪一步。它不替代后面的深读文档，而是先帮你立住边界，避免在代码走读前把几条链路混成一团。

兼容说明：以下先补回 `HEAD` 版章节骨架，便于沿用旧目录、旧引用和旧阅读顺序；后文现有正文、源码摘录和细讲全部保留。

## 统一口径（2026-04-21）

兼容旧版目录：下文现有正文继续按 `master` 实时运行口径展开，保留 RF 主链、Qt 消费、Vision 本地 runtime、未实现输入链和验证边界说明。

## 1. 设计目标

兼容旧版目录：对应下文现有 `## 1. 先讲结论` 与 `## 2. 系统分层图` 的总体目标说明。

## 2. 三端职责边界

兼容旧版目录：对应当前正文里 hardware、master、pc_sim 以及 Qt/Vision 各自边界的说明。

## 3. 协议设计

兼容旧版目录：对应下文现有 `## 3. RF 主链逐层看` 中的共享协议、驱动和用户态边界说明。

## 4. 字段映射与允许差异

兼容旧版目录：对应当前正文里 `/dev/rf433`、`rf_gateway`、`stdout JSON`、MQTT、Qt 之间的字段与职责关系。

### 4.1 统计字段语义（master）

兼容旧版目录：对应当前正文里驱动在线位、驱动统计、应用侧统计和 UI 消费字段说明。

## 5. 组件职责

兼容旧版目录：对应下文现有 `## 3. RF 主链逐层看`、`## 4. /dev/rf433、rf_gateway、stdout JSON、MQTT、Qt 的关系`。

## 6. 运行基线

兼容旧版目录：对应当前正文里板侧运行路径、`/dev/rf433`、`rf_gateway`、Qt Dashboard 与本地 Vision runtime 的说明。

## 7. 当前验证边界

兼容旧版目录：对应下文现有 `## 5. 当前已实现输出`、`## 6. 当前没有实现的输入与输出`、`## 7. Vision 在这份文档里的边界`。

## 8. 排错清单

兼容旧版目录：对应当前正文里链路边界、未实现输入和推荐阅读顺序下的定位说明。

## 9. 互引

兼容旧版目录：对应下文现有 `## 8. 推荐阅读顺序` 及现有互引阅读路径。

## 1. 先讲结论

截至当前代码状态，`project2_master` 可以被描述为：

- 一个运行在 RK3568 板侧的事件网关目录
- 其中 RF 主链已经具备“采集 -> 内核成帧 -> 用户态解码 -> 本地 JSON 输出 -> Qt 消费 -> 可选 MQTT publish”的完整路径
- Vision 主链已经在 Qt 进程内本地运行

但下面这些内容不能写成“已实现能力”：

- MQTT command 订阅与执行
- GPIO dry contact 接入
- 事件录像完成通知或 `record_done` 事件闭环

## 2. 系统分层图

```text
Event Sources
  -> RF433 pulse source (implemented)
  -> Vision local runtime (implemented)
  -> MQTT command (not implemented)
  -> GPIO input (not implemented)

Normalization / Runtime
  -> RF shared pulse-frame protocol
  -> Linux serdev driver exposing /dev/rf433
  -> rf_gateway userland decode + JSON envelope + optional MQTT publish
  -> Qt local Vision runtime

Outputs
  -> Qt RF page and system log page
  -> MQTT publish side-branch
  -> Vision page
```

这张图里最重要的阅读原则是：

- RF 与 Vision 最终都汇入 Qt，但它们不是同一种接入方式。
- RF 是“外部子进程 `rf_gateway` + `stdout` 协议”。
- Vision 是“Qt 进程内本地运行时”。

## 3. RF 主链逐层看

### 3.1 硬件与共享协议

硬件侧和 master 侧共识的只有一件事：怎样表达“一帧脉冲”。

共享协议文件在：

- `project2_master/common/rf_protocol.h`
- `project2_master/common/rf_protocol.c`
- `project2_hardware/Hardware/RF_Protocol.h`
- `project2_hardware/Hardware/RF_Protocol.c`

它定义的是：

- 同步头 `0xAA 0x55`
- 小端 `LEN`
- `pulse_us[]`
- XOR CRC

来源文件：`project2_master/common/rf_protocol.h`  
函数/定义：`RF_PROTO_SYNC0`、`RF_PROTO_SYNC1`、`rf_frame_t`  
作用：共享协议先只定义“脉冲帧是什么”，还没有任何 EV1527 语义字段。

```c
#define RF_PROTO_SYNC0 0xAAu
#define RF_PROTO_SYNC1 0x55u
#define RF_BUFFER_SIZE 1024u

typedef struct {
    uint16_t pulse[RF_BUFFER_SIZE];
    uint16_t len;
} rf_frame_t;

uint8_t rf_proto_crc8(const uint8_t *data, size_t len);
size_t rf_proto_encode(const rf_frame_t *frame, uint8_t *out, size_t out_capacity);
```

这段头文件把共享 ABI 压得很薄：只有同步头、脉冲数组和长度。  
也正因为 `rf_frame_t` 只携带 `pulse[]`，所以硬件侧和 Linux/Qt 侧共享的是“波形事实”，不是“按钮语义解释”。

来源文件：`project2_master/common/rf_protocol.c`  
函数：`rf_proto_crc8()`、`rf_proto_encode()`  
作用：把 `rf_frame_t` 编码成 `AA 55 LEN_LO LEN_HI PAYLOAD CRC` 的字节流。

```c
uint8_t rf_proto_crc8(const uint8_t *data, size_t len) {
    size_t i = 0u;
    uint8_t crc = 0u;
    for (i = 0u; i < len; ++i) {
        crc ^= data[i];
    }
    return crc;
}

size_t rf_proto_encode(const rf_frame_t *frame, uint8_t *out, size_t out_capacity) {
    uint16_t i = 0u;
    uint16_t bytes = 0u;
    uint8_t crc = 0u;
    size_t total = 0u;

    if (frame == NULL || out == NULL) {
        return 0u;
    }
    if (frame->len == 0u || frame->len > RF_BUFFER_SIZE) {
        return 0u;
    }

    bytes = (uint16_t)(frame->len * 2u);
    total = (size_t)2u + 2u + bytes + 1u;
    if (out_capacity < total) {
        return 0u;
    }

    out[0] = RF_PROTO_SYNC0;
    out[1] = RF_PROTO_SYNC1;
    out[2] = (uint8_t)(frame->len & 0xFFu);
    out[3] = (uint8_t)((frame->len >> 8u) & 0xFFu);

    for (i = 0u; i < frame->len; ++i) {
        const uint16_t p = frame->pulse[i];
        const size_t off = (size_t)4u + (size_t)i * 2u;
        out[off] = (uint8_t)(p & 0xFFu);
        out[off + 1u] = (uint8_t)((p >> 8u) & 0xFFu);
    }

    crc = rf_proto_crc8(&out[2], (size_t)2u + bytes);
    out[4u + bytes] = crc;
    return total;
}
```

这里可以直接看出协议边界：CRC 只覆盖 `LEN + PAYLOAD`，并不包含同步头。  
同时每个 pulse 都按 `uint16_t` 小端写进 payload，这正是驱动 parser 后面按两字节恢复脉冲宽度的依据。

它没有定义：

- `addr`
- `key`
- `conf` / `confidence`
- MQTT topic
- Qt 页面字段

所以共享协议的边界必须停在 pulse frame，而不能越界写成“共享协议已经携带 EV1527 语义字段”。

### 3.2 Linux 驱动层

`project2_master/linux_driver/rf433_drv.c` 是一份 serdev client driver，同时注册了 misc device。

它在运行时做的事是：

1. 从 serdev 回调接收 UART 字节流
2. 在内核中执行 `SYNC0 -> SYNC1 -> LEN0 -> LEN1 -> PAYLOAD -> CRC` 状态机
3. 校验通过后组装 `struct rf433_frame`
4. 放入 `kfifo`
5. 通过 misc 设备对用户态导出 `/dev/rf433`

来源文件：`project2_master/linux_driver/rf433_drv.c`  
函数：`parser_feed_byte()`、`rf433_receive_buf()`  
作用：驱动在中断/回调路径里逐字节吃串口数据，只有完整过完状态机才会进入成帧输出。

```c
static void parser_feed_byte(struct rf433_priv *priv, u8 byte)
{
	priv->last_byte_jiffies = jiffies;

	switch (priv->state) {
	case RF_ST_SYNC0:
		if (byte == SYNC0)
			priv->state = RF_ST_SYNC1;
		break;

	case RF_ST_SYNC1:
		if (byte == SYNC1) {
			priv->state       = RF_ST_LEN0;
			priv->crc_accum   = 0;
			priv->expected_pulses = 0;
			priv->payload_idx = 0;
		} else if (byte == SYNC0) {
			/* stay in SYNC1 — consecutive 0xAA */
		} else {
			priv->state = RF_ST_SYNC0;
		}
		break;

	case RF_ST_LEN0:
		priv->expected_pulses = byte;
		priv->crc_accum       = byte;
		priv->state           = RF_ST_LEN1;
		break;

	case RF_ST_LEN1:
		priv->expected_pulses |= (u16)byte << 8;
		priv->crc_accum       ^= byte;
		if (priv->expected_pulses == 0 ||
		    priv->expected_pulses > RF433_MAX_PULSES) {
			priv->stats.len_err++;
			parser_reset(priv);
		} else {
			priv->state = RF_ST_PAYLOAD;
		}
		break;

	case RF_ST_PAYLOAD:
		priv->payload_buf[priv->payload_idx++] = byte;
		priv->crc_accum ^= byte;
		if (priv->payload_idx >= (u16)(priv->expected_pulses * 2u))
			priv->state = RF_ST_CRC;
		break;

	case RF_ST_CRC:
		if (byte != priv->crc_accum) {
			priv->stats.crc_err++;
			parser_reset(priv);
		} else {
			parser_emit_frame(priv);
			parser_reset(priv);
		}
		break;

	default:
		parser_reset(priv);
		break;
	}
}

static int rf433_receive_buf(struct serdev_device *serdev,
			     const unsigned char *buf, size_t count)
{
	struct rf433_priv *priv = serdev_device_get_drvdata(serdev);
	unsigned long flags;
	size_t i;

	spin_lock_irqsave(&priv->lock, flags);
	for (i = 0; i < count; i++)
		parser_feed_byte(priv, buf[i]);
	spin_unlock_irqrestore(&priv->lock, flags);

	return count;
}
```

这个 parser 和上面的共享协议是严格对齐的：先认 `AA 55`，再读小端长度，再累计 payload XOR，最后核对 CRC。  
注意 `rf433_receive_buf()` 没有直接向用户态透传字节，而是把串口流全部收进状态机，所以 `/dev/rf433` 不是原始 UART 口。

来源文件：`project2_master/linux_driver/rf433_drv.c`  
函数：`rf433_misc_read()`、`rf433_misc_ioctl()`  
作用：把 parser 已经确认完整的帧从 `kfifo` 导出给用户态，同时暴露状态和统计。

```c
static ssize_t rf433_misc_read(struct file *filp, char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	struct rf433_priv *priv = filp->private_data;
	struct rf433_frame frame;
	unsigned long flags;
	int ret;

	if (count < sizeof(frame))
		return -EINVAL;

	if (filp->f_flags & O_NONBLOCK) {
		spin_lock_irqsave(&priv->lock, flags);
		ret = kfifo_out(&priv->fifo, &frame, 1);
		spin_unlock_irqrestore(&priv->lock, flags);
		if (ret == 0)
			return -EAGAIN;
	} else {
		ret = wait_event_interruptible(priv->rdq,
					       !kfifo_is_empty(&priv->fifo));
		if (ret)
			return -ERESTARTSYS;
		spin_lock_irqsave(&priv->lock, flags);
		ret = kfifo_out(&priv->fifo, &frame, 1);
		spin_unlock_irqrestore(&priv->lock, flags);
		if (ret == 0)
			return -EIO;
	}

	if (copy_to_user(ubuf, &frame, sizeof(frame)))
		return -EFAULT;

	return sizeof(frame);
}

case RF433_IOC_GET_STATUS: {
	struct rf433_status status;

	memset(&status, 0, sizeof(status));
	spin_lock_irqsave(&priv->lock, flags);
	status.online         = priv->online ? 1 : 0;
	status.seq            = priv->seq;
	status.queue_depth    = kfifo_len(&priv->fifo);
	status.queue_capacity = FRAME_FIFO_DEPTH;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (copy_to_user((void __user *)arg, &status, sizeof(status)))
		return -EFAULT;
	return 0;
}
```

`read()` 给出去的是整个 `struct rf433_frame`，这进一步证明用户态读到的是“整帧脉冲”。  
而 `GET_STATUS` 把 `online/seq/queue_depth` 一起导出，所以后面的 `rf_gateway` 不只是消费事件，还会周期性采集驱动健康状态。

因此 `/dev/rf433` 的语义不是“串口透传”，而是“整帧 RF 脉冲接口”。

### 3.3 用户态网关层

`project2_master/linux_app/main.c` 生成可执行文件 `rf_gateway`。

这层完成的是：

1. 打开 `/dev/rf433`
2. 用 epoll 持续读驱动帧
3. 调用 `rf_decode_frame()` 做 EV1527 解码
4. 做稳定分组、近邻合并和重复抑制
5. 生成 `device_status`、`rf_stats`、`rf_event`
6. 将它们包装成统一的 `stdout JSON envelope`
7. 如果 MQTT 已连接，则把同一份 payload 并行发布出去

来源文件：`project2_master/linux_app/main.c`  
函数：`build_rf_event_payload()`  
作用：用户态网关在这一层才把脉冲帧解释成 `addr/key/conf/source` 等带业务语义的事件 JSON。

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

这里就是 `addr`、`key`、`conf` 第一次出现的地方。  
共享协议阶段还只有 `pulse[]`，到了 `build_rf_event_payload()` 才把解码结果、驱动序号、decode 耗时和原始脉冲一起塞进事件 payload，所以业务语义是在用户态网关层生成的。

来源文件：`project2_master/linux_app/main.c`  
函数：`build_protocol_line()`、`emit_protocol_message()`  
作用：把刚才生成的 payload 包成 stdout 协议根对象，并在连接存在时对同一份 payload 做 MQTT 旁路发布。

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

这就是本文前面反复强调的 “stdout JSON envelope” 实际长相。  
根对象统一携带 `type/topic/mqtt_published/payload`，所以 Qt 消费的是这个 envelope 契约；MQTT 只是这层顺手并行出去的一条旁路。

来源文件：`project2_master/linux_app/main.c`  
函数：`on_rf_frame()`、`emit_device_status()`、`emit_rf_stats()`  
作用：把三类输出真正接到运行时调用点上，而不是停留在“理论上会发”。

```c
static void emit_device_status(app_ctx_t *ctx, const char *reason) {
    char payload[JSON_PAYLOAD_CAPACITY];

    if (build_device_status_payload(payload, sizeof(payload), ctx, reason) != 0) {
        fprintf(stderr, "[RF_JSON] failed to assemble device status payload\n");
        return;
    }

    (void)emit_protocol_message(ctx, "device_status", MQTT_TOPIC_STATUS, payload, 1);
}

static void emit_rf_stats(app_ctx_t *ctx, const char *reason) {
    char payload[JSON_PAYLOAD_CAPACITY];
    rf_decode_runtime_stats_t decode_stats;

    memset(&decode_stats, 0, sizeof(decode_stats));
    rf_decode_get_runtime_stats(&decode_stats);
    if (build_rf_stats_payload(payload, sizeof(payload), ctx, &decode_stats, reason) != 0) {
        fprintf(stderr, "[RF_JSON] failed to assemble rf stats payload\n");
        return;
    }

    (void)emit_protocol_message(ctx, "rf_stats", MQTT_TOPIC_RF_STATS, payload, 0);
}

if (build_rf_event_payload(payload, sizeof(payload), ctx, frame, &pkt, &call_stats, timestamp_ns, drv_seq) != 0) {
    fprintf(stderr, "[RF_JSON] failed to assemble rf event payload\n");
    return 0;
}

(void)emit_protocol_message(ctx, "rf_event", MQTT_TOPIC_RF_EVENT, payload, 0);
```

这段调用链把三类输出都钉死在源码里了：状态、统计、事件最后都汇入同一个 envelope 发送器。  
因此“RF 页面能消费什么”不是 Qt 自己定义的，而是 `rf_gateway` 在这里决定的。

注意：`addr`、`key`、`conf` 是这一步才出现的。

### 3.4 Qt 消费层

RF 侧 Qt 入口在：

- `qt_gui/rf/rf_gateway_client.cpp`
- `qt_gui/core/dashboard_backend.cpp`
- `qt_gui/rf/rf_status_page.cpp`

Qt 不是直接打开 `/dev/rf433`。它的做法是：

1. `RFGatewayClient` 从固定路径拉起 `rf_gateway`
2. 按行读取 `stdout`
3. 解析 envelope 根对象
4. 把不同 `type` 的 payload 写入 `DashboardBackend`
5. `RFStatusPage` 定时读取快照并刷新页面

来源文件：`project2_master/qt_gui/rf/rf_gateway_client.cpp`  
函数：`parseProtocolEnvelope()`、`parseRFEventPayload()`  
作用：Qt 先解析 envelope 根对象，再把 `rf_event` payload 转成前端使用的 `RFEvent + waveform`。

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

bool RFGatewayClient::parseRFEventPayload(const QJsonObject &payload, RFEvent *event, QVector<int> *pulses) const {
    const QString address = scalarJsonString(payload.value(QStringLiteral("addr")));
    const QString key = scalarJsonString(payload.value(QStringLiteral("key")));
    const QString source = scalarJsonString(payload.value(QStringLiteral("src")));
    const QJsonValue confValue = payload.value(QStringLiteral("conf"));
    const QJsonValue pulseArrayValue = payload.value(QStringLiteral("pulse_us"));
    const QJsonArray pulseArray = pulseArrayValue.toArray();

    if (event == nullptr || pulses == nullptr) {
        return false;
    }
    if (address.isEmpty() || key.isEmpty() || source.isEmpty() || !confValue.isDouble() || !pulseArrayValue.isArray()) {
        return false;
    }

    event->timestamp = QDateTime::currentDateTime();
    event->address = address;
    event->key = key;
    event->confidence = confValue.toDouble();
    event->source = source;
    event->frameSeq = payload.value(QStringLiteral("seq")).isDouble()
        ? static_cast<qint64>(payload.value(QStringLiteral("seq")).toDouble(-1.0))
        : -1;
    event->decodeUs = payload.value(QStringLiteral("decode_us")).isDouble()
        ? static_cast<qint64>(payload.value(QStringLiteral("decode_us")).toDouble(-1.0))
        : -1;

    pulses->clear();
    pulses->reserve(pulseArray.size());
    for (const QJsonValue &value : pulseArray) {
        const int pulseUs = value.toInt(-1);
        if (!value.isDouble() || pulseUs <= 0) {
            pulses->clear();
            return false;
        }
        pulses->append(pulseUs);
    }

    return true;
}
```

这里能看出 Qt 消费侧完全依赖 envelope 约定：先抽 `type/topic/mqtt_published/payload`，再根据 `type` 决定怎么解释 `payload`。  
而 `parseRFEventPayload()` 明确要求 `addr/key/src/conf/pulse_us` 都存在，所以 RF 事件格式不是 UI 自由发挥，而是被代码严格校验。

来源文件：`project2_master/qt_gui/rf/rf_gateway_client.cpp`  
函数：`handleProtocolLine()`  
作用：把不同 `type` 的 payload 分流写入 `DashboardBackend`，并且只在 `mqtt_published=true` 时补记 MQTT 成功日志。

```cpp
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

这段分发代码把“Qt 怎么消费 RF 输出”说得非常具体：  
`rf_event` 更新事件和波形，`device_status` 更新串口在线状态和驱动错误计数，`rf_stats` 更新解析错误和掉帧统计。  
同时只有 envelope 明确说 `mqtt_published=true`，Qt 才会再追加一条 MQTT 成功日志，所以页面上的 MQTT 次数仍然来源于实际成功发布。

来源文件：`project2_master/qt_gui/core/dashboard_backend.cpp`  
函数：`updateSerialStatus()`、`addRFEvent()`、`updateProtocolStats()`、`addMqttPublishLog()`  
作用：所谓“状态落库”在当前工程里并不是数据库，而是落到 `DashboardBackend` 的内存快照和日志容器。

```cpp
void DashboardBackend::updateSerialStatus(bool online, const QString &port) {
    QMutexLocker locker(&mutex_);
    serialOnline_ = online;
    serialPort_ = port;
}

void DashboardBackend::addRFEvent(const RFEvent &event, const QVector<int> &pulses) {
    QMutexLocker locker(&mutex_);
    hasLastDecode_ = true;
    lastDecode_ = event;
    waveform_ = pulses;
    if (waveform_.size() > kMaxWaveform) {
        waveform_.resize(kMaxWaveform);
    }
    eventHistory_.prepend(event);
    if (eventHistory_.size() > kMaxHistory) {
        eventHistory_.resize(kMaxHistory);
    }
    ++frameCount_;
}

void DashboardBackend::updateProtocolStats(int crcErrors, int parseErrors, int driverDropFrames) {
    QMutexLocker locker(&mutex_);
    if (crcErrors >= 0) {
        crcErrors_ = crcErrors;
    }
    if (parseErrors >= 0) {
        parseErrors_ = parseErrors;
    }
    if (driverDropFrames >= 0) {
        driverDropFrames_ = driverDropFrames;
    }
}

void DashboardBackend::addMqttPublishLog(const QString &topic, const QString &payload) {
    addLog("INFO", "MQTT", QString("PUB %1: %2").arg(topic, payload));
}
```

`DashboardBackend` 这里持有的是线程安全的内存态：最后一次解码、波形、历史事件、错误计数和 MQTT 日志计数。  
所以“Qt 状态落库”更准确地说是“落到 UI 共享状态容器”，而不是写 SQLite 或外部存储。

因此 Qt RF 页面和 `/dev/rf433` 之间隔着一个明确的用户态网关层，而不是直接连通。

## 4. `/dev/rf433`、`rf_gateway`、`stdout JSON`、MQTT、Qt 的关系

这是整套文档必须反复强调的主关系：

```text
/dev/rf433
  = 驱动导出的整帧输入接口

rf_gateway
  = /dev/rf433 的唯一现有用户态消费者
  = EV1527 解码器 + 状态/事件聚合器 + JSON envelope 生成器

stdout JSON envelope
  = rf_gateway 给本地进程使用的主输出契约
  = Qt RF 路径直接消费的对象

MQTT publish
  = rf_gateway 对同一份 payload 的可选旁路输出
  = 不决定 Qt 是否能工作

Qt RF 页面
  = 读取 rf_gateway 的 stdout 结果刷新状态、波形和历史表
  = 不是 MQTT 订阅客户端
```

如果一句话总结：

`/dev/rf433` 是输入边界，`stdout JSON envelope` 是本地进程间输出边界，MQTT 只是旁支，不是 RF 页面主输入。

## 5. 当前已实现输出

`rf_gateway` 当前会输出三类消息：

- `device_status`
- `rf_stats`
- `rf_event`

它们在代码里的实际 MQTT topic 前缀是：

- `argi/device/rk3568-001/status`
- `argi/device/rk3568-001/rf/stats`
- `argi/device/rk3568-001/rf/event`

这里要区分两件事：

1. 这三类消息一定会先形成 `stdout` JSON 行。
2. 只有在 MQTT 连接存在时，才会额外 publish；这时根对象中的 `mqtt_published` 才会是 `true`。

## 6. 当前没有实现的输入与输出

### 6.1 MQTT command

仓库里没有 MQTT 订阅、回调注册、命令分发、执行反馈路径，因此不能写成已实现。

### 6.2 GPIO

仓库里没有 GPIO 设备驱动接入、poll 逻辑或上层事件适配，因此不能写成已实现。

### 6.3 事件录像

当前看得到的是 Vision 本地运行时和 MQTT publish 路径，但没有一条可证明的“事件触发录像并回发 `record_done`”闭环，所以不能把事件录像写成已完成功能。

## 7. Vision 在这份文档里的边界

Vision 是当前工程的另一条主线，但不应把它和 RF 混为一谈。这里只保留三条边界信息：

- Vision 运行时在 Qt 进程内
- 它最终也把结果汇入 `DashboardBackend`
- RTSP 相关细节由其他文档负责，不在本文展开

## 8. 推荐阅读顺序

如果你的目标是彻底读通 RF 主链，建议顺序如下：

1. 本文，先把边界立住
2. [project2_shared_protocol_deep_dive.md](project2_shared_protocol_deep_dive.md)，先看 pulse frame ABI
3. [project2_master_driver_deep_dive.md](project2_master_driver_deep_dive.md)，再看 `/dev/rf433` 怎么长出来
4. [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md)，最后看 `rf_gateway` 如何解码、发 JSON、接到 Qt
