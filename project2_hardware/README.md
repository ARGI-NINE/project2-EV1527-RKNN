# project2_hardware

## 这是什么

`project2_hardware` 是 `project2` 里的 STM32 前端固件目录。按当前代码，它的职责只有三件事：

1. 用 `TIM2` 输入捕获记录 RF 接收模块输出的边沿间隔。
2. 在 MCU 侧做最小限度的前端筛选、分帧和空闲收尾。
3. 把一帧脉冲宽度数组编码成 UART 字节流，经 `USART1` 发给上位链路。

它的系统定位是“RF433 事件源前端”，不是遥控破解器，不是授权控制器，不是开门控制器，也不是“学习 / 克隆遥控器”的替代品。

这份文档只写静态代码里能确认的事实，不写任何未完成的实机验收结论。

## 先看主路径

当前 `main()` 很短，运行路径也很明确：

1. `RF_Capture_Init()`
2. `RF_Uart_Init()`
3. `Timer_Init()`
4. `while (1) { RF_Capture_ProcessLoop(); }`

摘自 `User/main.c`：

```c
int main(void) {
    RF_Capture_Init();
    RF_Uart_Init();
    Timer_Init();

    while (1) {
        RF_Capture_ProcessLoop();
    }
}
```

这说明当前固件没有 RTOS，没有任务调度，没有命令处理线程。主职责就是持续采集、分帧、上送。

## 它不做什么

- 不在 STM32 上做最终业务解码。
- 不在 STM32 上生成 `addr`、`key`、`conf`、`timestamp` 这类业务字段。
- 不在 STM32 上做门禁授权、继电器控制、开门动作。
- 不把 `USART1 RX` 当成已落地的控制面协议使用。
- 不在当前运行路径里启用 RF 发射或回放。

仓库里虽然有 `Hardware/RF_Tx.c`，但当前 `main()` 没有调用 `RF_Tx_Init()`、`RF_Tx_Replay()` 或 `RF_Tx_ReplayFrame()`。从代码调用关系看，当前运行态是单向上送链路，不是收发双向控制链路。

## 目录怎么读

| 文件 | 作用 | 读它要解决的问题 |
| --- | --- | --- |
| `User/main.c` | 固件入口 | 先确认运行时主路径到底有哪些模块 |
| `Hardware/RF_Capture.c` | TIM2 采集、分帧、Idle flush、队列 | 这份固件最核心的实时逻辑在哪里 |
| `Hardware/RF_Protocol.c` | `AA55/LEN/PAYLOAD/XOR` 封包与 parser helper | 串口线上字节布局是什么 |
| `Hardware/RF_Uart.c` | `USART1` 初始化、TX FIFO、发送中断、占位 RX | 帧怎样从内存发到串口 |
| `system/Timer.c` | `TIM3` 1ms 心跳 | Idle flush 依赖的时基从哪里来 |
| `Hardware/MASTER_INTEGRATION.md` | 与 master 的接口文档 | 硬件输出到哪一层结束，哪些字段不属于硬件 ABI |

如果你是第一次接手，建议阅读顺序就是上表顺序。

## 当前代码确认的硬件采集链

### 1. TIM2 输入捕获

`RF_Capture_Init()` 把 `TIM2` 配成 RF 前端采集定时器：

- 输入脚：`PA0`
- 通道：`TIM2_CH1`
- GPIO 模式：上拉输入 `GPIO_Mode_IPU`
- 定时器预分频：`71`
- 计数频率：`72 MHz / 72 = 1 MHz`
- 计数粒度：`1 us`
- 自动重装值：`0xFFFF`
- 输入捕获滤波：`0xF`
- 初始捕获极性：上升沿

这组配置说明当前实现直接把“边沿间隔”量化成微秒级脉冲宽度，不先转成比特流，也不先做 EV1527 级别语义判断。

摘自 `Hardware/RF_Capture.c`：

```c
memset(&g_rf_capture, 0, sizeof(g_rf_capture));
g_rf_capture.TimerHz = 1000000u;
g_rf_capture.TimerPeriod = 0xFFFFu;
g_rf_capture.MinPulseUs = RF_CAPTURE_DEFAULT_MIN_PULSE_US;
g_rf_capture.MaxPulseUs = RF_CAPTURE_DEFAULT_MAX_PULSE_US;
g_rf_capture.SyncUs = RF_CAPTURE_DEFAULT_SYNC_US;
g_rf_capture.MinFramePulses = RF_CAPTURE_DEFAULT_MIN_FRAME_PULSES;
g_rf_capture.IdleFlushMs = RF_Capture_ComputeIdleFlushMs(g_rf_capture.SyncUs);

TIM_TimeBaseInitStructure.TIM_Prescaler = 71u;
TIM_TimeBaseInitStructure.TIM_Period = g_rf_capture.TimerPeriod;
TIM_ICInitStructure.TIM_Channel = TIM_Channel_1;
TIM_ICInitStructure.TIM_ICPolarity = TIM_ICPolarity_Rising;
TIM_ICInitStructure.TIM_ICFilter = RF_CAPTURE_TIM_IC_FILTER;
```

这里把默认过滤参数、`1 MHz` 计时基准和 `TIM2_CH1` 捕获极性都写死在初始化里，所以 README 里关于 `MinPulseUs` / `SyncUs` / `IdleFlushMs` 的描述不是推测，而是直接来自这段初始化代码。

### 2. TIM2 IRQ 里怎样拿到双边沿间隔

`RF_Capture_TIM2_IRQHandler()` 的关键点有两个：

- 先读 `CCR1`，再结合 `TIM2` 溢出计数拼成扩展时间戳。
- 每处理完一次捕获，就翻转 `TIM2->CCER` 里的 `CC1P`，这样下次会捕获相反极性的边沿。

这意味着代码不是只看上升沿，也不是只看下降沿，而是靠“每次中断后翻转捕获极性”来交替抓取双边沿，从而得到连续的高低电平宽度序列。

### 3. 溢出扩展不是可有可无的细节

因为 `TIM2` 周期只有 `65536 us`，单靠 16 位计数器不够覆盖更长的边沿间隔，所以代码维护了 `Tim2OverflowCount`。

IRQ 里的处理做了两层补偿：

- `TIM_IT_Update` 负责在每次计数器回卷时给 `Tim2OverflowCount` 加一。
- 如果捕获发生时 `UIF` 已经挂起，且 `CCR <= 当前 CNT`，代码会把这次捕获归到“下一轮计数周期”。

这部分的注释写得很直接：它的目的就是避免长间隔在边界情况下被误算成短脉冲。

### 4. 采样值先过前端过滤

`RF_Capture_DetectPulse()` 不是把所有间隔都原样收下。默认过滤参数在 `RF_Capture_Init()` 里初始化：

- `MinPulseUs = 80`
- `MaxPulseUs = 65535`
- `SyncUs = 8000`
- `MinFramePulses = 50`

如果外部代码不主动调用 `RF_Capture_SetFilter()`，当前运行时就一直使用这组默认值。当前 `main()` 没有调用 `RF_Capture_SetFilter()`，所以静态代码上可以确认默认参数生效。

### 5. 分帧规则

当前固件的“帧”不是按业务码定义的，而是按脉冲流边界定义的。现有代码里有三层约束：

1. 只有 `80 us <= pulse <= 65535 us` 的间隔才会被纳入考虑。
2. 当新脉冲 `pulse > SyncUs` 且当前缓冲长度已经达到 `MinFramePulses` 时，认为可以把已有脉冲作为一帧冲刷出去。
3. 一帧最终入队前，还要通过 “first low is longest” 的启发式过滤。

最后这一条很容易忽略。`RF_Capture_FrameFirstLowIsLongest()` 会检查两种相位起点，只要有一种满足“首个 low 脉冲不短于后续同相位 low 脉冲”，这帧才会入 `ReadyQueue`。不满足的帧会被直接丢掉。

### 6. Sync 分帧里的 carry 逻辑

`RF_Capture_DetectPulse()` 里还有一段值得单独说明的逻辑：当遇到 `SyncUs` 以上的大间隔并准备 flush 当前帧时，代码可能把旧帧最后一个“不是 sync 的尾脉冲”拿出来，作为下一帧的第一个脉冲继续保留。

代码条件是：

- 当前帧长度大于 `MinFramePulses`
- 当前帧最后一个脉冲 `<= SyncUs`

满足时会先把这个尾脉冲从旧帧移除，flush 旧帧，再把它塞回新的 `CurrentFrame` 开头。

这不是通用 RF 理论文档里的必备步骤，而是当前实现的一个具体分帧细节。后续如果你改 framing，一定要先看懂这段，否则很容易把边界脉冲吃掉或重复记账。

### 7. ReadyQueue 是 ISR 到主循环的单生产者/单消费者桥

`RF_Capture.c` 内部维护了一个深度为 `4` 的 `ReadyQueue`：

- 生产者：`TIM2` ISR
- 消费者：主循环里的 `RF_Capture_ProcessLoop()`

设计意图很明确：

- ISR 里尽量只做采样、最小处理和入队。
- 真实 UART 发送尽量放到主循环完成。

如果队列满了，`DroppedFrames` 会加一，但当前模块没有导出 getter，所以这个计数目前只存在于内部状态里，不会自动上报给 master。

## Idle flush 为什么依赖 TIM3

只靠 `SyncUs` 并不能覆盖所有收尾场景，所以代码还做了“空闲收尾”。

### 1. TIM3 提供 1ms 心跳

`Timer_Init()` 把 `TIM3` 配成 1 kHz 更新中断：

- 预分频仍然是 `72 - 1`
- 定时器计数频率仍然是 `1 MHz`
- 周期是 `1000 us`
- 每次 `TIM3_IRQHandler()` 触发时调用 `RF_Capture_Tick1msHandler()`

`RF_Capture_Tick1msHandler()` 很简单，只做 `MsTick++`。也就是说，`TIM3` 在当前工程里的主要职责不是参与 RF 捕获，而是给主循环提供一个 1ms 级别的空闲判定时基。

### 2. Idle flush 阈值怎么算

`IdleFlushMs` 由 `SyncUs` 推导：

`idle_ms = sync_us / 1000 + 8`

然后再做夹紧：

- 最小 `12 ms`
- 最大 `100 ms`

按默认 `SyncUs = 8000` 计算，当前默认 `IdleFlushMs = 16 ms`。

### 3. 主循环怎样做空闲收尾

`RF_Capture_ProcessLoop()` 会先看：

- 当前 `CurrentFrame.len` 是否已经达到 `MinFramePulses`
- `now_ms - LastEdgeMs` 是否超过 `IdleFlushMs`

满足后，它会先复制一份工作帧，再在一个很短的关中断临界区里确认：

- 当前长度没变
- 最后边沿时间没变

如果快照仍然成立，就把 `CurrentFrame` 清空，然后立刻尝试通过 UART 发送这帧。

这说明 idle flush 是“主循环补收尾”，不是“TIM3 直接发帧”。`TIM3` 只提供时间基，真正发帧仍然发生在主循环。

## UART 上送协议是什么

`RF_Uart_SendFrame()` 会先调用 `rf_proto_encode()`，把 `rf_frame_t` 转成固定线协议：

摘自 `Hardware/RF_Uart.c` 与 `Hardware/RF_Protocol.c`：

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
SYNC0  SYNC1  LEN0  LEN1  PAYLOAD[0..len*2-1]  XOR
0xAA   0x55   LE16 pulse count  LE16 pulse_us[]  XOR over LEN+PAYLOAD
```

更具体地说：

- `SYNC0 = 0xAA`
- `SYNC1 = 0x55`
- `LEN` 是脉冲个数，不是字节数
- `PAYLOAD` 里每个脉冲是一个小端 `uint16_t`
- 尾字节是从 `LEN0` 开始到 payload 末尾的逐字节 XOR

这就是当前 hardware 对外的 wire ABI。它只描述“脉冲帧怎么上送”，不描述“业务事件怎么命名”。

也就是说，真正发上 UART 的并不是某个抽象“RF 事件”，而是 `rf_frame_t -> rf_proto_encode() -> TX FIFO` 这一条非常具体的编码路径。后面的 `addr` / `key` / `conf` 都不在这里出现。

## UART 模块当前的真实状态

### 1. TX 路是当前唯一有效业务路径

`RF_Uart.c` 当前做了完整的发送路径：

- 外设：`USART1`
- 波特率：`9600`
- 格式：`8N1`
- TX 引脚：`PA9`
- RX 引脚：`PA10`
- 硬件流控：无
- 发送队列：`2304` 字节环形 FIFO
- 发送机制：写 FIFO 后打开 `USART_IT_TXE`，由 `USART1_IRQHandler()` 持续搬运

`RF_Uart_SendFrame()` 返回值不是“是否已经发完”，而是“是否成功进入 TX FIFO”。如果 FIFO 空间不足，当前帧会被拒绝，内部 `g_uart_tx_drop_frames` 自增。

### 2. RX 路已经初始化，但当前没有形成可用控制协议

`USART1` 确实打开了 `RXNE` 中断，也提供了：

- `RF_Uart_GetRxData()`
- `RF_Uart_GetRxFlag()`

但当前工程里没有任何调用者消费这两个接口，也没有任何逻辑根据收到的串口字节去改过滤参数、控制发射或回传状态。

所以应当把 `USART1 RX` 理解成“已留出占位，但当前未接入业务链路”，而不是“已经存在 master -> hardware 控制面协议”。

## IRQ bridge 怎么集成

`RF_Capture.c` 默认会导出：

```text
TIM2_IRQHandler() -> RF_Capture_TIM2_IRQHandler()
```

如果你的工程已经在别处统一管理 `TIM2_IRQHandler`，当前代码提供了一个编译期开关：

- `RF_CAPTURE_DISABLE_TIM2_IRQ_BRIDGE`

定义它以后，默认桥接函数不会编译出来，外部 ISR 需要自己调用 `RF_Capture_TIM2_IRQHandler()`。

这个点写进文档很重要，因为它直接关系到中断向量是否冲突。它不是“可选优化”，而是集成边界的一部分。

## hardware 和 master 的边界

这部分只说当前代码事实。

### hardware 负责什么

- 负责把 RF 前端产生的边沿间隔整理成 `rf_frame_t`
- 负责把 `rf_frame_t` 编成 `AA55/LEN/PAYLOAD/XOR`
- 负责经 `USART1` 把这批原始脉冲宽度上送

### hardware 不负责什么

- 不负责在 wire 上发送 `addr`
- 不负责在 wire 上发送 `key`
- 不负责在 wire 上发送 `conf`
- 不负责在 wire 上发送 `timestamp`
- 不负责在 wire 上发送 `source`
- 不负责在 wire 上发送门禁控制语义

换句话说，hardware 的 ABI 截止在“脉冲帧协议”这一层；业务字段是 master 后续层级根据同一帧脉冲解码、补充和包装出来的，不属于硬件线协议。

更完整的对接说明见 `Hardware/MASTER_INTEGRATION.md`。

## master 侧会在后面补什么

从当前仓库代码能静态确认的链路看，master 后续层级会继续做这些事：

- Linux `serdev` 驱动把 UART 字节流解析回结构化帧
- 驱动在 `/dev/rf433` 这一层补充驱动侧 `timestamp_ns` 和 `seq`
- 用户态 `rf_gateway` 把脉冲帧送进解码器，产出 `addr`、`key`、`conf`、`source`
- Qt 再把事件和 `pulse_us[]` 用于 UI 展示

所以如果你在某份文档里看到 `addr/key/conf/timestamp` 被写成“hardware 串口字段”，那份文档就是把层次写错了。

## 当前静态代码能确认的限制

- 当前没有实机链路验收结论。
- 当前没有长时间稳定性结论。
- 当前没有 RF 接收头模拟前端、天线、供电噪声的板级测试结论。
- 当前没有“可用于授权控制”或“可替代原门禁控制器”的结论。

这份 README 只应被当作“读代码导览 + 接口边界说明”，不能被当作产品能力验收报告。

## 接手时的最短排查路径

1. 先看 `User/main.c`，确认当前运行路径没有别的隐藏任务。
2. 再看 `Hardware/RF_Capture.c`，重点读 `RF_Capture_Init()`、`RF_Capture_TIM2_IRQHandler()`、`RF_Capture_DetectPulse()`、`RF_Capture_ProcessLoop()`。
3. 再看 `Hardware/RF_Protocol.c`，把 `AA55/LEN/PAYLOAD/XOR` 字节布局看死。
4. 最后看 `Hardware/RF_Uart.c`，确认 `USART1` 发送路径和“RX 只占位未接入”的事实。

只要这四步读通了，你就不会把这个目录误当成“解码器”或“控制器”。
