# STM32 固件分册 2：捕获、队列与 UART

## 目标

沿真实调用链理解 `RF_Capture.c` 与 `RF_Uart.c`，并能区分有意义的容量/并发保护和已删除的无效计数/私有不可能分支。

## 捕获路径

`TIM2_IRQHandler -> RF_Capture_TIM2_IRQHandler` 处理更新事件和 CC1 捕获。代码把 overflow count 与 CCR1 合为单调时间戳，再由 `RF_Capture_TicksToUs` 计算相邻边沿间隔。生产固件由 `RF_Capture_DetectPulse` 根据范围、同步阈值和最少 pulse 数形成帧；`RF_Capture_ConsumePulse` 只存在于独立 `project2_test` 的注入实现，不是生产调用链。

空闲期间 `RF_Capture_Tick1msHandler` 触发 idle flush 检查。idle 阈值由 sync 微秒数换算并夹在 12..100 ms，避免只等待下一个遥远边沿。

完整帧进入哨兵式 ready queue。底层数组有 4 个后备槽，但用 `head` 的下一位置等于 `tail` 表示满，因此最多保留 3 帧。满时 `RF_Capture_EnqueueFrame` 直接拒绝新到帧，不推进 `tail`，也不承诺 freshness；已有 3 帧仍按入队顺序等待主循环处理。

## 发送路径

`RF_Capture_ProcessLoop` 反复 dequeue，调用 `RF_Uart_SendFrame`：

1. `rf_proto_encode` 检查 frame、pulse 数和输出容量。
2. 编成 `AA55 + LE16 count + LE16 pulses + XOR`。
3. `RF_Uart_TxEnqueue` 检查空指针、空长度、packet 是否小于 FIFO、剩余空间是否足够。
4. 成功后开启 TXE 中断；`USART1_IRQHandler` 每次取一个字节，FIFO 空时关闭 TXE。

TX FIFO 为 2304 字节，最大协议 packet 为 `2 + 2 + 1024*2 + 1 = 2053` 字节。保留容量检查很重要：它们保护外部 frame 大小和 ISR/主循环共享缓冲。源码不再维护只写不读的 `DroppedFrames` 或 `g_uart_tx_drop_frames`。FIFO 空间不足时，`RF_Capture_ProcessLoop` 把当前帧保存在 `frame_work`，设置 `has_pending_tx` 并返回；后续主循环先重试同一帧，成功前暂停 dequeue 后面的 ready frame，不会把这次待发帧当场丢掉。

## 接线与观察

- RF 接收模块 DATA -> PA0（上拉输入）。
- STM32 PA9/USART1 TX -> RK3568 UART RX；如需回传，RK3568 TX -> PA10。
- 3.3 V 电平并共地；若模块是 5 V 输出，应先做电平匹配。
- 串口固定 9600 8N1，无流控。

逻辑分析仪步骤：触发一次遥控，先确认 PA0 有约 50-pulse 的重复帧，再解码 PA9。若 PA0 正常而 PA9 无数据，检查最少 pulse/sync 过滤和 ready queue；若 PA9 有乱码，检查波特率、时钟和电平；若 `AA55` 正确但末字节错误，按 XOR 范围检查主控解析器。

## 成功标准与限制

单次完整 EV1527 帧应产生一个协议 packet；连续帧不应阻塞 ISR。高负载时 ready queue 可拒绝新到帧，UART FIFO 暂满则让同一待发帧留在主循环重试。当前固件不提供累计 capture drop 计数，因此不能从已删除变量推导丢包数，需用逻辑分析仪或主控序号/统计判断。

主机 CTest 验证协议而非电气和中断时序。真实板卡必须复核 TIM2 频率、PA0 边沿与 USART1 输出。
