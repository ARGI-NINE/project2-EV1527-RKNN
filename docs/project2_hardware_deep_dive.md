# project2_hardware 专题阅读入口

## 1. 这一组文档只负责什么

这一组文档只负责讲清楚 `project2_hardware/` 目录的真实代码主线，不越界去替代：

- `project2_master` 的驱动深读
- `project2_master` 的 MQTT / RTSP 专题
- `project2_pc_sim` 的离线仿真文档

换句话说，这里只回答一个问题：

> STM32 这部分代码，究竟怎样把 RF 前端脉冲整理成可以上送到 RK3568 的帧数据。

## 2. 本专题的完成口径

- 本专题基于当前仓库静态代码编写。
- 本专题确认的是模块职责、主链结构、关键参数和调用关系。
- 本专题不宣称已经完成实机接线、串口波形质量验证或与 RK3568 的整机联调。

这点必须先钉牢，否则后面的任何“看起来已经完整”的调用链都容易被误读成“实机已经跑通”。

## 3. 当前代码里的真实硬件主链

如果你只记一条链，请记下面这一条：

```text
RF 前端脉冲
-> PA0 / TIM2_CH1
-> RF_Capture_TIM2_IRQHandler()
-> RF_Capture_DetectPulse()
-> CurrentFrame / ReadyQueue
-> RF_Capture_ProcessLoop()
-> RF_Uart_SendFrame()
-> rf_proto_encode()
-> RF_Uart_TxEnqueue()
-> USART1_IRQHandler()
-> PA9(TX)
-> RK3568 串口
-> serdev
-> /dev/rf433
```

这条链已经说明了两个很重要的事实：

1. `project2_hardware` 的职责止步于“采集、整理、编码、上送”。
2. EV1527 业务解码、UI 展示、MQTT 发布都不在这个目录里。

### 3.1 `main()` 只负责把主链搭起来

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

这个 `main()` 很短，但它把调用关系定死了。初始化顺序是 `RF_Capture_Init()`、`RF_Uart_Init()`、`Timer_Init()`，之后主循环里没有业务分支，没有协议解析，也没有串口忙等，只有 `RF_Capture_ProcessLoop()` 一条持续泵送路径。也就是说，实时边沿采集主要发生在 `TIM2` 中断里，主循环的职责是把已经形成的帧快照或 ready 队列继续送去 UART。

边界也因此非常清楚：如果你在这个目录里找 `addr`、`key`、`confidence`，在 `main()` 这一层肯定找不到，因为它只维护“采集前端 + 上送前端”，不维护上层语义。

### 3.2 `RF_Capture_TIM2_IRQHandler()` 负责把边沿变成脉冲宽度

代码来源：`project2_hardware/Hardware/RF_Capture.c::RF_Capture_TIM2_IRQHandler()`

```c
void RF_Capture_TIM2_IRQHandler(void) {
    uint8_t had_cc1 = (TIM_GetITStatus(TIM2, TIM_IT_CC1) != RESET) ? 1u : 0u;

    if (had_cc1 != 0u) {
        uint16_t ccr = (uint16_t)TIM_GetCapture1(TIM2);
        uint16_t cnt_now = (uint16_t)TIM_GetCounter(TIM2);
        uint8_t update_pending = (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) ? 1u : 0u;
        uint32_t overflows_for_capture = g_rf_capture.Tim2OverflowCount;
        uint64_t stamp_ticks = 0ull;

        /*
         * Reliable extended timestamp:
         * - TIM2 update IRQ maintains Tim2OverflowCount for every 0xFFFF wrap.
         * - If UIF is pending and CCR <= current CNT, overflow happened before capture,
         *   so this capture belongs to the next timer cycle and needs +1 overflow.
         * - This avoids mis-decoding long gaps (>1 wrap) as short pulses.
         */
        if (update_pending != 0u && ccr <= cnt_now) {
            overflows_for_capture++;
        }

        stamp_ticks = RF_Capture_BuildStampTicks(overflows_for_capture, ccr);
        if (g_rf_capture.HasLastCaptureStamp == 0u) {
            g_rf_capture.LastCaptureStampTicks = stamp_ticks;
            g_rf_capture.HasLastCaptureStamp = 1u;
        } else {
            uint64_t delta_ticks = stamp_ticks - g_rf_capture.LastCaptureStampTicks;
            uint16_t pulse_us = 0u;
            if (delta_ticks > 0xFFFFFFFFull) {
                delta_ticks = 0xFFFFFFFFull;
            }
            g_rf_capture.LastCaptureStampTicks = stamp_ticks;
            pulse_us = RF_Capture_TicksToUs((uint32_t)delta_ticks);
            g_rf_capture.LastIntervalUs = pulse_us;
            RF_Capture_DetectPulse(pulse_us);
            g_rf_capture.LastEdgeMs = g_rf_capture.MsTick;
        }

        TIM_ClearITPendingBit(TIM2, TIM_IT_CC1);
        TIM2->CCER ^= TIM_CCER_CC1P;
    }

    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
        g_rf_capture.Tim2OverflowCount++;
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        TIM_ClearFlag(TIM2, TIM_FLAG_Update);
    }
}
```

这段中断的核心不是“读一下 CCR 就完了”，而是把 16 位定时器扩成一个可靠的长时间轴。`Tim2OverflowCount` 记录每次 `0xFFFF` 回绕，`update_pending` 加上 `ccr <= cnt_now` 这一支专门修正“捕获发生在回绕之后、但 UIF 还没清掉”的边界，这样长间隔不会被误判成一个很短的 pulse。第一条边沿只建立 `LastCaptureStampTicks` 基准，不产出脉冲；从第二条边沿开始才会计算 `delta_ticks -> pulse_us`。

后半段的动作链是实际主线：`RF_Capture_TicksToUs()` 把 tick 差转成微秒，写进 `LastIntervalUs` 后立刻调用 `RF_Capture_DetectPulse(pulse_us)`，也就是把单个脉冲喂给当前帧构建器。`LastEdgeMs = MsTick` 给主循环的空闲刷帧判定提供时间基准。最后 `TIM2->CCER ^= TIM_CCER_CC1P` 每次翻转捕获极性，所以它不是只看上升沿或下降沿，而是交替采完整个脉冲序列。

### 3.3 主循环怎样把当前帧和 ready 队列继续上送

代码来源：`project2_hardware/Hardware/RF_Capture.c::RF_Capture_ProcessLoop()`

```c
void RF_Capture_ProcessLoop(void) {
    static rf_frame_t frame_work;
    static uint8_t has_pending_tx = 0u;
    uint32_t now_ms = g_rf_capture.MsTick;
    uint32_t edge_ms = g_rf_capture.LastEdgeMs;
    uint16_t len_snapshot = g_rf_capture.CurrentFrame.len;
    uint8_t has_snapshot = 0u;

    if (has_pending_tx != 0u) {
        if (RF_Uart_SendFrame(&frame_work) != 0u) {
            has_pending_tx = 0u;
        } else {
            return;
        }
    }

    if (
        len_snapshot >= g_rf_capture.MinFramePulses &&
        (now_ms - edge_ms) > (uint32_t)g_rf_capture.IdleFlushMs
    ) {
        frame_work.len = len_snapshot;
        memcpy(frame_work.pulse, g_rf_capture.CurrentFrame.pulse, (size_t)len_snapshot * sizeof(uint16_t));

        __disable_irq();
        if (g_rf_capture.CurrentFrame.len == len_snapshot && g_rf_capture.LastEdgeMs == edge_ms) {
            g_rf_capture.CurrentFrame.len = 0u;
            g_rf_capture.LastEdgeMs = g_rf_capture.MsTick;
            has_snapshot = 1u;
        }
        __enable_irq();

        if (has_snapshot != 0u) {
            if (RF_Uart_SendFrame(&frame_work) == 0u) {
                has_pending_tx = 1u;
                return;
            }
        }
    }

    while (1) {
        uint8_t has_frame = 0u;
        has_frame = RF_Capture_DequeueFrame(&frame_work);
        if (has_frame == 0u) {
            break;
        }
        if (RF_Uart_SendFrame(&frame_work) == 0u) {
            has_pending_tx = 1u;
            break;
        }
    }
}
```

这段代码把中断侧和 UART 侧接上了。`has_pending_tx` 说明 `RF_Uart_SendFrame()` 的成功语义不是“整帧已经发完”，而是“已经成功塞进发送 FIFO”；如果上次因为 FIFO 空间不足返回 `0`，主循环会优先拿着同一份 `frame_work` 重试，不会静默丢掉。中间那段 `len_snapshot + edge_ms` 逻辑则是在做“空闲刷帧”：只要当前帧脉冲数达到 `MinFramePulses`，并且距离上一条边沿已经超过 `IdleFlushMs`，就把 `CurrentFrame` 快照出来准备发。

`__disable_irq()` 到 `__enable_irq()` 之间那一小段非常关键，它保证“我拷出来的长度”和“最后边沿时间”在提交前没有被新的中断改掉；只有两者都没变，才把 `CurrentFrame.len` 清零并确认 `has_snapshot = 1`。后面的 `while (1)` 则负责继续冲刷 `ReadyQueue` 里已经完成筛选的帧，所以主循环实际上服务两条来源：一条是当前正在累积、但因空闲超时被刷出的 `CurrentFrame`，另一条是已经通过 `RF_Capture_EnqueueFrame()` 压进 ready 队列的完整帧。

### 3.4 `RF_Uart_SendFrame()` 和 `rf_proto_encode()` 把帧变成线上字节流

代码来源：`project2_hardware/Hardware/RF_Uart.c::RF_Uart_SendFrame()` / `project2_hardware/Hardware/RF_Protocol.c::rf_proto_encode()`

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

size_t rf_proto_encode(const rf_frame_t *frame, uint8_t *out, size_t out_capacity) {
    uint16_t i = 0u;
    uint16_t bytes = 0u;
    uint8_t crc = 0u;
    size_t total = 0u;

    if (frame == NULL || out == NULL) {
        return 0u;
    }
    if (frame->len == 0u || frame->len > RF_BUFFER_SIZE) {
        return 0u;
    }

    bytes = (uint16_t)(frame->len * 2u);
    total = (size_t)2u + 2u + bytes + 1u;
    if (out_capacity < total) {
        return 0u;
    }

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
    return total;
}
```

`RF_Uart_SendFrame()` 本身非常薄，只做三件事：判空、调用 `rf_proto_encode()`、再把编码好的字节包交给 `RF_Uart_SendArray()`。它返回 `0/1` 的语义也要读准：这里的成功不是“字节已经从 PA9 发完”，而是“编码成功且整个数据包已经入 TX FIFO”；真正逐字节推送发生在 `USART1_IRQHandler()` 里。也正因为如此，上一节 `RF_Capture_ProcessLoop()` 才需要 `has_pending_tx` 做重试。

`rf_proto_encode()` 则把线上格式写死成 `AA55 + LEN_LE + pulse[] + CRC`。边界条件都在函数头：`frame == NULL`、`out == NULL`、`frame->len == 0`、`frame->len > RF_BUFFER_SIZE`、输出缓冲区不够大，任何一种都会直接返回 `0`。长度字段按小端写在 `out[2]`、`out[3]`；每个 `pulse[i]` 也按小端拆成两个字节；最后的 CRC 明确是对 `LEN + PAYLOAD` 做 `rf_proto_crc8()`，并放在 `out[4 + bytes]`。这就是 STM32 端“帧对象 -> UART 字节流”的唯一正式编码路径。

## 4. 当前主链之外，还有哪些模块

`project2_hardware` 目录里还有一些不是当前主链的模块：

- `RF_Tx.c`：脉冲回放辅助模块，当前 `main()` 没有接入。
- `Delay.c`：阻塞式延时工具，当前主链没有调用。
- `RF_Protocol.c` 里的 parser：协议解析能力存在，但当前上送主线只用到编码侧。
- `Timer_NowUs()`：微秒时间查询接口存在，但当前主链主要依赖 `TIM3` 的 1ms tick 来做空闲刷帧判定。

所以，看代码时不要把“文件存在”自动理解成“主链正在使用”。

## 5. 推荐阅读顺序

### 第一步：先看总览

[project2_hardware_deep_dive_01_overview.md](project2_hardware_deep_dive_01_overview.md)

这一篇负责回答：

- 目录边界是什么
- `main()` 的初始化顺序是什么
- 哪些文件真的在主链上
- 哪些参数是当前行为的关键

### 第二步：再看采集与 UART 主线

[project2_hardware_deep_dive_02_capture_and_uart.md](project2_hardware_deep_dive_02_capture_and_uart.md)

这一篇负责从 `main()` 走到 `USART1_IRQHandler()`，把最重要的实时链讲透。

### 第三步：补齐协议、时基与辅助模块

[project2_hardware_deep_dive_03_protocol_timer_and_aux.md](project2_hardware_deep_dive_03_protocol_timer_and_aux.md)

这一篇负责把你在主链里见到、但来不及展开的公共协议、1ms 时基、延时工具和脉冲回放补全。

### 第四步：需要定位函数时查索引

[project2_hardware_deep_dive_04_function_index.md](project2_hardware_deep_dive_04_function_index.md)

这一篇不是叙事文，而是回查索引。

## 6. 读这组文档前，你需要先建立的三个判断

### 判断 1：这套固件不是业务解码层

这里输出的是脉冲帧，不是最终业务事件。最终业务上的 `addr`、`key`、`confidence` 在 `project2_master/linux_app` 才出现。

### 判断 2：这套固件的上行协议非常窄

它只关心一件事：把 `rf_frame_t` 编码成固定格式字节流，经 `USART1` 发出去。

### 判断 3：这组文档是 tutorial，不是宣传材料

这里会强调：

- 当前主链依赖了哪些中断和缓存。
- 哪些函数当前没接入主流程。
- 哪些能力只是预留，不该写成已完成。

## 7. 和其它文档簇的关系

读完本专题后，通常会继续往两个方向走：

### 方向 A：继续追到 RK3568 板端

建议接着读：

- [../project2_master/docs/project2_shared_protocol_deep_dive.md](../project2_master/docs/project2_shared_protocol_deep_dive.md)
- [../project2_master/docs/project2_master_driver_deep_dive.md](../project2_master/docs/project2_master_driver_deep_dive.md)
- [../project2_master/docs/project2_master_userland_deep_dive.md](../project2_master/docs/project2_master_userland_deep_dive.md)

### 方向 B：只想建立顶层项目认知

回到：

- [project2_full_guide.md](project2_full_guide.md)

### 关于 MQTT / RTSP

这两个话题不在本专题展开。只保留跳转入口：

- [../project2_master/docs/project2_iot_design.md](../project2_master/docs/project2_iot_design.md)
- [../project2_master/docs/rk3568_vision_dashboard.md](../project2_master/docs/rk3568_vision_dashboard.md)

## 8. 最后一句提醒

如果你读完后只记住一句话，那就记住这句：

> `project2_hardware` 的当前代码事实，是“RF 脉冲采集与 UART 协议上送前端”，不是“已经完成整机联调的 RF 子系统”。
