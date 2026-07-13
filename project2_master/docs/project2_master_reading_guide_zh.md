# project2_master 阅读指南

这份指南的目标很简单：帮你用最短路径把当前 master 里的 RF 主链读通，同时避免误把 Vision、MQTT 规划项和 RF 现状混到一起。

## 阅读顺序

兼容入口保留在这里：若只读 RF 主链，直接跳下方“2. 最推荐的阅读顺序”；若只关心 Qt RF 页面，再看“3. 如果你只关心 Qt RF 页面是怎么来的”。

## 每份文档负责什么

各文档的职责映射见下方“4. 每份文档各自回答什么问题”；这里保留旧目录骨架，方便从旧链接回到新结构。

## 建议读法

先立共享协议与 `/dev/rf433` 边界，再读 `rf_gateway` 的 `stdout JSON envelope`，最后回查 Qt 消费层与 Vision 在整套材料里的位置。

## 这套文档怎么写

当前指南继续按“目的 -> 代码锚点 -> 误读提醒”的方式组织，不把规划接口写成现状。

## 最后一遍强调

Qt 是 RF 主链的消费者，不是生产者；共享协议只到 pulse frame，`addr/key/conf` 属于用户态解码结果。

## 1. 如果你只想读通 RF 主链

请先把下面这句话背下来：

```text
shared pulse frame
  -> serdev driver
  -> /dev/rf433
  -> rf_gateway
  -> stdout JSON envelope
  -> Qt RF page
```

这条链上的每一层都解决不同问题：

- 共享协议：脉冲帧长什么样
- 驱动：怎样从 UART 字节流长出 `/dev/rf433`
- userland：怎样解码、去重、发 JSON、旁路 MQTT
- Qt：怎样消费 JSON 刷新页面

## 2. 最推荐的阅读顺序

### 第一步：先立边界

先读：

- [project2_iot_design.md](project2_iot_design.md)

目的：

- 知道什么是已实现
- 知道什么只是规划
- 知道 RF 与 Vision 虽然都汇入 Qt，但接入方式不同

### 第二步：再看共享协议

再读：

- [project2_shared_protocol_deep_dive.md](project2_shared_protocol_deep_dive.md)

目的：

- 搞清楚 pulse frame 的最小 ABI
- 牢牢记住共享协议只到 pulse frame 为止
- 不再把 `addr/key/conf` 误认为共享字段

#### 代码锚点：共享协议只到 pulse frame

代码来源：`project2_master/common/rf_protocol.c::rf_proto_encode()`

```c
if (frame->len == 0u || frame->len > RF_BUFFER_SIZE) {
    return 0u;
}

bytes = (uint16_t)(frame->len * 2u);
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
```

- 为什么先看这段：它把共享协议边界写得最死，线上只有 `AA55 + LEN + pulse[] + CRC`，没有任何 `addr/key/conf` 业务字段。
- 看代码时要注意什么：`frame->len` 是脉冲个数不是字节数；CRC 覆盖 `LEN + PAYLOAD`；只要先读懂这里，后面就不容易把解码结果误读成协议层。

### 第三步：看 `/dev/rf433` 怎么出现

再读：

- [project2_master_driver_deep_dive.md](project2_master_driver_deep_dive.md)

目的：

- 读懂 serdev parser
- 读懂 `read/poll/ioctl`
- 读懂驱动的 `online` 是如何定义的

#### 代码锚点：字节流怎样落成 `/dev/rf433` 里的帧

代码来源：`project2_master/linux_driver/rf433_drv.c::parser_feed_byte()` / `parser_emit_frame()`

```c
frame.timestamp_ns = ktime_get_real_ns();
frame.pulse_count  = priv->expected_pulses;
frame.seq          = ++priv->seq;

for (i = 0; i < priv->expected_pulses; i++) {
    u16 lo = priv->payload_buf[i * 2];
    u16 hi = priv->payload_buf[i * 2 + 1];
    frame.pulse[i] = lo | (hi << 8);
}

case RF_ST_CRC:
    if (byte != priv->crc_accum) {
        priv->stats.crc_err++;
        parser_reset(priv);
    } else {
        parser_emit_frame(priv);
        parser_reset(priv);
    }
    break;
```

- 为什么先看这段：它把 “UART 字节流 -> parser 状态机 -> 带 `timestamp_ns/seq` 的整帧” 这个关键跳变直接摆出来了。
- 看代码时要注意什么：驱动只有 CRC 成功才交付帧；`pulse_count` 来自长度字段，不是上层算法算出来的；队列满时驱动会丢最老帧保最新帧。

### 第四步：看 `rf_gateway` 如何把帧变成 JSON

再读：

- [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md)

目的：

- 读懂 `/dev/rf433` 如何进入 epoll 循环
- 读懂 EV1527 解码发生在用户态
- 读懂 `stdout JSON envelope` 与 MQTT publish 的关系

#### 代码锚点：`stdout JSON envelope` 和 MQTT 是同一份 payload 的两条出口

代码来源：`project2_master/linux_app/main.c::emit_protocol_message()`

```c
if (mqtt_publisher_is_connected(&ctx->mqtt)) {
    publish_rc = mqtt_publisher_publish(&ctx->mqtt, subtopic, payload_json, retain);
    mqtt_published = (publish_rc == 0);
}

if (build_protocol_line(line, sizeof(line), type, subtopic, mqtt_published, payload_json) != 0) {
    fprintf(stderr, "[RF_JSON] failed to assemble protocol line for %s\n", type);
    return -1;
}

fprintf(stdout, "%s\n", line);
```

- 为什么先看这段：它最直接说明 master 当前对 Qt 的主输出其实是 `stdout` envelope，而 MQTT 只是旁路 publish。
- 看代码时要注意什么：`mqtt_published` 是结果标记，不是开关；即使 MQTT 没连上，只要 envelope 组装成功，Qt 这条主链仍然能跑。

#### 代码锚点：解码、稳定化、去重都发生在用户态

代码来源：`project2_master/linux_app/main.c::on_rf_frame()`

```c
rc = rf_decode_frame(frame, &pkt);
if (rc != 0) {
    if (rc == RF_DECODE_RC_NO_FRAME) {
        ctx->decode_no_frame++;
    } else {
        ctx->decode_err++;
    }
    return 0;
}

if (pkt.confidence < ctx->min_publish_confidence) {
    ctx->low_conf_drop++;
    return 0;
}

if (build_rf_event_payload(payload, sizeof(payload), ctx, frame, &pkt, &call_stats, timestamp_ns, drv_seq) != 0) {
    fprintf(stderr, "[RF_JSON] failed to assemble rf event payload\n");
    return 0;
}

(void)emit_protocol_message(ctx, "rf_event", MQTT_TOPIC_RF_EVENT, payload, 0);
```

- 为什么先看这段：它是用户态主链的最短入口，能最快确认 EV1527 解码、置信度阈值、发布策略都不在驱动里。
- 看代码时要注意什么：低置信度、稳定化不足、重复码都会在这里被丢掉；Qt 看到的是策略后的事件，不是驱动抓到的每一帧。

### 第五步：最后回查符号

最后配合：

- [project2_master_function_index.md](project2_master_function_index.md)

目的：

- 找函数入口更快
- 知道当前看到的函数属于哪一层
- 知道应该回到哪篇深读继续看

## 3. 如果你只关心 Qt RF 页面是怎么来的

那就按这个最短路径：

1. 读 `project2_master_userland_deep_dive.md` 里关于 `stdout JSON envelope` 的章节
2. 读 `qt_gui/rf/rf_gateway_client.cpp`
3. 读 `qt_gui/core/dashboard_backend.cpp`
4. 读 `qt_gui/rf/rf_status_page.cpp`

你应该得到的结论是：

- Qt 不直接读 `/dev/rf433`
- Qt 通过 `RFGatewayClient` 拉起 `rf_gateway`
- RF 页面由 `rf_event/device_status/rf_stats` 三类 JSON 消息共同驱动
- MQTT 发布日志会进入系统日志页，但 RF 页面本身不依赖 MQTT 订阅

#### 代码锚点：Qt 只消费 envelope，然后把事件投给 backend

代码来源：`project2_master/qt_gui/rf/rf_gateway_client.cpp::handleProtocolLine()`

```cpp
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

    backend_->addRFEvent(event, pulses);
    backend_->addLog("INFO", "RF", line);
}
```

- 为什么先看这段：它把 Qt 在主链里的职责钉死成“解析 envelope、拆 payload、交给 backend”，不会再误读成设备直连层。
- 看代码时要注意什么：Qt 不参与 `/dev/rf433`、EV1527 解码和 MQTT command；`mqttPublished` 只影响日志侧观感，不决定 RF 页是否刷新。

## 4. 每份文档各自回答什么问题

| 文档 | 回答的问题 |
| --- | --- |
| `project2_iot_design.md` | 这个目录当前到底是什么系统，哪些功能已实现，哪些没有 |
| `project2_shared_protocol_deep_dive.md` | pulse frame ABI 到底长什么样，边界停在哪里 |
| `project2_master_driver_deep_dive.md` | `/dev/rf433` 如何从 serdev + parser + kfifo 演化出来 |
| `project2_master_userland_deep_dive.md` | `rf_gateway` 如何解码、做策略、输出 JSON、旁路 MQTT |
| `project2_master_function_index.md` | 函数和模块位于哪里，分别属于哪一层 |

## 5. 阅读时最容易犯的三个错误

### 错误一：把共享协议读成解码协议

正确说法：

- 共享协议只有脉冲帧
- `addr/key/conf` 属于上层解码结果

### 错误二：把 Qt 页面读成 MQTT 客户端

正确说法：

- Qt RF 页面主输入是 `rf_gateway stdout`
- MQTT 只是同一份 payload 的旁路输出

### 错误三：把规划接口写成现状

当前不能写成已实现的有：

- MQTT command
- GPIO
- 事件录像闭环

## 6. Vision 在这个阅读指南里的位置

当前仓库确实有 Vision 主线，但这份指南不展开 RTSP 和视觉细节。你只需要记住：

- Vision 是另一条板侧运行时链
- 它在 Qt 进程内运行
- 它和 RF 最终都汇入 `DashboardBackend`

如果这次目标是 RF，就不要让 Vision 细节打断主链阅读。
