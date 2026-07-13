# project2_hardware 深读 03：协议、时基与辅助模块

## 1. 为什么还要专门写这一篇

前一篇已经把主线跑通了，但还有四块内容没有展开完：

1. `RF_Protocol` 到底定义了什么字节格式
2. `Timer.c` 的 1ms 节拍到底怎样支撑主循环
3. `Delay.c` 现在处于什么角色
4. `RF_Tx.c` 为什么存在，但又不在当前主流程里

这篇就是把这些“支撑主线但不等于主线本身”的内容讲清楚。

## 2. `RF_Protocol`：当前上下位机共享的最小字节契约

## 2.1 `rf_frame_t` 是这套契约的核心

当前硬件侧协议围绕 `rf_frame_t` 定义：

```c
typedef struct {
    uint16_t pulse[RF_BUFFER_SIZE];
    uint16_t len;
} rf_frame_t;
```

它表达的不是业务事件，而是：

- 一串按顺序采集的脉冲宽度
- 以及这串脉冲到底有多少个样本

## 2.2 当前线上字节格式是什么

当前编码格式可以直接概括成：

```text
SYNC0  SYNC1  LEN_LO  LEN_HI  PAYLOAD[len * 2]  CRC
AA     55     little-endian pulse count         XOR
```

展开成表格更好记：

| 字段 | 大小 | 当前规则 | 含义 |
| --- | --- | --- | --- |
| `SYNC0` | 1 字节 | `0xAA` | 帧起始标记 1 |
| `SYNC1` | 1 字节 | `0x55` | 帧起始标记 2 |
| `LEN` | 2 字节 | 小端 | 脉冲个数，不是字节数 |
| `PAYLOAD` | `len * 2` 字节 | 每个脉冲一个 `uint16_t`，小端 | 脉冲宽度数组 |
| `CRC` | 1 字节 | `LEN + PAYLOAD` 的逐字节 XOR | 简单完整性校验 |

这里最容易读错的是 `LEN`：

- 它表示脉冲数
- 不是 payload 字节数

## 2.3 `rf_proto_encode()` 只负责编码，不做业务判断

`rf_proto_encode()` 的职责非常纯粹：

1. 检查 `frame` 是否为空
2. 检查 `len` 是否在合法范围内
3. 依次写入 `AA 55`
4. 写入小端 `len`
5. 逐个写入脉冲宽度的低字节和高字节
6. 计算 XOR CRC

它不关心：

- 这串脉冲是不是 EV1527
- 这个帧值是否可信
- 这个帧是否应该显示到 UI

这些都不是它的职责。

## 2.4 为什么硬件侧还实现了 parser

`RF_Protocol.c` 里除了编码，还有：

- `rf_proto_parser_init()`
- `rf_proto_parser_consume()`

这说明当前协议层并不只服务“发送”这一件事，它还保留了“按字节流恢复为 `rf_frame_t`”的能力。

但要注意边界：

- 当前硬件主线直接使用的是编码侧
- parser 不是当前 UART 上送主链的必经路径

你可以把 parser 理解成：

- 方便回环、测试、调试、镜像验证
- 也让协议层在设计上保持对称

而不是“当前主线发送时也要先走 parser”

## 3. `Timer.c`：主链背后的 1ms 节拍

## 3.1 `Timer_Init()` 在做什么

`Timer_Init()` 配的是 `TIM3`，主要目的是生成稳定的 1ms 更新中断。

关键设置：

- `72 MHz -> 1 MHz` 计数基准
- 周期：`1000 us`
- 更新中断：打开

这意味着：

- `TIM3` 不是拿来采脉冲的
- 它是系统时基和空闲刷帧的支撑时钟

## 3.2 `TIM3_IRQHandler()` 给主链提供了什么

每次 `TIM3` 更新中断触发时，代码会做两件事：

1. `g_time_base_us += 1000`
2. `RF_Capture_Tick1msHandler()`

第二步尤其重要，因为 `RF_Capture_Tick1msHandler()` 会增加 `g_rf_capture.MsTick`。

而 `RF_Capture_ProcessLoop()` 正是通过：

```text
now_ms - LastEdgeMs > IdleFlushMs
```

来判断是否应该把一帧因“长时间无新边沿”而刷出。

也就是说，`Timer.c` 虽然不采样，但它决定了“空闲分帧”何时发生。

## 3.3 `Timer_NowUs()` 的角色

`Timer_NowUs()` 通过：

- `g_time_base_us`
- `TIM3` 当前计数值
- 以及对更新标志位的处理

组合出一个微秒级时间值。

但从当前目录内的调用关系看，主线并没有直接依赖 `Timer_NowUs()` 来测脉冲。脉冲宽度测量已经由 `TIM2` 捕获侧完成。

所以当前更准确的说法是：

- `Timer_NowUs()` 是可用的通用微秒时间接口
- 不是当前 RF 采样主线的核心入口

## 4. `Delay.c`：存在，但当前主链未用

`Delay.c` 提供：

- `Delay_us()`
- `Delay_ms()`
- `Delay_s()`

它基于 `SysTick` 做阻塞式延时。

当前主流程没有使用它，这一点反而很值得写出来，因为这说明：

- 当前采样和上送逻辑没有靠阻塞延时推进状态
- 主链是“中断 + 主循环 + FIFO”模型，不是“延时脚本式模型”

如果未来有人在主链里引入 `Delay_*()`，那会直接改变实时行为，应当格外谨慎。

## 5. `RF_Tx.c`：脉冲回放辅助模块

## 5.1 这个模块能做什么

`RF_Tx.c` 提供的能力很直白：

- 初始化某个 GPIO 输出脚
- 按脉冲数组交替拉高拉低
- 用忙等待方式延时
- 回放一个 `rf_frame_t`

从函数名就能看出来：

- `RF_Tx_Init()`
- `RF_Tx_SetLevel()`
- `RF_Tx_DelayUs()`
- `RF_Tx_Replay()`
- `RF_Tx_ReplayFrame()`

## 5.2 它为什么不算当前主线

因为 `main()` 没有调用它，当前 `RF_Capture_ProcessLoop()` 也没有把任何帧送给它。

所以当前对它最准确的表述是：

- 辅助测试 / 预留回放模块
- 不是当前交付主链的一部分

### 5.3 为什么文档还要专门写它

因为读代码时很容易产生一种错觉：

> 既然有 TX 模块，那项目是不是还能主动发射或控制外部设备？

当前仓库不能这么写。`RF_Tx.c` 的存在，只能证明“代码里保留了脉冲回放工具”，不能证明任何外部控制能力已经交付。

## 6. `RF_CAPTURE_DISABLE_TIM2_IRQ_BRIDGE` 是什么

`RF_Capture.c` 末尾有一段桥接逻辑：

```c
#if !defined(RF_CAPTURE_DISABLE_TIM2_IRQ_BRIDGE)
void TIM2_IRQHandler(void) {
    RF_Capture_TIM2_IRQHandler();
}
#endif
```

它的意义是：

- 默认情况下，这个模块直接导出 `TIM2_IRQHandler`
- 如果集成工程里已经有自己的 `TIM2_IRQHandler`，可以定义宏关闭默认桥接
- 然后在外部 ISR 里手动调用 `RF_Capture_TIM2_IRQHandler()`

这说明当前代码作者已经考虑到了“集成到更大工程时中断入口可能冲突”的问题。

对读者来说，要记住两点：

1. 这是一个集成点，不是业务逻辑点
2. 它不改变采样算法，只改变 ISR 入口归属

## 7. 主链、辅助链、预留链怎么分

为了防止读完三篇之后仍然混淆，下面直接给出一个收束表。

| 模块 | 当前状态 | 为什么 |
| --- | --- | --- |
| `RF_Capture` | 主链 | 负责采样、分帧、排队 |
| `RF_Uart` | 主链 | 负责编码后的串口发送 |
| `RF_Protocol::encode` | 主链 | 是 UART 上送必经步骤 |
| `Timer_Init` / `RF_Capture_Tick1msHandler` | 主链支撑 | 提供空闲刷帧所需的 1ms 节拍 |
| `RF_Protocol::parser` | 辅助 | 当前主线上送不依赖它 |
| `Timer_NowUs` | 辅助 | 是通用时间接口，不是主线核心 |
| `Delay_*` | 辅助 / 未使用 | 当前主流程未调用 |
| `RF_Tx_*` | 辅助 / 预留 | 当前未接入主流程 |
| UART RX 接口 | 预留 | 当前未被主流程消费 |

## 8. 读完整个 `project2_hardware` 后，你应该形成的结论

正确结论应该是下面这样：

- 下位机固件的当前重点是“把 RF 前端脉冲稳定地变成可上送帧”。
- 它已经具备完整的采样、分帧、编码、FIFO 发送骨架。
- 它没有越界承担业务解码、UI 或主控侧驱动职责。
- 它保留了一些辅助与扩展模块，但不能据此宣称额外功能已经交付。
- 整个专题的完成口径仍然只到静态代码要求与静态代码检验，不到实机联调验收。

## 9. 最后一篇怎么用

如果你下一步要做的是：

- 按函数定位代码
- 快速回查某个接口在哪个文件里
- 分清某个函数是主链、辅助还是预留

请直接使用：

[project2_hardware_deep_dive_04_function_index.md](project2_hardware_deep_dive_04_function_index.md)
