# project2_hardware 深读 01：总览

## 1. 先把口径定死

`project2_hardware` 当前统一理解为：

> 面向 `project2` 的 STM32 RF 前端固件，负责脉冲采集、分帧、协议编码和 UART 上送。

本篇只基于当前静态代码讲解，不宣称：

- 实机接线已经完成
- 与 RK3568 板端已经联调完成
- RF 波形质量、长时间稳定性已经验收完成

这篇的目标只有一个：让你先把“这个目录里谁在主链上、谁不在主链上”看明白。

## 2. 从 `main()` 开始看全局

`project2_hardware/User/main.c` 非常短，但它已经把当前固件的主结构说明白了：

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

这段代码可以直接读出四件事：

1. 主链入口只有三个初始化：采集、串口、时基。
2. 没有业务解码线程，没有 UI，没有文件系统。
3. 主循环唯一常驻任务是 `RF_Capture_ProcessLoop()`。
4. 所有真正的实时采样动作都在中断上下文里，不在 `while (1)` 里。

这就是整个目录的骨架。

## 3. 真实主链长什么样

### 3.1 信号如何进入固件

- 输入引脚：`PA0`
- 外设角色：`TIM2_CH1`
- 采样方式：输入捕获
- 计数时基：`TIM2` 以 1 MHz 计数，也就是 1 tick = 1 us

也就是说，固件并不是“按位解码 RF 协议”，而是先把前端脉冲宽度测出来。

### 3.2 脉冲如何变成帧

脉冲进入 `RF_Capture_TIM2_IRQHandler()` 后，会被整理成一串 `uint16_t` 脉冲宽度，写入 `rf_frame_t`：

```c
typedef struct {
    uint16_t pulse[RF_BUFFER_SIZE];
    uint16_t len;
} rf_frame_t;
```

这里的 `rf_frame_t` 是当前目录里最核心的数据模型。你可以把它理解成：

- `pulse[]`：按顺序记录的脉冲宽度数组
- `len`：当前帧里记录了多少个脉冲

### 3.3 帧如何离开固件

当 `RF_Capture_ProcessLoop()` 判断出“当前帧可以刷出”之后，会调用：

```text
RF_Uart_SendFrame()
-> rf_proto_encode()
-> RF_Uart_TxEnqueue()
-> USART1_IRQHandler()
-> PA9(TX)
```

也就是说，固件输出的不是调试文本，而是固定格式的二进制协议帧。

## 4. 当前目录里的模块地图

| 文件 | 角色 | 是否在当前主链上 | 你应该怎样理解它 |
| --- | --- | --- | --- |
| `User/main.c` | 启动入口 | 是 | 初始化三大模块并驱动主循环 |
| `Hardware/RF_Capture.c` | 脉冲采集与分帧核心 | 是 | 当前最关键的实时逻辑 |
| `Hardware/RF_Protocol.c` | 协议编码 / 解析工具 | 是，但主链只直接用编码侧 | 定义上行字节格式 |
| `Hardware/RF_Uart.c` | UART FIFO 与发包 | 是 | 把帧真正送出芯片 |
| `system/Timer.c` | 1ms 节拍与时间基 | 是 | 支撑空闲刷帧和系统 tick |
| `Hardware/RF_Tx.c` | 脉冲回放工具 | 否 | 当前未接入主流程 |
| `system/Delay.c` | 阻塞延时工具 | 否 | 当前主流程未调用 |

## 5. 启动顺序为什么是这样

### 5.1 先 `RF_Capture_Init()`

原因很直接：先把采样硬件和中断打开，系统才能开始看到外部 RF 脉冲。

### 5.2 再 `RF_Uart_Init()`

采到帧之后必须有办法送出去，所以串口是第二个关键初始化项。

### 5.3 最后 `Timer_Init()`

`RF_Capture_ProcessLoop()` 需要 1ms 节拍来判断“多久没有新边沿，可以把当前帧刷出去”。这个节拍由 `TIM3_IRQHandler()` 通过 `RF_Capture_Tick1msHandler()` 提供。

这也是为什么 `Timer_Init()` 虽然不直接参与采样，但仍在主链上。

## 6. 当前行为最重要的参数

这些参数决定了“什么样的脉冲会被收下、什么样的帧会被丢弃、什么时候会刷帧”。

| 参数 | 当前值 | 位置 | 含义 |
| --- | --- | --- | --- |
| `RF_BUFFER_SIZE` | `1024` | `RF_Protocol.h` | 单帧最多容纳的脉冲数 |
| `RF_CAPTURE_READY_QUEUE_SIZE` | `4` | `RF_Capture.c` | 采集完成帧的等待队列深度 |
| `RF_CAPTURE_DEFAULT_MIN_PULSE_US` | `80` | `RF_Capture.c` | 小于该值的脉冲直接忽略 |
| `RF_CAPTURE_DEFAULT_MAX_PULSE_US` | `65535` | `RF_Capture.c` | 大于该值的脉冲直接忽略 |
| `RF_CAPTURE_DEFAULT_SYNC_US` | `8000` | `RF_Capture.c` | 用作同步间隔判断的阈值 |
| `RF_CAPTURE_DEFAULT_MIN_FRAME_PULSES` | `50` | `RF_Capture.c` | 小于该脉冲数的帧不入队 |
| `RF_UART_BAUDRATE` | `9600` | `RF_Uart.c` | 当前 UART 波特率 |
| `RF_UART_TX_FIFO_SIZE` | `2304` | `RF_Uart.c` | UART 软件发送 FIFO 大小 |

这些常量本身不代表“参数已经调优完成”，只代表“当前代码就是按这组值工作”。

## 7. 主链里最值得先抓住的三个设计点

### 7.1 用 `TIM2` 的扩展时间戳避免长间隔误判

`RF_Capture_TIM2_IRQHandler()` 不只看 `CCR`，还维护了 `Tim2OverflowCount`，用于处理定时器回绕。否则长间隔很容易被误读成一个很短的脉冲。

### 7.2 用双层缓存隔开中断和主循环

当前实现里至少有两层缓存概念：

- `CurrentFrame`：当前正在拼的帧
- `ReadyQueue[4]`：已经拼好的帧，等待主循环拿走

这意味着中断侧是单生产者，主循环是单消费者。这个模型直接影响后面所有“何时复制、何时清空、何时重试”的逻辑。

### 7.3 用 UART 发送 FIFO 对抗瞬时背压

`RF_Uart_SendFrame()` 并不直接忙等整个数据包发送完，而是先编码，再入发送 FIFO，由 `USART1_IRQHandler()` 持续把字节送出。

所以，主循环的工作不是“逐字节发完再回来”，而是“尽量把一整帧交给发送侧”。

## 8. 哪些东西最容易被误读

### 8.1 误读一：`RF_Tx.c` 也是当前主线的一部分

不是。它目前没有被 `main()` 调用，更像是后续测试或回放工具。

### 8.2 误读二：`RF_Protocol` 的 parser 是当前 UART 上送主线的一部分

不是。当前上行主线直接使用 `rf_proto_encode()`。parser 存在，但当前主流程不依赖它来发送数据。

### 8.3 误读三：只要代码链完整，就能写成“联调完成”

不能。当前只能说“静态代码链条闭合”，不能说“实机联调完成”。

## 9. 读完本篇后，下一篇该看什么

如果你现在已经知道：

- `main()` 初始化了什么
- 主循环为什么只调用 `RF_Capture_ProcessLoop()`
- `rf_frame_t` 是什么
- 哪些文件在主链上

那就可以继续读：

[project2_hardware_deep_dive_02_capture_and_uart.md](project2_hardware_deep_dive_02_capture_and_uart.md)

下一篇会把最重要的实时链从 `TIM2` 中断一直走到 `USART1` 发送中断。
