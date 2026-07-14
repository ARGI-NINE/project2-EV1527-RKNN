# Project2 STM32 RF 采集固件教程

## 目标

本目录提供 STM32F103 的 RF433 脉宽采集和串口上送源码。读完后应能把它加入已有标准外设库工程、完成接线、烧录并用逻辑分析仪验证 AA55 packet。它不是一个自带链接脚本的独立构建工程。

## 前置条件

- STM32F103、ST 标准外设库与适配板卡的启动文件/链接脚本。
- RF433 接收模块，数字输出接 `PA0`。
- USART1 `PA9(TX)`/`PA10(RX)`，9600 8N1，无流控；接 RK3568 时使用 3.3 V 电平并共地。
- 可查看 PA0/PA9 的逻辑分析仪或示波器。

## 源码组成

| 路径 | 责任 |
|---|---|
| `User/main.c` | 初始化和主循环 |
| `Hardware/RF_Capture.*` | TIM2 双边沿间隔、过滤、4 后备槽/有效容量 3 帧的 ready queue |
| `Hardware/RF_Uart.*` | 协议编码、2304-byte TX FIFO、USART1 ISR |
| `Hardware/RF_Protocol.*` | AA55/LE16/XOR encoder/parser |
| `system/Timer.*` | TIM3 时间与 1 ms idle-flush tick |
| `Hardware/RF_Tx.*`、`system/Delay.*` | 公开辅助发送/延时模块，生产 main 未调用 |

主循环是：

```text
RF_Capture_Init -> RF_Uart_Init -> Timer_Init
while (1): RF_Capture_ProcessLoop
```

TIM2 ISR 生产 pulse frame，主循环消费 ready queue；主循环生产 TX FIFO，USART1 TXE ISR 消费字节。不要把编码或阻塞发送重新塞入捕获 ISR。

## 加入工程并构建

1. 把 `User/`、`Hardware/`、`system/` 中当前 `.c/.h` 加入你的标准外设库工程。
2. include path 至少包含上述目录和标准库头文件目录。
3. 确认启动文件将 `TIM2_IRQHandler`、`TIM3_IRQHandler`、`USART1_IRQHandler` 链接到当前实现。
4. 使用板级工程既有的 ARM 编译/链接/烧录流程。仓库没有统一的 Keil、IAR 或 Makefile 命令，因此文档不伪造跨环境通用命令。
5. 编译警告中若出现重复 IRQ 定义，应删除板级旧实现而不是改名绕过；若缺少 `stm32f10x_*`，先修复标准库与芯片宏配置。

## 配置与协议

采集默认：80..65535 us、sync 8000 us、最少 50 pulse；ready queue 是 4 个后备槽的哨兵环，有效容量为 3 帧。可通过 `RF_Capture_SetFilter` 修改过滤参数。完整 packet：

```text
AA 55 | count_lo count_hi | pulse0_lo pulse0_hi ... | xor
```

长度与 pulse 都是 LE16，末字节从长度低字节起逐字节 XOR。`rf_proto_crc8` 是 legacy 名称，不是 CRC-8 多项式。

UART 入 FIFO 前会验证 frame、输出容量和剩余空间。空间不足时 `RF_Uart_SendFrame` 返回 0；`RF_Capture_ProcessLoop` 保留同一 `frame_work`、标记 `has_pending_tx`，并在后续轮次优先重试，成功前暂停 dequeue 后续 ready frame。代码没有 `DroppedFrames` 或 `g_uart_tx_drop_frames` 计数。ready queue 满时拒绝新到捕获帧，不推进 `tail`，因此不保证保留最新数据。

## 板端验收

1. 无 RF 输入时固件应持续运行，PA9 不应连续乱码。
2. 触发 EV1527 遥控，PA0 应出现重复 pulse；一个可解码帧通常约 50 pulse。
3. PA9 应输出 `AA 55` 开头、总长度为 `5 + count*2` 的 packet。
4. 手算/脚本验证末字节 XOR，并确认 count/pulse 是小端。
5. 与 RK3568 接通后按 [主控接入教程](Hardware/MASTER_INTEGRATION.md) 观察 `/dev/rf433` 与 driver stats。

成功标准不是“编译通过”，而是 PA0 间隔合理、PA9 packet 合法且主控能收到完整帧。

## 排错

- PA0 没波形：检查接收模块供电、DATA 引脚、天线与共地。
- PA0 有波形但 PA9 无 packet：检查 sync/min-frame 过滤、TIM2 时钟与 IRQ。
- PA9 乱码：检查 9600 8N1、系统时钟、电平和 TX/RX 是否交叉。
- 偶发少帧：高负载时 ready queue 会拒绝新到帧；TX FIFO 暂满会令主循环重试同一待发帧并暂停后续 dequeue。用分析仪和主控 seq 定位，不要查已删除的计数变量。
- pulse 整体倍数错误：优先复核 APB/TIM 时钟和 prescaler。

## 进一步阅读与验证边界

完整原理见 [STM32 深入指南](../docs/project2_hardware_deep_dive.md)，协议见 [协议分册](../docs/project2_hardware_deep_dive_03_protocol_timer_and_aux.md)。仓库的主机 CTest 覆盖协议契约，不覆盖 ARM 编译、真实中断时序和 UART 电气；这些必须在目标板验收。
