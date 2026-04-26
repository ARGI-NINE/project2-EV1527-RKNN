# rf433_drv v2 设计说明

## 统一口径（2026-04-21）

- 本设计文档只描述 `master` 实时驱动设计，不描述 `pc_sim` 离线运行能力。
- `master` 当前唯一 RF 用户态输入是 `/dev/rf433`，底层路径是 `UART -> serdev -> /dev/rf433`。
- `hardware` 上送 UART 协议帧，字段语义需与 `pc_sim` 真值表保持对应。
- `online` 是驱动维护的运行位，供用户态判定链路是否真的有合法帧进入。
- 当前仅完成设计与代码口径核对，未完成端到端实机验收。

## 设计目标

将 RF 串口字节流解析前移到内核，向用户态提供稳定帧接口 `/dev/rf433`，保证 `master` 用户态只处理“帧”，不处理碎片字节。

## 分层原则

1. UART 物理层仍由 SoC 串口基础设施负责。
2. `rf433_drv` 作为 serdev client 实现上层协议状态机。
3. 用户态通过 `/dev/rf433` 读帧，形成实时业务入口。

## 协议状态机

状态：`SYNC0 -> SYNC1 -> LEN0 -> LEN1 -> PAYLOAD -> CRC`

关键规则：

- `SYNC` 固定 `0xAA 0x55`
- `LEN` 为 LE16，范围 `1..RF433_MAX_PULSES`
- `CRC` 为 `XOR(LEN + PAYLOAD)`

CRC 错误或长度错误立即复位状态机并累计统计。

## 队列策略

帧队列为 `kfifo`。

- 现行策略：队列满时丢弃最旧帧（drop oldest），保留最新实时帧。
- 对应统计：`drop_cnt` 递增。

该策略与当前代码保持一致，优先保证最新实时数据可被用户态消费。

## online 位设计

- 在完整合法帧成功入队后，驱动置 `online=true`。
- 在超时窗口内没有新的完整帧时，驱动置 `online=false`。
- 因此 `online` 反映的是“驱动是否持续看到真实 RF 帧”，不是“进程是否启动”。

## write_wakeup 设计

驱动为 RX-only，不维护待发送缓存。

- `write_wakeup` 使用显式空实现 `rf433_write_wakeup_nop()`。
- 不再绑定通用 helper 以规避潜在递归/语义歧义。

## 三端字段映射（设计口径）

| 字段 | hardware | driver/master | pc_sim | 约束 |
|---|---|---|---|---|
| 帧同步 | `AA55` | 同步头识别 | 离线帧同步 | 必须一致 |
| 长度 | LE16 脉冲数 | `pulse_count` | `pulse_count` | 必须一致 |
| 脉冲负载 | LE16 us | `pulse[]` | `pulse[]` | 必须一致 |
| CRC | XOR | 内核校验 | 基线校验 | 必须一致 |
| 序号/时间 | 无 | `seq/timestamp_ns` | 回放序号/离线时间 | 允许差异 |

## 与 master 用户态契合点

用户态默认路径统一为 `/dev/rf433`。

- `linux_app/rf_source.h`：`RF_SOURCE_PATH=/dev/rf433`
- `linux_app/main.c`：帮助文本和默认参数沿用该路径，并输出真实 `pulse_us=`
- `qt_gui`：参数校验与默认值沿用该路径，在线状态读取 `online`，波形读取真实 `pulse_us`

## 当前验证边界

- 已核对：状态机、队列策略、`online` 位、用户态路径契合点。
- 未核对完成：硬件接线噪声、长时间运行、整机端到端联调。
- 因此不能在设计文档中表述为“驱动已完成实机验收”。

## 排错清单

- `frame_ok` 不增长：检查 serdev 绑定与中断输入。
- `len_err` 高：检查下位机 `LEN` 小端编码。
- `drop_cnt` 高：说明消费端不足，需调优读取频率。
- `online` 长期为 0：说明未看到持续合法帧，不要仅凭进程启动判定在线。
- 解析卡死疑虑：核查半帧超时复位逻辑是否触发。

## 互引

- `README.md`锛氭湰鐩綍涓嬬殑椹卞姩浣跨敤涓庤竟鐣屾憳瑕併€?
- `project2_master` 鏍圭洰褰?`README.md`锛氭暣浣?master 杩愯鍙ｅ緞涓庤竟鐣屾憳瑕併€?
- `docs/project2_iot_design.md`锛歁aster 瀹炴椂 IoT 璁捐鍙ｅ緞涓庡瓧娈垫槧灏勬憳瑕併€?
