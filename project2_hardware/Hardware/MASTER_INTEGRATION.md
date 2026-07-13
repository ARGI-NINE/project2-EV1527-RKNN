# Hardware-Master Integration

## 这份文档解决什么问题

这份文档只定义 `project2_hardware` 和 `project2_master` 之间当前代码可确认的接口边界。

重点只有两个：

1. hardware 到底发什么。
2. master 从哪一层开始接手，以及哪些字段不属于 hardware wire ABI。

先把结论写死：

- hardware 是 RF433 事件源前端，不是遥控破解器，不是授权控制器，不是开门控制器。
- hardware 当前输出的是“脉冲帧协议”，不是“业务事件 JSON”，也不是“门禁控制命令”。
- `addr`、`key`、`conf`、`timestamp`、`source`、`seq` 这类字段不属于 hardware 线上 ABI。

## 实时链路分层

按当前仓库代码，实时链路应理解为：

```text
RF front-end pulse
-> STM32 TIM2 capture
-> frame split / idle flush
-> USART1 AA55/LEN/PAYLOAD/XOR uplink
-> RK3568 UART
-> serdev rf433 driver
-> /dev/rf433
-> linux_app/rf_gateway
-> qt_gui
```

这里最容易写错的地方是把 `/dev/rf433` 之后的业务字段倒灌回 hardware。

正确分层是：

- hardware 截止在 UART 脉冲帧。
- master driver 截止在结构化驱动帧。
- master userland 才开始解码业务字段。

## hardware 当前实际输出

### 1. 采集来源

`RF_Capture_Init()` 把 `TIM2_CH1` 配到 `PA0`，输入模式是上拉输入，定时器工作在 `1 MHz` 计数频率。中断里通过翻转捕获极性获取双边沿间隔，所以输出数据本质上是一串连续的微秒脉冲宽度。

### 2. 分帧来源

当前帧边界来自两部分：

- `SyncUs` 阈值驱动的显式分帧
- `Timer3` 1ms 心跳支持的 Idle flush

默认参数来自 `RF_Capture_Init()`：

- `MinPulseUs = 80`
- `MaxPulseUs = 65535`
- `SyncUs = 8000`
- `MinFramePulses = 50`

当前 `main()` 没有调用 `RF_Capture_SetFilter()`，所以默认参数就是当前静态代码的真实口径。

### 3. 输出格式

`RF_Uart_SendFrame()` 最终发送的是 `rf_proto_encode()` 的结果：

摘自 `project2_hardware/Hardware/RF_Uart.c` 与 `project2_hardware/Hardware/RF_Protocol.c`：

```c
uint8_t RF_Uart_SendFrame(const rf_frame_t *Frame) {
    size_t n = 0u;
    if (Frame == NULL) {
        return 0u;
    }
    n = rf_proto_encode(Frame, g_uart_tx_packet, sizeof(g_uart_tx_packet));
    if (n > 0u && n <= 65535u) {
        return RF_Uart_SendArray(g_uart_tx_packet, (uint16_t)n);
    }
    return 0u;
}
```

```c
out[0] = RF_PROTO_SYNC0;
out[1] = RF_PROTO_SYNC1;
out[2] = (uint8_t)(frame->len & 0xFFu);
out[3] = (uint8_t)((frame->len >> 8u) & 0xFFu);
```

```text
AA 55 LEN_LO LEN_HI PAYLOAD[len*2] XOR
```

字段语义如下：

| 字段 | 宽度 | 含义 |
| --- | --- | --- |
| `AA 55` | 2 字节 | 帧同步头 |
| `LEN` | 2 字节，小端 | 本帧脉冲数量，不是 payload 字节数 |
| `PAYLOAD` | `LEN * 2` 字节 | 每个脉冲一个小端 `uint16_t`，单位是微秒 |
| `XOR` | 1 字节 | 从 `LEN_LO` 到 payload 末尾的逐字节 XOR |

这就是当前 hardware 的 wire ABI 全部内容。

这两段代码把边界讲得很死：hardware 先把 `rf_frame_t` 编成 `AA55/LEN/PAYLOAD/XOR`，再入 UART TX 队列。也就是说，master 对接的第一层事实永远是“脉冲帧字节流”，不是业务 JSON。

## 这条 ABI 明确不包含什么

下面这些字段都不是硬件发到 UART 上的字段：

- `addr`
- `key`
- `conf`
- `confidence`
- `source`
- `seq`
- `drv_seq`
- `timestamp_ns`
- `decode_us`
- `mqtt_connected`

原因不是“文档约定上不想发”，而是当前代码路径里根本没有这些内容的生成点。

## 这些字段后来是在哪一层出现的

静态读仓库代码，可以把边界拆得很清楚：

| 层级 | 输入 | 输出 | 新增了什么 |
| --- | --- | --- | --- |
| hardware `RF_Capture` | RF 边沿 | `rf_frame_t` | 只得到脉冲数组 |
| hardware `RF_Uart` | `rf_frame_t` | `AA55/LEN/PAYLOAD/XOR` | 只做封包 |
| master `rf433_drv` | UART 字节流 | `struct rf433_frame` / `/dev/rf433` | 新增驱动侧 `timestamp_ns`、`seq`、在线状态和统计 |
| master `rf_gateway` | `rf_frame_t` | `rf_event` JSON payload | 新增 `addr`、`key`、`conf`、`source`、`decode_us` 等业务字段 |
| Qt | `rf_event` | UI 状态和波形 | 只做展示，不改 hardware ABI |

这张表就是 hardware 与 master 的边界描述。

驱动层和用户态层新增字段的代码入口也很直接。

摘自 `project2_master/linux_driver/rf433_drv.c`：

```c
memset(&frame, 0, sizeof(frame));
frame.timestamp_ns = ktime_get_real_ns();
frame.pulse_count  = priv->expected_pulses;
frame.seq          = ++priv->seq;

for (i = 0; i < priv->expected_pulses; i++) {
    u16 lo = priv->payload_buf[i * 2];
    u16 hi = priv->payload_buf[i * 2 + 1];
    frame.pulse[i] = lo | (hi << 8);
}
```

这段说明 `/dev/rf433` 之前的 master driver 层会新增 `timestamp_ns` 和驱动侧 `seq`，但 `pulse[]` 仍然是从 UART payload 原样重组出来的。

摘自 `project2_master/linux_app/main.c`：

```c
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
```

这段则说明 `addr`、`key`、`conf`、`source`、`decode_us` 和最终 JSON envelope 都是在 `rf_gateway` 用户态里生成的，所以它们天然属于 master 后级，不属于 hardware wire ABI。

可以换一种更直接的话说：

- hardware 只承诺“这是一帧脉冲”。
- master driver 承诺“我把这帧脉冲可靠地还原并排队了”。
- master userland 才承诺“我把这帧脉冲解释成某个业务事件了”。

## master 侧与当前 ABI 对齐的代码事实

当前仓库里能静态确认这些对齐点：

- `project2_master/common/rf_protocol.h` 与 hardware 侧共享同样的 `RF_PROTO_SYNC0`、`RF_PROTO_SYNC1` 和 `rf_frame_t` 基本定义。
- `project2_master/linux_driver/rf433_drv.c` 的 parser 也是按 `AA55 + LEN + PAYLOAD + XOR` 状态机实现。
- 驱动把 payload 里的两个字节重新拼成 `u16 pulse[i]`，再写入 `/dev/rf433` 输出帧。
- `project2_master/linux_app/rf_epoll.c` 会把驱动帧搬成用户态 `rf_frame_t`。
- `project2_master/linux_app/main.c` 输出 `rf_event` 时，`pulse_us[]` 直接来自这帧 `rf_frame_t.pulse[]`。

这说明 master UI 里看到的波形，语义上仍然是 hardware 上传的真实脉冲数组，而不是 UI 自己反推的假数据。

## UART 参数与接线边界

当前硬件串口口径来自 `RF_Uart_Init()`：

- 外设：`USART1`
- 波特率：`9600`
- 数据位：`8`
- 校验位：`None`
- 停止位：`1`
- 流控：`None`
- TX：`PA9`
- RX：`PA10`

如果做板级联调，至少要保证：

- 双方电平是同一套逻辑电平
- `PA9 -> 对端 RX`
- `PA10 <- 对端 TX`
- `GND` 共地

这只是静态代码对接口的要求，不代表仓库已经完成任意板卡上的实机连通性验收。

## Timer3 心跳在集成里的真实作用

`system/Timer.c` 里的 `TIM3` 不是第二个 RF 采样器，它在当前工程中的职责很单一：

- 每 `1 ms` 触发一次中断
- 调 `RF_Capture_Tick1msHandler()` 累加 `MsTick`
- 让 `RF_Capture_ProcessLoop()` 能做 Idle flush 判定

所以 master 不应该把 Timer3 理解成“第二时间戳源”或“业务时钟协议”。它只是 hardware 内部用来收尾分帧的心跳。

## IRQ bridge 是嵌入式集成边界，不是 master 边界

`RF_Capture.c` 默认导出：

```text
TIM2_IRQHandler() -> RF_Capture_TIM2_IRQHandler()
```

如果你的 STM32 工程已经在别处统一定义了 `TIM2_IRQHandler`，需要在编译期定义：

- `RF_CAPTURE_DISABLE_TIM2_IRQ_BRIDGE`

然后由外部 ISR 手动调用 `RF_Capture_TIM2_IRQHandler()`。

这个问题发生在 hardware 工程内部集成阶段，不发生在 RK3568 master 侧，但它足够常见，应该在对接文档里说清楚。

## 当前未使用 RX 路怎么理解

`RF_Uart_Init()` 打开了 `USART1 RXNE` 中断，`USART1_IRQHandler()` 也会把最近一个收到的字节保存到：

- `g_uart_rx_data`
- `g_uart_rx_flag`

同时模块导出了：

- `RF_Uart_GetRxData()`
- `RF_Uart_GetRxFlag()`

但当前整个 hardware 工程没有任何代码去消费这两个接口，也没有任何“收到上位机命令后修改行为”的逻辑。

因此：

- 当前 ABI 只有上行数据面，没有可用的下行控制面。
- master 不能假设自己今天已经可以通过 UART 命令 hardware 调参、回放、授权或执行动作。
- 如果以后要加下行协议，那会是一次新的 ABI 扩展，不是当前文档已经覆盖的能力。

## 不要把这些责任写错层

### hardware 的边界

- 负责采样
- 负责分帧
- 负责 Idle flush
- 负责把脉冲数组编码成 UART 帧

### master 的边界

- 负责接收 UART 字节流
- 负责在驱动里还原协议帧
- 负责生成驱动时间戳和序号
- 负责在用户态解码出业务事件
- 负责把业务事件展示给 UI 或继续发布

### 明确不是 hardware 边界的内容

- 地址语义
- 按键语义
- 置信度语义
- 事件时间戳语义
- MQTT 语义
- 门禁授权语义
- 开门控制语义

只要看到这些字段，就应该默认它们属于 master 后级，而不是 hardware wire ABI。

## 静态代码可确认的验证边界

这份文档只基于仓库代码，可以确认的是：

- hardware 确实实现了 `TIM2` 输入捕获。
- hardware 确实实现了分帧和 Idle flush。
- hardware 确实实现了 `AA55/LEN/PAYLOAD/XOR` UART 输出。
- master 驱动和用户态代码确实按同一层级继续消费这批脉冲帧。

不能确认的是：

- 任何具体板卡上的实际接线已经完成。
- 端到端串口链路已经实机跑通。
- 长时间运行没有丢帧或波形质量问题。
- 这套链路可用于授权控制或门禁替代。

如果未来做了实机联调，应另写验收记录，不要把验收结论提前写进这里。

## 接手者最容易踩的坑

1. 把 `LEN` 误解成 payload 字节数，而不是脉冲数量。
2. 把 `pulse_us[]` 当成 master 自己生成的数据，而不是 hardware 原始上传结果。
3. 把 `addr/key/conf/timestamp` 写进 hardware 协议定义。
4. 看到 `USART1 RX` 已初始化，就误以为已经存在 master -> hardware 控制协议。
5. 忽略 `RF_CAPTURE_DISABLE_TIM2_IRQ_BRIDGE`，导致 `TIM2_IRQHandler` 冲突。

如果你先把这五个坑避开，这条链的边界基本就不会再写错。
