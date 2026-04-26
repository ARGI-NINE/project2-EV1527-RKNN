# project2_hardware

## 统一口径（2026-04-21）

- `project2_hardware` 是 STM32 端实时固件，仅负责采集 RF 脉冲并通过 UART 上传协议帧。
- `project2_master` 当前唯一 RF 用户态输入是 `/dev/rf433`；`/dev/ttyS9` 只是其底层 UART 物理链路的一部分。
- `hardware` 与 `master` 的实时对接链路是：`STM32 采集 -> USART1 -> RK3568 UART 基础链路 -> serdev -> /dev/rf433`。
- `pc_sim` 只是离线仿真/回归基线，用于字段真值对照，不是实机实时路径。
- `master` Qt 波形来自真实 `pulse_us`；这些值的上游事实源就是本目录上传的脉冲数组。
- `master` RF 在线状态依据驱动 `online` 位和真实 RF 帧，不能写成仅凭 gateway 进程启动就在线。
- 当前仍未完成端到端实机验收，因此本目录文档只能陈述接口契约与代码口径。

## 目录定位

`project2_hardware` 仅负责三件事：

1. `TIM2 CH1` 实时捕获 RF 脉冲宽度。
2. 依据帧规则进行前端筛选与分帧。
3. 通过 `USART1` 上送协议帧给上位机。

该目录不承担解码显示，不承担离线回放。

## 三端职责边界

| 端 | 职责 | 不做什么 |
|---|---|---|
| hardware (本目录) | STM32 实时采集 + 串口上传 | 不做 EV1527 最终判决，不做 UI |
| master | RK3568 实时接收 `/dev/rf433` + 解析 + 展示 | 不做 WAV/回放模拟入口 |
| pc_sim | 离线仿真与回归基线 | 不作为板端部署路径 |

## 实时链路语义

```text
RF 前端脉冲 -> STM32 TIM2 捕获 -> STM32 USART1 -> RK3568 UART 基础链路 -> serdev 上层 RF 驱动 -> /dev/rf433 帧接口 -> master 解码展示
```

本目录位于该链路左侧，输出的是原始脉冲帧，不直接面对 `/dev/rf433`。

## 串口参数（必须与 master 对齐）

- 外设：`USART1`（`PA9 TX`, `PA10 RX`）
- 波特率：`9600`
- 数据位：`8`
- 校验位：`None`
- 停止位：`1`
- 流控：`None`

## 协议与事件字段映射（hardware/master/pc_sim）

| 字段 | hardware 输出 | master 解释 | pc_sim 对应 | 要求 |
|---|---|---|---|---|
| `SYNC` | 固定 `0xAA 0x55` | 作为状态机起始字节 | 离线帧构造必须相同 | 必须一致 |
| `LEN` | `uint16` 小端，脉冲数 | 映射为 `rf_frame_t.len` / `pulse_count` | `pulse_count` | 必须一致 |
| `PAYLOAD` | `len*2` 字节，`uint16` 小端脉冲宽度 | 映射为 `rf_frame_t.pulse[]` / `pulse_us` | 离线脉冲数组 | 必须一致 |
| `CRC` | `XOR(LEN + PAYLOAD)` | 驱动侧校验，不通过则丢弃 | 离线基线需复现同规则 | 必须一致 |
| `addr/key/conf` | 不产生 | C 解码后生成事件字段 | 仿真链也生成同名字段 | 语义一致 |
| `seq` | 不产生 | `/dev/rf433` 读帧序号 | 回放序号 | 允许实现差异 |
| `timestamp` | 不产生 | 驱动时间戳 | 离线时间戳/WAV 秒位 | 允许实现差异 |

### 允许差异项

- `seq` 可在不同进程、不同运行中重新计数。
- `timestamp` 来源可不同（驱动时间或离线时间轴）。
- `confidence` 允许因样本窗口与统计口径存在轻微差异，但解码码值应一致。

## 与当前代码一致的实现点

- `Hardware/RF_Capture.c` 采集到的真实 `pulse_us` 进入分帧逻辑。
- `Hardware/RF_Protocol.c` 与板端解析侧保持同样的 `AA55/LEN/PAYLOAD/XOR CRC` 规则。
- `Hardware/RF_Uart.c` 通过 `rf_proto_encode()` 编码后经 `USART1` 发出。

## TIM2 IRQ Bridge 宏约定

`Hardware/RF_Capture.c` 默认导出：

```text
TIM2_IRQHandler() -> RF_Capture_TIM2_IRQHandler()
```

如工程已有自定义 `TIM2_IRQHandler`，可定义编译宏关闭默认桥接：

- `RF_CAPTURE_DISABLE_TIM2_IRQ_BRIDGE`

关闭后由外部 ISR 手动调用 `RF_Capture_TIM2_IRQHandler()`。

## 当前验证边界

- 已核对：脉冲 us 采集、协议编码规则、USART1 输出路径、三端字段对应。
- 未核对完成：真实接线、波形质量、长时间稳定性、与 `master` 的端到端实机联调。
- 不能写成：`hardware` 与 `master` 已完成整机 E2E 验收。

## 排错清单

- 无数据上送：先查 `PA9/PA10/GND` 接线与 3.3V 电平。
- master 报 CRC 错误：核对 `LEN` 字节序与 XOR 覆盖范围。
- 帧长度异常：确认 `MinFramePulses` 与同步低电平阈值配置。
- 中断冲突：若项目已有 `TIM2_IRQHandler`，启用 `RF_CAPTURE_DISABLE_TIM2_IRQ_BRIDGE`。
- 偶发丢帧：检查主循环负载与串口发送背压。

## 相关文档

- `Hardware/MASTER_INTEGRATION.md`：本项目与板端实时接口契约、字段映射与验证边界。
- 板端口径摘要：用户态 RF 入口是 `/dev/rf433`，UART 只是其底层物理链路。
- 离线对照摘要：`pc_sim` 只用于回放、对比和回归，不作为板端部署路径。
