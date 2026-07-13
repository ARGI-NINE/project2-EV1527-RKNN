# project2_master RF 主链函数索引

这份索引只服务于当前 RF 主链走读。它不重复讲完整实现，而是告诉你：

- 这个函数在哪一层
- 它解决什么问题
- 看完它应该跳回哪篇深读文档

## 1. 入口与主链

| 函数 / 模块 | 文件 | 作用 | 继续阅读 |
| --- | --- | --- | --- |
| `rf_proto_encode()` | `common/rf_protocol.c` | 把 `rf_frame_t` 编成 `AA55/LEN/PAYLOAD/CRC` 字节流 | `project2_shared_protocol_deep_dive.md` |
| `rf433_receive_buf()` | `linux_driver/rf433_drv.c` | serdev 收到 UART 字节后逐字节喂给驱动 parser | `project2_master_driver_deep_dive.md` |
| `main()` | `linux_app/main.c` | 组装策略参数、设备入口和 epoll 事件泵 | `project2_master_userland_deep_dive.md` |
| `RFGatewayClient::handleProtocolLine()` | `qt_gui/rf/rf_gateway_client.cpp` | 把 `rf_gateway` 的 JSON stdout envelope 还原成 Qt 后端状态 | `project2_master_qt_vision_deep_dive.md` |

## 2. `common/rf_protocol.c`

这部分对应共享 pulse-frame ABI 的最小边界。

### 共享协议层

### 核心结构

| 符号 | 文件 | 作用 | 继续阅读 |
| --- | --- | --- | --- |
| `rf_frame_t` | `common/rf_protocol.h` | 共享脉冲帧结构，只包含 `pulse[]` 和 `len` | `project2_shared_protocol_deep_dive.md` |
| `RF_PROTO_SYNC0/SYNC1` | `common/rf_protocol.h` | 共享同步头常量 | `project2_shared_protocol_deep_dive.md` |
| `RF_BUFFER_SIZE` | `common/rf_protocol.h` | 单帧脉冲上限 | `project2_shared_protocol_deep_dive.md` |

### 核心函数

| 函数 | 文件 | 作用 | 继续阅读 |
| --- | --- | --- | --- |
| `rf_proto_crc8()` | `common/rf_protocol.c` | 对 `LEN + PAYLOAD` 做 XOR CRC | `project2_shared_protocol_deep_dive.md` |
| `rf_proto_encode()` | `common/rf_protocol.c` | 把 `rf_frame_t` 编成线上的 `AA55/LEN/PAYLOAD/CRC` | `project2_shared_protocol_deep_dive.md` |

#### 代表性代码摘录：共享协议层的真实边界

代码来源：`project2_master/common/rf_protocol.c::rf_proto_encode()`

```c
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

- 为什么先看这段：它最能代表这一层的风格，只有 pulse 帧编码，不掺任何解码语义。
- 看代码时要注意什么：如果你在别处看到 `addr/key/conf`，那已经不是共享协议层了，应该回到用户态解码层去找。

## 3. `linux_driver/rf433_drv.c`

这部分对应 `/dev/rf433` 从 UART 字节流长出来的内核侧实现。

### Linux 驱动层

### driver 私有对象与 parser

| 符号 | 文件 | 作用 | 继续阅读 |
| --- | --- | --- | --- |
| `struct rf433_priv` | `linux_driver/rf433_drv.c` | 驱动上下文，包含 serdev、parser、kfifo、统计、online 状态 | `project2_master_driver_deep_dive.md` |
| `parser_reset()` | `linux_driver/rf433_drv.c` | parser 回到起始态 | `project2_master_driver_deep_dive.md` |
| `parser_feed_byte()` | `linux_driver/rf433_drv.c` | 逐字节状态机，负责从 UART 字节流成帧 | `project2_master_driver_deep_dive.md` |
| `parser_emit_frame()` | `linux_driver/rf433_drv.c` | 组装 `struct rf433_frame` 并推入队列 | `project2_master_driver_deep_dive.md` |

### serdev 与运行时

| 函数 | 文件 | 作用 | 继续阅读 |
| --- | --- | --- | --- |
| `rf433_receive_buf()` | `linux_driver/rf433_drv.c` | serdev 接收回调，批量把字节喂给 parser | `project2_master_driver_deep_dive.md` |
| `rf433_write_wakeup_nop()` | `linux_driver/rf433_drv.c` | RX-only 场景下的显式空写回调 | `project2_master_driver_deep_dive.md` |
| `rf433_online_timer_fn()` | `linux_driver/rf433_drv.c` | 周期检查 `online` 与半帧超时 | `project2_master_driver_deep_dive.md` |
| `rf433_probe()` | `linux_driver/rf433_drv.c` | 初始化 serdev、misc、kfifo、timer | `project2_master_driver_deep_dive.md` |
| `rf433_remove()` | `linux_driver/rf433_drv.c` | 卸载时释放资源 | `project2_master_driver_deep_dive.md` |

### 用户态 ABI

| 符号 / 函数 | 文件 | 作用 | 继续阅读 |
| --- | --- | --- | --- |
| `struct rf433_frame` | `linux_driver/rf433_ioctl.h` | 用户态读取的一整帧，附带 `timestamp_ns` 和 `seq` | `project2_master_driver_deep_dive.md` |
| `struct rf433_stats` | `linux_driver/rf433_ioctl.h` | 驱动统计 | `project2_master_driver_deep_dive.md` |
| `struct rf433_status` | `linux_driver/rf433_ioctl.h` | 驱动在线状态与队列状态 | `project2_master_driver_deep_dive.md` |
| `rf433_misc_read()` | `linux_driver/rf433_drv.c` | 向用户态返回一整帧 | `project2_master_driver_deep_dive.md` |
| `rf433_misc_poll()` | `linux_driver/rf433_drv.c` | 让用户态用 epoll 等待新帧 | `project2_master_driver_deep_dive.md` |
| `rf433_misc_ioctl()` | `linux_driver/rf433_drv.c` | 导出 stats/status/flush 控制面 | `project2_master_driver_deep_dive.md` |

#### 代表性代码摘录：驱动层的典型风格是“状态机成帧后入队”

代码来源：`project2_master/linux_driver/rf433_drv.c::parser_emit_frame()` / `parser_feed_byte()`

```c
frame.timestamp_ns = ktime_get_real_ns();
frame.pulse_count  = priv->expected_pulses;
frame.seq          = ++priv->seq;

if (kfifo_is_full(&priv->fifo)) {
    kfifo_skip(&priv->fifo);
    priv->stats.drop_cnt++;
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

- 为什么先看这段：它把 parser、统计、kfifo 和用户态 ABI 的交界放在同一段代码里，是驱动层最典型的入口风格。
- 看代码时要注意什么：驱动交付的是原始 pulse 帧，不做 EV1527 业务解码；队列满时会保留最新实时数据，这会影响 `drop_cnt`。

## 4. `linux_app/main.c`

`rf_gateway` 的进程入口、发布策略和根级 JSON envelope 组装位于 `main.c`；输入事件循环和解码实现分别挂在下面两个兼容小节。

### 用户态网关层

### 5. `linux_app/rf_source.c` 与 `linux_app/rf_epoll.c`

### 输入与事件循环

| 函数 | 文件 | 作用 | 继续阅读 |
| --- | --- | --- | --- |
| `rf_source_is_supported_path()` | `linux_app/rf_source.c` | 只允许 `/dev/rf433` | `project2_master_userland_deep_dive.md` |
| `rf_source_open()` | `linux_app/rf_source.c` | 校验字符设备后以非阻塞模式打开 | `project2_master_userland_deep_dive.md` |
| `rf_source_close()` | `linux_app/rf_source.c` | 关闭设备 | `project2_master_userland_deep_dive.md` |
| `rf_epoll_run()` | `linux_app/rf_epoll.c` | 把 `/dev/rf433` 和 `timerfd` 组织成事件循环 | `project2_master_userland_deep_dive.md` |
| `consume_frames()` | `linux_app/rf_epoll.c` | 连续读取驱动帧并上推回调 | `project2_master_userland_deep_dive.md` |

### 6. `linux_app/rf_decode.c` 与 `linux_app/rf_decode_c.c`

### 解码与统计

| 函数 / 符号 | 文件 | 作用 | 继续阅读 |
| --- | --- | --- | --- |
| `rf_decoded_packet_t` | `linux_app/rf_decode.h` | 用户态统一解码结果结构 | `project2_master_userland_deep_dive.md` |
| `rf_decode_frame()` | `linux_app/rf_decode.c` | pulse frame 到 EV1527 结果的主入口 | `project2_master_userland_deep_dive.md` |
| `rf_decode_get_runtime_stats()` | `linux_app/rf_decode.c` | 解码累计统计 | `project2_master_userland_deep_dive.md` |
| `rf_decode_get_last_call_stats()` | `linux_app/rf_decode.c` | 最近一次解码耗时与状态 | `project2_master_userland_deep_dive.md` |
| `rf_decode_ev1527_c_with_stats()` | `linux_app/rf_decode_c.c` | 当前唯一的 EV1527 识别算法入口 | `project2_master_userland_deep_dive.md` |

### 发布与策略

| 函数 | 文件 | 作用 | 继续阅读 |
| --- | --- | --- | --- |
| `build_protocol_line()` | `linux_app/main.c` | 组装根级 JSON envelope | `project2_master_userland_deep_dive.md` |
| `emit_protocol_message()` | `linux_app/main.c` | 先尝试 MQTT publish，再输出 `stdout` JSON | `project2_master_userland_deep_dive.md` |
| `build_device_status_payload()` | `linux_app/main.c` | 组装 `device_status.payload` | `project2_master_userland_deep_dive.md` |
| `build_rf_stats_payload()` | `linux_app/main.c` | 组装 `rf_stats.payload` | `project2_master_userland_deep_dive.md` |
| `build_rf_event_payload()` | `linux_app/main.c` | 组装 `rf_event.payload`，包含 `pulse_us[]` 和解码结果 | `project2_master_userland_deep_dive.md` |
| `stable_group_find()` / `stable_group_seed()` / `stable_group_update()` | `linux_app/main.c` | 稳定分组与近邻合并 | `project2_master_userland_deep_dive.md` |
| `on_rf_frame()` | `linux_app/main.c` | 用户态处理一帧 RF 的核心回调 | `project2_master_userland_deep_dive.md` |
| `on_drv_stats()` | `linux_app/main.c` | 定时刷新驱动状态并发状态/统计消息 | `project2_master_userland_deep_dive.md` |
| `main()` | `linux_app/main.c` | `rf_gateway` 入口，串起参数、输入、事件循环和退出收尾 | `project2_master_userland_deep_dive.md` |

### MQTT 支撑

| 函数 / 符号 | 文件 | 作用 | 继续阅读 |
| --- | --- | --- | --- |
| `mqtt_publisher_t` | `linux_app/mqtt_publisher.h` | MQTT 发布器上下文 | `project2_master_userland_deep_dive.md` |
| `mqtt_publisher_init()` | `linux_app/mqtt_publisher.c` | 建连并启动循环线程 | `project2_master_userland_deep_dive.md` |
| `mqtt_publisher_publish()` | `linux_app/mqtt_publisher.c` | 向给定子 topic 发布 payload | `project2_master_userland_deep_dive.md` |
| `mqtt_publisher_is_connected()` | `linux_app/mqtt_publisher.c` | 判断当前 MQTT 连接状态 | `project2_master_userland_deep_dive.md` |
| `mqtt_publisher_cleanup()` | `linux_app/mqtt_publisher.c` | 清理连接与资源 | `project2_master_userland_deep_dive.md` |

#### 代表性代码摘录：用户态网关层先做 decode/策略，再统一发 envelope

代码来源：`project2_master/linux_app/main.c::on_rf_frame()` / `emit_protocol_message()`

```c
rc = rf_decode_frame(frame, &pkt);
if (rc != 0) {
    return 0;
}

if (pkt.confidence < ctx->min_publish_confidence) {
    ctx->low_conf_drop++;
    return 0;
}

if (mqtt_publisher_is_connected(&ctx->mqtt)) {
    publish_rc = mqtt_publisher_publish(&ctx->mqtt, subtopic, payload_json, retain);
    mqtt_published = (publish_rc == 0);
}

fprintf(stdout, "%s\n", line);
```

- 为什么先看这段：它同时代表了这一层的两个核心动作，先把 pulse frame 变成业务事件，再把同一份 payload 同步送到 `stdout` 和 MQTT 旁路。
- 看代码时要注意什么：EV1527 解码、置信度阈值、稳定化、去重都发生在这里；`stdout` 不是调试日志，而是 Qt 主链输入。

## 7. `qt_gui` 入口与共享状态

当前 RF 主链虽然以 `qt_gui/rf` 为主，但入口与共享状态至少要记住下面这些符号。

### Qt 消费层

### 8. `qt_gui/rf` 与 `qt_gui/vision`

这一层虽然不在本次“只读 linux_app”的范围内，但和 RF 主链强相关，所以至少要把入口记住。

| 函数 / 类 | 文件 | 作用 | 继续阅读 |
| --- | --- | --- | --- |
| `RFGatewayClient::startGateway()` | `qt_gui/rf/rf_gateway_client.cpp` | 从固定路径启动 `rf_gateway` 子进程 | `project2_iot_design.md`，源码直读 |
| `RFGatewayClient::parseProtocolEnvelope()` | `qt_gui/rf/rf_gateway_client.cpp` | 解析根级 JSON envelope | `project2_iot_design.md`，源码直读 |
| `RFGatewayClient::handleProtocolLine()` | `qt_gui/rf/rf_gateway_client.cpp` | 按 `type` 分发 `rf_event/device_status/rf_stats` | `project2_iot_design.md`，源码直读 |
| `DashboardBackend::addRFEvent()` | `qt_gui/core/dashboard_backend.cpp` | 更新最近事件、波形、历史表 | `project2_iot_design.md`，源码直读 |
| `DashboardBackend::updateProtocolStats()` | `qt_gui/core/dashboard_backend.cpp` | 更新 RF 错误和掉帧计数 | `project2_iot_design.md`，源码直读 |
| `RFStatusPage::refresh()` | `qt_gui/rf/rf_status_page.cpp` | 把后端快照刷到 RF 页面 | `project2_iot_design.md`，源码直读 |

#### 代表性代码摘录：Qt 消费层只解析 envelope 并刷新快照

代码来源：`project2_master/qt_gui/rf/rf_gateway_client.cpp::handleProtocolLine()` / `qt_gui/core/dashboard_backend.cpp::addRFEvent()`

```cpp
if (!parseProtocolEnvelope(line, &type, &topic, &mqttPublished, &payload)) {
    backend_->incrementParseError();
    return;
}

if (type == QStringLiteral("rf_event")) {
    backend_->addRFEvent(event, pulses);
    backend_->addLog("INFO", "RF", line);
}

void DashboardBackend::addRFEvent(const RFEvent &event, const QVector<int> &pulses) {
    hasLastDecode_ = true;
    lastDecode_ = event;
    waveform_ = pulses;
    eventHistory_.prepend(event);
}
```

- 为什么先看这段：它能最快说明 Qt 这层的真实风格是“消费 `stdout` 合同并维护展示快照”，不是自己再做一遍解码。
- 看代码时要注意什么：一旦 `handleProtocolLine()` 看懂了，再查 `RFStatusPage::refresh()` 就只是 UI 投影；如果 payload 字段不对，问题通常不在页面层。

## 9. 回查时先看什么

### 走读时最值得优先回查的结构

| 结构 | 所在层 | 为什么重要 |
| --- | --- | --- |
| `rf_frame_t` | 共享协议 | 这是所有高层语义之前的最小帧模型 |
| `struct rf433_frame` | 驱动 ABI | 这是 `/dev/rf433` 真正交付给用户态的结构 |
| `rf_decoded_packet_t` | 用户态解码 | 这是 `addr/key/conf/source` 的来源 |
| `RFSnapshot` | Qt 后端 | 这是 RF 页面最终展示的数据汇总体 |

#### 代表性代码摘录：最值得回查的两个边界结构

代码来源：`project2_master/linux_driver/rf433_ioctl.h` / `project2_master/linux_app/rf_decode.h`

```c
struct rf433_frame {
    __u64 timestamp_ns;
    __u16 pulse_count;
    __u16 reserved;
    __u32 seq;
    __u16 pulse[RF433_MAX_PULSES];
};

typedef struct {
    char addr[16];
    char key[8];
    float confidence;
    unsigned raw_code;
    char source[16];
} rf_decoded_packet_t;
```

- 为什么先看这段：这两个结构把“驱动交付什么”和“用户态额外生成什么”分得最清楚。
- 看代码时要注意什么：前者还是原始 pulse 世界，后者已经进入业务语义世界；如果边界混了，整条主链都会被误写成错误叙述。
