# project2_hardware 深读 02：采集与 UART 主线

## 1. 这一篇只讲主线

这一篇只追当前最关键的一条实时链：

```text
main()
-> RF_Capture_Init()
-> TIM2 捕获中断
-> RF_Capture_DetectPulse()
-> CurrentFrame / ReadyQueue
-> RF_Capture_ProcessLoop()
-> RF_Uart_SendFrame()
-> rf_proto_encode()
-> RF_Uart_TxEnqueue()
-> USART1_IRQHandler()
```

如果你只打算读一篇硬件专题，优先读这一篇。

## 2. 从 `main()` 的执行顺序开始

`main()` 只做三件初始化：

1. `RF_Capture_Init()`
2. `RF_Uart_Init()`
3. `Timer_Init()`

之后就在死循环里反复执行：

```c
RF_Capture_ProcessLoop();
```

这意味着：

- 实时采样靠中断。
- 主循环负责“把中断侧整理好的东西继续向前推”。
- 主循环本身不主动轮询 GPIO，也不自己计算脉冲宽度。

## 3. `RF_Capture_Init()` 到底做了什么

### 3.1 先清状态并写默认参数

`RF_Capture_Init()` 先把 `g_rf_capture` 全清，再写入默认运行参数：

- `TimerHz = 1000000`
- `TimerPeriod = 0xFFFF`
- `MinPulseUs = 80`
- `MaxPulseUs = 65535`
- `SyncUs = 8000`
- `MinFramePulses = 50`

这说明当前采样逻辑的单位就是“微秒级脉冲宽度”，不是电平次数，也不是业务码位。

### 3.2 配置输入引脚

采样引脚是：

- 端口：`GPIOA`
- 引脚：`PA0`
- 模式：上拉输入

代码里还写了一句注释，说明它这么做是为了提高空闲电平稳定性。也就是说，当前实现已经把“空闲噪声与抖动”作为一个真实问题来处理。

### 3.3 配置 `TIM2`

`TIM2` 的关键设置是：

- 预分频：`71`
- 结果：`72 MHz -> 1 MHz`
- 计数模式：向上计数
- 周期：`0xFFFF`
- 捕获通道：`TIM_Channel_1`
- 初始极性：上升沿
- 输入滤波：`0xF`

这组配置的直观含义是：

- `TIM2` 用 1 us 分辨率记录边沿间隔。
- 输入捕获数字滤波开得很高，用来压抑一部分毛刺和噪声。

### 3.4 打开两个中断源

`RF_Capture_Init()` 同时打开：

- `TIM_IT_CC1`
- `TIM_IT_Update`

原因是当前实现不仅关心“捕获到边沿”，还关心“16 位计数器是否回绕”。

## 4. `RF_Capture_TIM2_IRQHandler()` 为什么是整条链的核心

这就是当前固件最重要的实时函数。

## 4.1 它先处理捕获事件

当 `CC1` 触发时，它会读取：

- `CCR1`：边沿发生时的捕获值
- 当前计数器 `CNT`
- `UIF` 是否挂起
- 软件维护的 `Tim2OverflowCount`

然后拼出一个“扩展时间戳”。

### 4.2 为什么要拼扩展时间戳

因为 `TIM2` 是 16 位计数器，单次回绕以后只看 `CCR` 已经不够了。

当前代码的做法是：

- `TIM_IT_Update` 每次触发时增加 `Tim2OverflowCount`
- 如果本次进入中断时发现 `UIF` 还挂着，而且 `CCR <= CNT`，就把捕获归到下一轮计数周期

这能避免把“长间隔脉冲”误判成“短脉冲”。

### 4.3 第一次边沿只建基准，不产出脉冲

如果 `HasLastCaptureStamp == 0`，当前边沿只会被记作起点。

只有从第二个边沿开始，代码才会计算：

```text
delta_ticks = 当前时间戳 - 上一个时间戳
pulse_us = delta_ticks -> 微秒
```

然后才把 `pulse_us` 送进 `RF_Capture_DetectPulse()`。

### 4.4 为什么每次中断都翻转极性

处理完一个 `CC1` 之后，代码执行：

```c
TIM2->CCER ^= TIM_CCER_CC1P;
```

这一步的作用是：上次等上升沿，这次就等下降沿；上次等下降沿，这次就等上升沿。

所以当前实现采到的不是“只看某一类边沿的间隔”，而是连续高低电平段的宽度序列。

这正是后面 `pulse[]` 数组的来源。

## 5. `RF_Capture_DetectPulse()` 怎么把脉冲组织成帧

### 5.1 先做最基本的脉冲过滤

函数一开始就过滤掉三类输入：

- `PulseUs == 0`
- `PulseUs < MinPulseUs`
- `PulseUs > MaxPulseUs`

这说明当前主链默认把超短噪声和极端异常间隔直接丢掉，不进入后续分帧。

### 5.2 用 `SyncUs` 判断帧边界

当某个脉冲宽度大于 `SyncUs`，并且当前帧长度已经达到 `MinFramePulses`，代码就把它视为“可以触发分帧”的同步间隔。

但它不是简单地“看到同步就清空当前帧”。这里还有一个细节：

- 如果当前帧长度大于最小值，它会尝试把最后一个非同步脉冲留给下一帧作为 carry

这个设计非常工程化。它不是照搬教科书，而是试图减少同步间隔附近可能出现的分界误伤。

### 5.3 帧满时的处理

如果 `CurrentFrame.len` 已经到 `RF_BUFFER_SIZE`：

- 先刷出当前帧
- 再把当前脉冲放进新的帧开头

所以这里的策略不是“满了就整帧丢弃”，而是“先提交一帧，再接着收”。

## 6. 什么样的帧才允许进入 `ReadyQueue`

不是每个 `CurrentFrame` 都能进 `ReadyQueue`。

`RF_Capture_EnqueueFrame()` 至少做了三层筛选：

1. 帧不能为空，也不能超过 `RF_BUFFER_SIZE`
2. 脉冲数必须大于等于 `MinFramePulses`
3. `RF_Capture_FrameFirstLowIsLongest()` 必须通过

第三条尤其关键。当前实现要求“某个相位上的第一个低电平要是最长低电平”，用于进一步过滤不符合预期形态的帧。

这不是 EV1527 业务解码，但已经是明显的前端形态筛选。

## 7. `ReadyQueue` 解决了什么问题

`ReadyQueue` 的大小是 `4`，模型是：

- `TIM2` 中断：单生产者
- 主循环：单消费者

代码注释已经明确说明：

- 中断侧先把数据完整复制进槽位
- 再推进 `ReadyHead`
- 主循环只需要按 `ReadyTail` 拷贝并前移

这能降低长时间关中断的需要。

如果 `ReadyQueue` 满了，当前策略是：

- 增加 `DroppedFrames`
- 直接丢掉新帧

也就是说，队列满并不是阻塞等待，而是实时系统常见的“宁可丢帧，也不拖死采样中断”。

## 8. `RF_Capture_ProcessLoop()` 负责把帧真正推出去

这是主循环里的唯一常驻任务。

它主要处理三件事。

### 8.1 先重试上一次没发出去的帧

函数里有两个静态变量：

- `frame_work`
- `has_pending_tx`

如果上一次 `RF_Uart_SendFrame()` 失败了，说明发送 FIFO 当时塞不下整帧。主循环会先优先重试这帧，直到成功再继续后面的逻辑。

### 8.2 处理“空闲刷帧”

除了看到同步间隔会刷帧以外，主循环还会看：

- 当前帧长度是否至少达到 `MinFramePulses`
- 距离上一个边沿是否已经超过 `IdleFlushMs`

如果满足，就把 `CurrentFrame` 做一次快照，然后在一个很短的关中断区里确认：

- 帧长度还没变
- 最后边沿时间还没变

确认无竞争后，才把这一帧拿出来发送。

这一步很重要，因为它说明“分帧完成”不只依赖同步间隔，也依赖“长时间没有新边沿”。

### 8.3 把 `ReadyQueue` 里的帧一个个拿出来发

最后，主循环不断执行：

```text
RF_Capture_DequeueFrame()
-> RF_Uart_SendFrame()
```

如果某一帧发送失败，就把它记成 `pending`，留待下一轮重试。

## 9. `RF_Uart_Init()` 到底打开了什么

UART 当前配置非常明确：

| 项目 | 当前设置 |
| --- | --- |
| 外设 | `USART1` |
| TX | `PA9` |
| RX | `PA10` |
| 波特率 | `9600` |
| 数据位 | `8` |
| 校验 | `None` |
| 停止位 | `1` |
| 流控 | `None` |

从代码看，这个串口既开了接收中断，也开了发送中断，但当前主线上真正被使用的是发送侧。

## 10. `RF_Uart_SendFrame()` 为什么不是“直接发字节”

这一步分成两层：

1. `rf_proto_encode()` 把 `rf_frame_t` 编码成协议字节流
2. `RF_Uart_SendArray()` 把字节流放入发送 FIFO

也就是说，当前 UART 层并不知道 EV1527，也不知道业务意义。它只知道“有一帧已经编码好的字节需要送出去”。

## 11. `RF_Uart_TxEnqueue()` 和 `USART1_IRQHandler()` 如何配合

### 11.1 入队侧

`RF_Uart_TxEnqueue()` 会先检查：

- 长度是否为 0
- 长度是否超过 FIFO 总容量
- 当前剩余空间是否足够

如果放不下：

- 增加 `g_uart_tx_drop_frames`
- 返回失败

所以，这里丢的是“整帧送入发送 FIFO 的机会”，不是发送一半才丢。

### 11.2 出队侧

`USART1_IRQHandler()` 的 TXE 分支非常直接：

- FIFO 里还有字节：`USART_SendData()`
- FIFO 已空：关闭 `USART_IT_TXE`

这是一个标准的“软件 FIFO + TXE 中断抽水”模型。

## 12. 当前 RX 路径是什么状态

`RF_Uart.c` 里还保留了：

- `RF_Uart_GetRxData()`
- `RF_Uart_GetRxFlag()`

以及 `RXNE` 中断对最后一个接收字节的记录。

但从当前目录搜索结果看，这条 RX 路径没有被主流程消费。所以它现在更像：

- 预留接口
- 调试辅助

而不是当前业务主线的一部分。

## 13. 这条主线里最值得记住的工程判断

### 判断 1：采样和发送被明确分层

中断负责“尽快采”和“尽快记”；主循环负责“尽快往前送”。两者之间不是共享一个大状态机，而是靠帧缓存与发送 FIFO 解耦。

### 判断 2：当前固件已经有前端筛选意识

它不是把所有脉冲一股脑上送，而是已经做了：

- 脉冲阈值过滤
- 同步间隔判定
- 首个低电平最长的形态筛选
- 最小帧长约束

### 判断 3：当前代码口径到“上送前端”为止

它不负责 `/dev/rf433`，不负责 JSON，也不负责 UI。

## 14. 看主线时最该关注的故障点

如果未来要做实机联调，最先需要关注的静态风险点是：

- `TIM2` 输入极性翻转是否与实际前端极性匹配
- `SyncUs` / `MinFramePulses` 是否适配真实样本
- `ReadyQueue` 深度 `4` 是否足以覆盖突发流量
- `RF_UART_TX_FIFO_SIZE` 是否能覆盖典型帧长度和发送背压
- `RF_CAPTURE_DISABLE_TIM2_IRQ_BRIDGE` 是否需要在集成工程里显式定义

这里仍然只是风险提示，不是实机结论。

## 15. 下一篇看什么

如果你已经理解：

- 脉冲如何被采到
- 帧如何被整理出来
- UART 如何把整帧送出去

下一步就去看：

[project2_hardware_deep_dive_03_protocol_timer_and_aux.md](project2_hardware_deep_dive_03_protocol_timer_and_aux.md)

那一篇会把本篇反复提到、但没有完全展开的协议格式、1ms 时基和辅助模块补齐。
