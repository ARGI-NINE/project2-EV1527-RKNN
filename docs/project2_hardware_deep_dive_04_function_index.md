# STM32 固件当前函数索引

本页是定位表，不复制完整实现。符号以当前头文件/源码为准；调用关系只列生产入口和明确的辅助入口。

## 生产调用链

```text
main
├─ RF_Capture_Init
├─ RF_Uart_Init
├─ Timer_Init
└─ loop: RF_Capture_ProcessLoop
   └─ RF_Uart_SendFrame
      ├─ rf_proto_encode
      └─ RF_Uart_SendArray

TIM2_IRQHandler -> RF_Capture_TIM2_IRQHandler
TIM3_IRQHandler -> RF_Capture_Tick1msHandler
USART1_IRQHandler -> TX FIFO drain / RX byte latch
```

## `Hardware/RF_Capture.*`

| 公共符号 | 作用 | 当前生产调用者 |
|---|---|---|
| `RF_Capture_Init` | 初始化状态、PA0、TIM2 和默认过滤 | `main` |
| `RF_Capture_SetFilter` | 修改 pulse/sync/min-frame 参数 | 无固定生产调用者，公开配置 API |
| `RF_Capture_ProcessLoop` | 主循环取 ready frame 并排队 UART | `main` |
| `RF_Capture_Tick1msHandler` | idle flush 时间推进 | `TIM3_IRQHandler` |
| `RF_Capture_TIM2_IRQHandler` | TIM2 捕获/overflow 实现 | `TIM2_IRQHandler` |
| `RF_Capture_GetLastIntervalUs` | 读取最近间隔 | 公开诊断 API，生产 main 未调用 |

私有 `RF_Capture_EnqueueFrame`/`DequeueFrame` 维护哨兵式 SPSC 队列：4 个后备槽、有效容量 3 帧，满时拒绝新到帧。生产代码由 `RF_Capture_DetectPulse` 形成帧；`TicksToUs`/`BuildStampTicks` 处理定时器换算。私有路径中已删除无意义的自检，但输入范围、容量和队列策略仍保留。

## `Hardware/RF_Uart.*`

| 公共符号 | 作用 | 说明 |
|---|---|---|
| `RF_Uart_Init` | PA9/PA10、USART1、NVIC | 9600 8N1 |
| `RF_Uart_SendByte` | 阻塞发送单字节 | 辅助 API，帧主链路不使用 |
| `RF_Uart_SendArray` | 原子式尝试放入 TX FIFO | 空间不足返回 0 |
| `RF_Uart_SendFrame` | 编码 frame 并入 FIFO | `RF_Capture_ProcessLoop` 调用 |
| `RF_Uart_GetRxData/GetRxFlag` | 读取 RX latch | 公开辅助 API |

私有 `RF_Uart_TxUsed`、`TxFree`、`TxEnqueue`、`TxDequeueByte` 管理 2304-byte ring。不存在 `g_uart_tx_drop_frames` 统计。

## `Hardware/RF_Protocol.*`

| 符号 | 作用 |
|---|---|
| `rf_proto_crc8` | 历史命名的逐字节 XOR |
| `rf_proto_encode` | AA55/LE16/XOR 编码 |
| `rf_proto_parser_init` | 初始化 parser 状态 |
| `rf_proto_parser_consume` | 单字节解析并在完整帧时返回成功 |

固件生产发送只调用 encoder；parser 用于复用/测试。协议最大 1024 pulse。

## `system/*` 与辅助模块

- `Timer_Init`、`Timer_NowUs`、`TIM3_IRQHandler`：时间基准和 1 ms tick。
- `Delay_us/ms/s`：忙等辅助 API，生产 main 未调用。
- `RF_Tx_Init/SetLevel/DelayUs/Replay/ReplayFrame`：GPIO 重放辅助 API，生产 main 未调用。

## 测试映射

协议由 master/PC/独立 `project2_test` 的 CTest 覆盖；仅独立测试固件额外提供 `RF_Capture_ConsumePulse`，供脉冲注入使用，生产固件没有该符号。STM32 外设函数没有主机可执行测试。修改 ISR、时钟、GPIO 或 UART 必须在目标板补做逻辑分析仪验收，见 [硬件总览](project2_hardware_deep_dive.md)。
