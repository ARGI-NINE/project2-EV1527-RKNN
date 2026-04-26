# project2 IoT 设计（master 实时运行口径）

## 统一口径（2026-04-21）

- `master` 是板端实时运行目录，`pc_sim` 只是离线仿真/回归基线。
- `master` 当前 RF 唯一用户态输入是 `/dev/rf433`，其底层链路是 `UART -> serdev -> /dev/rf433`。
- RF 页面和日志中的波形数据都来自真实 `pulse_us`，不是 UI 侧反推的假波形。
- RF 在线状态由驱动 `online` 位和真实 RF 帧共同表征，不以网关进程启动为在线判据。
- `master` 视觉当前仍未接入真实板侧状态，且本地视频文件输入已被禁用；本文档不把视觉页写成已接通。
- `hardware` 与 `master` 通过 UART 协议帧对接，字段语义需与 `pc_sim` 真值表对应。
- 当前尚未完成端到端实机验收，因此本文档只陈述设计和代码口径，不陈述 E2E 已通过。

## 1. 设计目标

在 RK3568 板端建立稳定实时链路：

```text
STM32 采集 -> UART 基础链路 -> serdev RF 驱动 -> /dev/rf433 -> 网关解码 -> UI/IoT 输出
```

其中，`/dev/rf433` 是 `master` 用户态默认且唯一实时输入。

## 2. 三端职责边界

| 端 | 输入 | 输出 | 约束 |
|---|---|---|---|
| hardware | RF 脉冲 | UART 协议帧 | 只做采集上传 |
| master | `/dev/rf433` 帧 | 解码事件 + 可视化 | 不提供模拟入口 |
| pc_sim | WAV/离线脉冲 | 仿真事件 | 不作为板端路径 |

## 3. 协议设计

协议帧：`AA55 + LEN(LE16) + PAYLOAD(len*2) + CRC(XOR)`

CRC 覆盖范围：从 `LEN0` 到 payload 末字节。

## 4. 字段映射与允许差异

| 维度 | hardware | master | pc_sim | 规则 |
|---|---|---|---|---|
| 协议同步 | `AA55` | 状态机同步 | 离线帧同步 | 必须一致 |
| 长度 | LE16 脉冲数 | `rf_frame.len` / `pulse_count` | `pulse_count` | 必须一致 |
| 负载 | LE16 脉冲数组 | `rf_frame.pulse[]` / `pulse_us` | 脉冲数组 | 必须一致 |
| 校验 | XOR | 驱动校验 | 离线校验 | 必须一致 |
| 解码输出 | 无 | `addr/key/conf` | `addr/key/conf` | 语义一致 |
| 序号/时间 | 无 | `seq/timestamp_ns` | replay 序号/离线秒位 | 允许差异 |

允许差异项：

- `seq` 起点、增量节奏。
- `timestamp` 采样来源。
- `confidence` 在不同窗口策略下的小幅波动。

### 4.1 统计字段语义（master）

| 字段 | 语义 | 典型来源 |
|---|---|---|
| `parseErrors` | 仅表示解析失败计数，不包含驱动丢帧；可由 `crc_err + len_err` 构成 | `[DRV_STATS] crc_err/len_err`、明确解析失败日志 |
| `driverDropFrames` | 仅表示驱动层丢帧计数 | 当前前端实现仅由 `[DRV_STATS] drop` 更新；`[RF_STATS] drv_drop` 仅记录日志，不写入该字段 |
| `dropCount` | 用户态解码阶段丢弃计数 | `rf_gateway` 解码日志 |
| `online` | 驱动在线位 | `[DRV_STATS] online` / `RF433_IOC_GET_STATUS` |

## 5. 组件职责

| 组件 | 职责 |
|---|---|
| `linux_driver/rf433_drv.c` | serdev 接收、协议解析、导出 `/dev/rf433`、维护 `online` 位 |
| `linux_app/rf_source.c` | 打开并校验 `/dev/rf433` |
| `linux_app/rf_epoll.c` | epoll 读取帧与回调分发 |
| `linux_app/main.c` | 输出 `[RF]` 与 `[DRV_STATS]`，携带真实 `pulse_us` |
| `linux_app/rf_decode*.c` | EV1527 解码与统计 |
| `qt_gui` | 启动网关并展示实时结果 |

## 6. 运行基线

```bash
./rf_gateway --rf-input /dev/rf433
./rf_dashboard_qt5 --rf-input /dev/rf433
```

## 7. 当前验证边界

- 已核对：输入路径限制、协议字段、在线位语义、真实 `pulse_us` 波形、vision 输入限制。
- 未核对完成：UART 实机链路、驱动在线超时、Qt 长时间运行、真实视觉状态通道接线。
- 不能写成：已完成整机端到端实机验收。

## 8. 排错清单

- `/dev/rf433` 打不开：检查驱动加载、设备权限、DTS 绑定。
- 解码不稳定：核查下位机脉冲质量与稳定分组参数。
- UI 无更新：检查网关 stdout 行是否被前端解析，是否包含真实 `pulse_us=`。
- 与 `pc_sim` 不一致：按实时链路优先级进行差异归因。

## 9. 互引

- `project2_master` 鏍圭洰褰?`README.md`锛氬疄鏃惰繍琛屽彛寰勬憳瑕併€?
- `project2_master` 鏍圭洰褰?`README_IOT.md`锛氭澘渚?IoT 杩愯鍙ｅ緞鎽樿銆?
- `linux_driver/README.md`锛氶┍鍔ㄨ竟鐣屻€佽繍琛岃涔夊拰鎺掗敊瑕佺偣銆?
- `pc_sim` 侧仍维护独立的 EV1527 真值映射基线；在 `master` 文档口径中只要求字段语义与该离线基线保持一致，不直接引用其工程路径。
