# Hardware-Master Integration Notes

## 统一口径（2026-04-21）

- 本文档定义 `project2_hardware` 与 `project2_master` 的实时接口契约。
- `pc_sim` 只是离线真值/回归基线，不是这条实时集成链路的一部分。
- `master` 当前 RF 唯一用户态输入是 `/dev/rf433`；底层链路是 `UART -> serdev -> /dev/rf433`。
- `hardware` 只负责持续产出合法 UART 协议帧，不直接面对 Qt 或 `/dev/rf433`。
- `master` RF 波形展示依赖真实 `pulse_us`，这些值必须来自本契约中的真实脉冲负载。
- `master` RF 在线状态依赖驱动 `online` 位和真实 RF 帧，不依赖网关进程是否已经启动。
- 当前未完成端到端实机验收，因此本文档只说明接口契约和验证边界。

## 目的

本文档定义 `project2_hardware` 与 `project2_master` 的实时接口契约，并说明 `project2_pc_sim` 仅作为离线对照。

## 三端职责边界

| 端 | 输入 | 输出 | 边界声明 |
|---|---|---|---|
| hardware | RF 模块脉冲（TIM2 CH1） | UART 协议帧 | 仅做实时采集与上传 |
| master | `/dev/rf433` 帧接口 | 解码事件、UI 展示 | 仅做板端实时链路 |
| pc_sim | WAV/离线脉冲数据 | 仿真事件 | 仅做回归基线 |

## 链路定义

链路必须按下列层级实现并联调：

```text
STM32 TIM2 捕获 -> USART1 -> RK3568 UART 物理链路 -> serdev 上层 RF 驱动 -> /dev/rf433 帧接口 -> master 用户态解码
```

说明：`/dev/rf433` 是 master 用户态唯一接入点；hardware 侧只负责持续产出合法协议帧。

## 串口参数契约

- `USART1` / `9600` / `8N1` / 无流控
- 电平：3.3V
- 接线：`PA9(TX)->RK3568 RX`, `PA10(RX)<-RK3568 TX`, `GND 共地`

## 协议与事件字段映射

| 类别 | hardware 字段 | master 字段 | pc_sim 字段 | 一致性要求 |
|---|---|---|---|---|
| 帧头 | `0xAA 0x55` | parser 同步字节 | 离线帧同步字节 | 必须一致 |
| 长度 | `LEN0/LEN1` (LE16) | `rf_frame_t.len` / `pulse_count` | `pulse_count` | 必须一致 |
| 负载 | `pulse[i]` LE16 us | `rf_frame_t.pulse[i]` / `pulse_us[i]` | `pulse[i]` | 必须一致 |
| 校验 | `XOR(LEN+PAYLOAD)` | 驱动校验并统计错误 | 基线校验同规则 | 必须一致 |
| 解码码值 | 无 | `addr/key` | `addr/key` | 语义一致 |
| 置信度 | 无 | `conf` | `conf` | 允许口径差异 |
| 序号 | 无 | 帧序号 `seq` | 回放序号 | 允许实现差异 |

### 允许差异项

- `seq` 起点与递增节奏可不同。
- `timestamp` 与运行时统计字段可不同。
- `conf` 允许因窗口策略不同出现小幅偏差。

## 与当前 master 行为的对齐点

- `master` 用户态只消费 `/dev/rf433`，不直接消费 UART 设备节点。
- `rf_gateway` 输出真实 `pulse_us=`，要求上游负载是正确的 us 脉冲数组。
- Qt 在线状态依赖驱动 `online` 位和真实 `[RF]` 帧，因此实时帧必须稳定、连续、可被驱动完整解析。

## TIM2 IRQ Bridge 约束

`RF_Capture.c` 默认导出 `TIM2_IRQHandler` 并转发至 `RF_Capture_TIM2_IRQHandler`。

如项目中断向量由外部文件统一管理，定义宏：

- `RF_CAPTURE_DISABLE_TIM2_IRQ_BRIDGE`

然后在外部 ISR 中手动桥接。

## 当前验证边界

- 已核对：串口参数、协议字段、三端字段对应、`master` 输入口径。
- 未核对完成：实际接线后的长时间稳定收发、驱动在线状态时序、Qt 端到端实时展示。
- 不能写成：`hardware` 与 `master` 已完成实机端到端验收。

## 排错清单

- master 收不到帧：先确认驱动已注册 `/dev/rf433`。
- LEN/CRC 异常：检查 LE16 与 XOR 范围。
- 间歇性丢帧：查看驱动 `drop_cnt` 与下位机输出节拍。
- TIM2 重定义冲突：检查是否忘记定义 `RF_CAPTURE_DISABLE_TIM2_IRQ_BRIDGE`。
- pc_sim 结果与板端不一致：优先以板端实时链路为准。

## 本地参考

- 项目根 `README.md` 摘要：本项目职责边界、串口参数与常见排错项。
- 板端摘要：用户态通过 `/dev/rf433` 接收协议帧，驱动在线状态与真实 RF 帧共同决定在线显示。
- 驱动接口摘要：上位侧消费的是 `AA55/LEN/PAYLOAD/XOR CRC` 帧语义，而不是直接耦合某个兄弟目录路径。
- 离线对照摘要：`pc_sim` 仅做 WAV/回放基线，不属于这条实时集成链路。
