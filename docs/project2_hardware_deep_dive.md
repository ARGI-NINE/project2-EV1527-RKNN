# STM32 RF 采集固件总览

本系列面向需要维护 `project2_hardware` 的固件开发者。目标是从初始化入口追到 TIM2 捕获、队列、AA55 封包和 USART1 发送，并能用目标板做最小验收。

## 前置条件

- STM32F103 与 STM32F10x 标准外设库工程；本目录不是独立 CMake 工程。
- 433 MHz 接收模块数字输出接 `PA0`，USART1 `PA9` 接主控 RX，`PA10` 可接主控 TX，双方共地。
- 串口配置固定为 9600、8 数据位、无校验、1 停止位、无流控。

## 初始化和主循环

`User/main.c` 依次调用 `RF_Capture_Init`、`RF_Uart_Init`、`Timer_Init`，主循环只执行 `RF_Capture_ProcessLoop`。中断负责快速记录时间戳/搬移字节，主循环负责取出完整帧并排队发送；这是 ISR 与慢路径的所有权边界。

## 数据流

```text
PA0 双边沿
  -> TIM2/CC1 + overflow 组合 64-bit 时间戳
  -> 相邻时间戳差转为 uint16_t 微秒
  -> 当前帧（默认 80..65535 us，sync >= 8000 us，至少 50 pulse）
  -> 4 后备槽 SPSC ready queue（哨兵环有效容量 3 帧；满时拒绝新到帧）
  -> RF_Uart_SendFrame
  -> AA55 + LE16 count/pulses + XOR
  -> 2304-byte USART1 TX FIFO
  -> TXE 中断逐字节发送
```

`RF_Capture_SetFilter` 可修改过滤参数。默认 idle flush 由 sync 阈值计算并限制在 12..100 ms；超长间隔饱和到 65535 us。ready queue 满时保持已排队的 3 帧并拒绝新到帧，不承诺“最新帧优先”。`RF_Uart_SendFrame` 在编码失败或 FIFO 空间不足时返回 0；FIFO 暂满时，主循环保留同一待发帧并在后续轮次重试，也没有已删除的 `DroppedFrames`/`g_uart_tx_drop_frames` 计数器。

## 协议词汇

算法是一个字节的 XOR checksum。`rf_proto_crc8` 是历史函数名，不应据此推断 CRC-8 多项式。完整细节与 golden vector 见 [协议与定时器分册](project2_hardware_deep_dive_03_protocol_timer_and_aux.md)。

## 构建、烧录和观察

1. 将 `User/`、`Hardware/`、`system/` 中源文件加入现有标准外设库工程，并确保启动文件定义 TIM2、TIM3、USART1 中断入口。
2. 使用项目自己的 ARM 工具链构建并烧录；本仓库没有可移植的独立固件构建脚本，不能声称某条通用命令适配所有 IDE/链接脚本。
3. 逻辑分析仪同时观察 PA0 与 PA9。按键发射一次 EV1527 时，PA0 应出现重复脉冲；PA9 应出现以 `AA 55` 开头的 9600 8N1 字节。
4. 解码长度字段并确认总字节数为 `2 + 2 + pulse_count*2 + 1`，末字节等于从长度字段开始的 XOR。

成功标准：固件持续运行，捕获输入不会在 ISR 中长时间发送；完整帧可在 PA9 看到，且主控/协议测试能解码。无数据时按 [采集与 UART 分册](project2_hardware_deep_dive_02_capture_and_uart.md) 的顺序排查。

## 文档路线

1. [初始化、资源和配置](project2_hardware_deep_dive_01_overview.md)
2. [捕获、队列与 UART](project2_hardware_deep_dive_02_capture_and_uart.md)
3. [协议、Timer 与辅助发送](project2_hardware_deep_dive_03_protocol_timer_and_aux.md)
4. [当前函数索引](project2_hardware_deep_dive_04_function_index.md)
5. [接入 RK3568 主控](../project2_hardware/Hardware/MASTER_INTEGRATION.md)

## 已验证与未验证

主机 CTest 覆盖 AA55/LE16/XOR 与解析恢复；代码审阅确认队列/FIFO 的容量检查仍在。没有 ARM 工具链和真实板卡时，不能证明定时器频率、NVIC 优先级、电气连接或 ISR 时序。板端观察是发布前必要步骤。
