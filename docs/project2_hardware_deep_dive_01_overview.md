# STM32 固件分册 1：初始化、资源与配置

## 目标与前置条件

本文帮助固件维护者定位入口、外设资源和可调参数。先具备 STM32F10x 标准外设库工程、启动文件和板级时钟配置，再阅读源码；本目录没有独立链接脚本。

## 启动顺序

`project2_hardware/User/main.c`：

1. `RF_Capture_Init()` 清空采集状态并配置 PA0/TIM2 双边沿捕获。
2. `RF_Uart_Init()` 配置 PA9/PA10 与 USART1 9600 8N1，并打开 RXNE 中断。
3. `Timer_Init()` 配置 TIM3，为 `Timer_NowUs` 和 1 ms tick 提供时间基础。
4. 主循环调用 `RF_Capture_ProcessLoop()`，不在中断里编码整帧或等待串口。

`RF_Capture_Init` 的默认过滤值在 `Hardware/RF_Capture.c`：

| 参数 | 默认值 | 含义 |
|---|---:|---|
| `MinPulseUs` | 80 us | 更短脉宽丢弃 |
| `MaxPulseUs` | 65535 us | 更长间隔饱和/过滤边界 |
| `SyncUs` | 8000 us | 分帧同步低脉宽阈值 |
| `MinFramePulses` | 50 | 入队所需最少 pulse |
| ready queue | 4 个后备槽、有效容量 3 帧 | 哨兵环满时拒绝新到帧；不推进 tail，也不保证 freshness |

运行时可调用 `RF_Capture_SetFilter` 更新前四项。只应在理解接收器波形后调整；把最小帧数降得很低会让不能被 EV1527 解码器接受的片段进入后级。

## 所有权与并发边界

- TIM2 中断更新当前帧和队列 head；主循环更新 tail，形成单生产者/单消费者队列。
- USART1 主循环把完整 packet 入 TX FIFO，TXE 中断消费字节。
- `volatile` 用于 ISR/主循环共享索引和状态，不等于可以任意增加多生产者。
- Timer 与捕获分别使用 TIM3、TIM2；改变板级时钟后必须复核微秒换算。

## 目标板验证

构建、烧录后：

1. 空闲时确认 PA9 没有持续乱码。
2. 给 PA0 输入已知脉冲，观察 `RF_Capture_GetLastIntervalUs()` 或逻辑分析仪测得间隔。
3. 确认 USART1 输出从 `AA 55` 开始，长度与 pulse 数一致。
4. 连续快速触发时，ready queue 满可拒绝新到帧；UART FIFO 暂满时主循环应保留并重试同一待发帧、暂停后续 dequeue，但不应死锁或在 ISR 阻塞。

失败时先核对系统时钟、TIM2 输入映射、NVIC 入口和共地；若时间值整体成比例错误，通常是定时器时钟假设不匹配，而不是协议端序错误。

## 验证边界

主机协议测试不能覆盖 STM32 时钟树、标准库版本或硬件波形。本文参数来自当前源码，不是对任意板级工程的自动构建保证。下一步见 [捕获与 UART](project2_hardware_deep_dive_02_capture_and_uart.md)。
