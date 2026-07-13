# project2_hardware 深读 04：函数索引

## 1. 这份索引怎么用

这不是一篇连续叙事文，而是一个“查函数、查角色、查主链位置”的速查表。

建议用法：

1. 先确认你要找的是哪个文件。
2. 再看该函数属于主链、辅助还是预留。
3. 如果需要完整上下文，再回到前面三篇专题。

状态列统一使用三种标记：

- 主链：当前采样与 UART 上送必经
- 支撑：当前主链会依赖，但不是数据主线本身
- 预留 / 辅助：存在于仓库中，但不属于当前交付主线

## 2. `User/main.c`

| 函数 | 状态 | 作用 |
| --- | --- | --- |
| `main` | 主链 | 初始化采集、串口、时基，然后在死循环中执行 `RF_Capture_ProcessLoop()` |

#### 代表性代码摘录：主链入口其实非常短

代码来源：`project2_hardware/User/main.c::main()`

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

- 为什么先看这段：它把硬件主链真正串起来的顺序压缩得最清楚，先起采集、再起 UART、再起时基，最后一直跑采集处理循环。
- 看代码时要注意什么：这里没有业务分支、没有多余线程；后面所有复杂度都藏在 `RF_Capture_ProcessLoop()` 和中断路径里。

## 3. `Hardware/RF_Capture.h`

| 接口 | 状态 | 作用 |
| --- | --- | --- |
| `RF_Capture_Init` | 主链 | 初始化采样输入、`TIM2` 和相关状态 |
| `RF_Capture_SetFilter` | 支撑 | 运行期修改脉冲阈值、同步阈值和最小帧长 |
| `RF_Capture_ProcessLoop` | 主链 | 主循环处理空闲刷帧、队列出帧和发送重试 |
| `RF_Capture_Tick1msHandler` | 支撑 | 接收 `TIM3` 的 1ms tick |
| `RF_Capture_TIM2_IRQHandler` | 主链 | `TIM2` 捕获中断核心处理 |
| `RF_Capture_GetLastIntervalUs` | 支撑 | 查询最近一次脉冲间隔，偏向调试用途 |

## 4. `Hardware/RF_Capture.c`

### 4.1 内部辅助函数

| 函数 | 状态 | 作用 |
| --- | --- | --- |
| `RF_Capture_ComputeIdleFlushMs` | 支撑 | 根据 `SyncUs` 推导空闲刷帧超时，结果夹在 `12~100 ms` |
| `RF_Capture_TicksToUs` | 主链支撑 | 把 `TIM2` tick 差值换算为微秒 |
| `RF_Capture_BuildStampTicks` | 主链支撑 | 结合回绕计数和 CCR 构造扩展时间戳 |
| `RF_Capture_FirstLowLongestForPhase` | 支撑 | 判断某个相位上的首个低电平是否最长 |
| `RF_Capture_FrameFirstLowIsLongest` | 支撑 | 对整帧做“首个低电平最长”筛选 |
| `RF_Capture_EnqueueFrame` | 主链 | 把通过筛选的帧复制进 `ReadyQueue` |
| `RF_Capture_DequeueFrame` | 主链 | 从 `ReadyQueue` 取出一帧交给主循环 |
| `RF_Capture_FlushCurrentFrame` | 主链 | 把 `CurrentFrame` 按条件刷入队列并清空 |
| `RF_Capture_DetectPulse` | 主链 | 执行脉冲过滤、同步判定、分帧和满缓冲处理 |

### 4.2 对外可见实现

| 函数 | 状态 | 作用 |
| --- | --- | --- |
| `RF_Capture_SetFilter` | 支撑 | 允许运行期更新 `MinPulseUs`、`MaxPulseUs`、`SyncUs`、`MinFramePulses` |
| `RF_Capture_Init` | 主链 | 初始化 `g_rf_capture`、PA0、TIM2、捕获滤波和 NVIC |
| `RF_Capture_Tick1msHandler` | 支撑 | 每次 1ms 节拍到来时递增 `MsTick` |
| `RF_Capture_GetLastIntervalUs` | 支撑 | 返回最近一次测得的脉冲宽度 |
| `RF_Capture_TIM2_IRQHandler` | 主链 | 计算脉冲宽度、更新最后边沿时间、切换捕获极性、处理回绕 |
| `TIM2_IRQHandler` | 主链桥接 | 默认桥接到 `RF_Capture_TIM2_IRQHandler()`，可由宏关闭 |
| `RF_Capture_ProcessLoop` | 主链 | 处理 pending 发送、空闲刷帧和 `ReadyQueue` 出帧 |

#### 代表性代码摘录：`RF_Capture` 的典型风格是“中断测脉冲，主循环刷帧发串口”

代码来源：`project2_hardware/Hardware/RF_Capture.c::RF_Capture_TIM2_IRQHandler()` / `RF_Capture_ProcessLoop()`

```c
pulse_us = RF_Capture_TicksToUs((uint32_t)delta_ticks);
g_rf_capture.LastIntervalUs = pulse_us;
RF_Capture_DetectPulse(pulse_us);
g_rf_capture.LastEdgeMs = g_rf_capture.MsTick;

if (
    len_snapshot >= g_rf_capture.MinFramePulses &&
    (now_ms - edge_ms) > (uint32_t)g_rf_capture.IdleFlushMs
) {
    frame_work.len = len_snapshot;
    memcpy(frame_work.pulse, g_rf_capture.CurrentFrame.pulse, (size_t)len_snapshot * sizeof(uint16_t));
    if (RF_Uart_SendFrame(&frame_work) == 0u) {
        has_pending_tx = 1u;
        return;
    }
}
```

- 为什么先看这段：它最能代表捕获层的真实运行方式，脉冲宽度在中断里测，成帧和发送重试在主循环里做。
- 看代码时要注意什么：`RF_Capture` 不是一进中断就直接发 UART；它先做分帧和空闲刷帧，再把完整帧交给发送层。

## 5. `Hardware/RF_Protocol.h`

| 接口 / 类型 | 状态 | 作用 |
| --- | --- | --- |
| `RF_PROTO_SYNC0` / `RF_PROTO_SYNC1` | 主链支撑 | 定义固定同步头 `0xAA 0x55` |
| `RF_BUFFER_SIZE` | 主链支撑 | 定义单帧最大脉冲容量 `1024` |
| `rf_frame_t` | 主链核心模型 | 表示一帧脉冲宽度数据 |
| `rf_parse_state_t` | 支撑 | parser 状态机枚举 |
| `rf_proto_parser_t` | 支撑 | parser 运行状态 |
| `rf_proto_crc8` | 主链支撑 | 按字节 XOR 计算 CRC |
| `rf_proto_encode` | 主链 | 把 `rf_frame_t` 编码成上行字节流 |
| `rf_proto_parser_init` | 辅助 | 初始化协议解析器 |
| `rf_proto_parser_consume` | 辅助 | 流式消费字节并恢复 `rf_frame_t` |

## 6. `Hardware/RF_Protocol.c`

| 函数 | 状态 | 作用 |
| --- | --- | --- |
| `rf_proto_crc8` | 主链支撑 | 对 `LEN + PAYLOAD` 做逐字节 XOR |
| `rf_proto_encode` | 主链 | 写出 `AA55 + LEN + PAYLOAD + CRC` |
| `rf_proto_parser_init` | 辅助 | 清空 parser 状态并回到等待 `SYNC0` |
| `rf_proto_parser_consume` | 辅助 | 依次处理 `SYNC0 -> SYNC1 -> LEN -> PAYLOAD -> CRC` |

#### 代表性代码摘录：协议层只做脉冲帧封包与解包

代码来源：`project2_hardware/Hardware/RF_Protocol.c::rf_proto_encode()`

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

- 为什么先看这段：它最能说明硬件协议层的职责边界，只负责把 `rf_frame_t` 变成 `AA55/LEN/PAYLOAD/CRC`。
- 看代码时要注意什么：这里没有 EV1527 业务语义，只有 pulse 宽度和 CRC；如果你想找 `addr/key` 一类字段，方向已经错了。

## 7. `Hardware/RF_Uart.h`

| 接口 | 状态 | 作用 |
| --- | --- | --- |
| `RF_Uart_Init` | 主链 | 初始化 `USART1`、GPIO 和中断 |
| `RF_Uart_SendByte` | 辅助 | 直接发送一个字节，当前主链不依赖 |
| `RF_Uart_SendArray` | 主链支撑 | 把一段字节数组入发送 FIFO |
| `RF_Uart_SendFrame` | 主链 | 编码并发送一帧 `rf_frame_t` |
| `RF_Uart_GetRxData` | 预留 | 读取最近一次接收字节 |
| `RF_Uart_GetRxFlag` | 预留 | 读取并清除接收标志 |

## 8. `Hardware/RF_Uart.c`

| 函数 | 状态 | 作用 |
| --- | --- | --- |
| `RF_Uart_TxUsed` | 主链支撑 | 计算发送 FIFO 已占用字节数 |
| `RF_Uart_TxFree` | 主链支撑 | 计算发送 FIFO 剩余空间 |
| `RF_Uart_TxEnqueue` | 主链 | 检查容量后把整包字节写入发送 FIFO |
| `RF_Uart_TxDequeueByte` | 主链支撑 | 从 FIFO 取出一个待发字节 |
| `RF_Uart_Init` | 主链 | 配置 `PA9/PA10`、`USART1`、`RXNE/TXE` 中断与 NVIC |
| `RF_Uart_SendByte` | 辅助 | 忙等方式直接发送单字节 |
| `RF_Uart_SendArray` | 主链支撑 | 对外暴露数组入队接口 |
| `RF_Uart_SendFrame` | 主链 | 调用 `rf_proto_encode()` 后发送整帧 |
| `RF_Uart_GetRxData` | 预留 | 返回最近接收字节 |
| `RF_Uart_GetRxFlag` | 预留 | 查询并清零接收标志 |
| `USART1_IRQHandler` | 主链 | RX 分支记录最近字节，TX 分支持续抽水发送 FIFO |

#### 代表性代码摘录：UART 层先编码入 FIFO，再靠中断抽水发送

代码来源：`project2_hardware/Hardware/RF_Uart.c::RF_Uart_SendFrame()` / `USART1_IRQHandler()`

```c
n = rf_proto_encode(Frame, g_uart_tx_packet, sizeof(g_uart_tx_packet));
if (n > 0u && n <= 65535u) {
    return RF_Uart_SendArray(g_uart_tx_packet, (uint16_t)n);
}

if (USART_GetITStatus(USART1, USART_IT_TXE) == SET) {
    uint8_t tx_byte = 0u;
    if (RF_Uart_TxDequeueByte(&tx_byte) != 0u) {
        USART_SendData(USART1, tx_byte);
    } else {
        USART_ITConfig(USART1, USART_IT_TXE, DISABLE);
    }
}
```

- 为什么先看这段：它最能代表 UART 层的实际风格，主链不是阻塞式逐字节发送，而是编码后进 FIFO，再由 TXE 中断持续发送。
- 看代码时要注意什么：`RF_Uart_SendFrame()` 成功只是“入队成功”，不等于已经把整帧发完；真正的串口推进在 `USART1_IRQHandler()`。

## 9. `Hardware/RF_Tx.h` 与 `Hardware/RF_Tx.c`

### 9.1 头文件接口

| 接口 | 状态 | 作用 |
| --- | --- | --- |
| `RF_Tx_Init` | 预留 / 辅助 | 初始化某个输出 GPIO |
| `RF_Tx_SetLevel` | 预留 / 辅助 | 拉高或拉低输出脚 |
| `RF_Tx_DelayUs` | 预留 / 辅助 | 忙等待微秒延时 |
| `RF_Tx_Replay` | 预留 / 辅助 | 按脉冲数组回放波形 |
| `RF_Tx_ReplayFrame` | 预留 / 辅助 | 直接回放一帧 `rf_frame_t` |

### 9.2 源文件实现

| 函数 | 状态 | 作用 |
| --- | --- | --- |
| `RF_Tx_EnablePortClock` | 辅助 | 根据端口打开 GPIO 时钟 |
| `RF_Tx_Init` | 预留 / 辅助 | 建立输出脚上下文并拉低初始电平 |
| `RF_Tx_SetLevel` | 预留 / 辅助 | 按当前上下文设置引脚电平 |
| `RF_Tx_DelayUs` | 预留 / 辅助 | 用 `NOP` 忙等待做微秒延时 |
| `RF_Tx_Replay` | 预留 / 辅助 | 交替输出高低电平并按脉冲宽度延时 |
| `RF_Tx_ReplayFrame` | 预留 / 辅助 | 读取 `rf_frame_t` 并调用 `RF_Tx_Replay` |

#### 代表性代码摘录：`RF_Tx` 是现成的波形回放能力，但不在当前主链里

代码来源：`project2_hardware/Hardware/RF_Tx.c::RF_Tx_Replay()`

```c
for (i = 0u; i < Length; ++i) {
    RF_Tx_SetLevel(level);
    RF_Tx_DelayUs(Pulse[i]);
    level = (uint8_t)(1u - level);
}
RF_Tx_SetLevel(0u);
```

- 为什么先看这段：它能最快解释“为什么仓库里有 TX 模块，但主线叙述里没把它算进去”。
- 看代码时要注意什么：这个模块是按脉冲数组回放 GPIO 电平，当前 `main()` 并没有调用它，所以它属于辅助/预留能力，不是交付主链。

## 10. `system/Timer.h` 与 `system/Timer.c`

| 函数 | 状态 | 作用 |
| --- | --- | --- |
| `Timer_Init` | 主链支撑 | 初始化 `TIM3` 为 1ms 周期时基 |
| `Timer_NowUs` | 支撑 | 提供通用微秒时间查询 |
| `TIM3_IRQHandler` | 主链支撑 | 维护 `g_time_base_us` 并调用 `RF_Capture_Tick1msHandler()` |

#### 代表性代码摘录：Timer 层的关键不是“计时”，而是给捕获层喂 1ms 节拍

代码来源：`project2_hardware/system/Timer.c::TIM3_IRQHandler()`

```c
void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) == SET) {
        g_time_base_us += TIMER_PERIOD_US;
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        RF_Capture_Tick1msHandler();
    }
}
```

- 为什么先看这段：它把 Timer 模块和捕获模块的真实耦合点写得很直白，`RF_Capture` 的空闲刷帧依赖的就是这 1ms 节拍。
- 看代码时要注意什么：`Timer_NowUs()` 是通用辅助；真正进入主链节奏的是 `TIM3_IRQHandler()` 对 `RF_Capture_Tick1msHandler()` 的调用。

## 11. `system/Delay.h` 与 `system/Delay.c`

| 函数 | 状态 | 作用 |
| --- | --- | --- |
| `Delay_us` | 预留 / 辅助 | 用 `SysTick` 做微秒阻塞延时 |
| `Delay_ms` | 预留 / 辅助 | 毫秒阻塞延时 |
| `Delay_s` | 预留 / 辅助 | 秒级阻塞延时 |

#### 代表性代码摘录：`Delay` 是阻塞式辅助，不参与当前采集上送主线

代码来源：`project2_hardware/system/Delay.c::Delay_us()`

```c
void Delay_us(uint32_t xus) {
    SysTick->LOAD = 72 * xus;
    SysTick->VAL = 0x00;
    SysTick->CTRL = 0x00000005;
    while (!(SysTick->CTRL & 0x00010000));
    SysTick->CTRL = 0x00000004;
}
```

- 为什么先看这段：它能直接说明 `Delay` 模块是典型阻塞延时工具，和当前中断采集、FIFO 上送这条主线不是一个设计风格。
- 看代码时要注意什么：如果后面你看到某处用了 `Delay_*`，那通常意味着辅助/测试路径，而不是高频采样主链。

## 12. 如果你要按问题反查，先看哪里

| 你想回答的问题 | 先看函数 |
| --- | --- |
| RF 脉冲是在哪里被测成微秒宽度的 | `RF_Capture_TIM2_IRQHandler` |
| 脉冲是在哪里被判断成一帧的 | `RF_Capture_DetectPulse` |
| 完成帧是在哪里入队的 | `RF_Capture_EnqueueFrame` |
| 主循环是在哪里刷帧和重试发送的 | `RF_Capture_ProcessLoop` |
| 协议帧是在哪里编码的 | `rf_proto_encode` |
| UART 发送 FIFO 是在哪里入队和出队的 | `RF_Uart_TxEnqueue` / `RF_Uart_TxDequeueByte` |
| 真正的串口发送中断在哪里 | `USART1_IRQHandler` |
| 1ms tick 是在哪里产生的 | `TIM3_IRQHandler` |
| 为什么有 TX 回放模块却不在主线里 | `main` 与 `RF_Tx_*` 对照看 |

## 13. 本专题到这里为止的最终结论

如果把整个 `project2_hardware` 专题压缩成一句话，可以写成：

> 当前 `project2_hardware` 已形成一条以 `TIM2` 捕获、`RF_Capture` 分帧、`RF_Protocol` 编码、`USART1` 上送为核心的静态代码主链；其余模块要么是支撑时基，要么是辅助 / 预留能力，不能写成实机联调已完成。

回到专题入口请看 [project2_hardware_deep_dive.md](project2_hardware_deep_dive.md)。
