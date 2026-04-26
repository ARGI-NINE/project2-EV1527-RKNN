# project2_master

## 统一口径（2026-04-21）

- `project2_master` 是 RK3568 板端实时运行目录，不提供离线仿真入口。
- `pc_sim` 是离线仿真/回归基线，不是板端实时路径。
- `master` 当前唯一 RF 用户态输入是 `/dev/rf433`；`/dev/ttyS9` 仅属于底层 UART 物理链路，不是用户态默认输入。
- 当前实时主链路是：`UART 基础链路 -> serdev 上层 RF 驱动 -> /dev/rf433 -> linux_app/rf_gateway -> qt_gui`。
- Qt RF 波形来自 `rf_gateway` 输出的真实 `pulse_us=` CSV，源头是驱动帧中的真实脉冲数组，不再是按地址/键值反推的假波形。
- RF 在线状态依据驱动 `online` 位和真实 `[RF]` 帧更新；仅有“`rf_gateway running`”并不代表 RF 已在线。
- Qt 视觉页已改为板侧本地直连 `VisionRuntime`，默认真实输入为 `/dev/video9`，只允许 Linux `/dev/video*` 设备，不开放本地视频文件主路径；所需 RKNN/RGA 依赖已内聚到 `third_party/rknn_yolov5_rk3568/`。
- 已完成的是代码事实同步，不是端到端实机验收结论。

## 目录定位

`project2_master` 负责：

1. 从 `/dev/rf433` 实时读取 RF 帧。
2. 进行协议解析与 EV1527 解码。
3. 将结果输出到网关日志与 Qt 可视化界面。

本目录不提供可执行模拟入口，不作为离线回放容器。

## 三端职责边界

| 端 | 职责 | 运行入口 |
|---|---|---|
| hardware（下位机采集端） | STM32 实时采集 + UART 上传 | MCU 固件 |
| master (本目录) | RK3568 实时接收 + 解析 + 展示 | `/dev/rf433` |
| pc_sim（离线仿真基线） | 离线仿真与回归基线 | PC/WSL 仿真流程 |

## 主链路语义

master 的 RF 数据路径固定为：

```text
UART 物理链路 -> serdev 上层 RF 驱动 -> /dev/rf433 帧接口 -> linux_app/rf_gateway -> qt_gui
```

说明：

- `linux_app` 默认输入路径是 `/dev/rf433`，并拒绝其他 `--rf-input`。
- `qt_gui` 透传并校验同一路径。

## 目录结构

```text
project2_master/
  common/        # 协议结构与公共定义
  linux_driver/  # serdev RF 驱动（导出 /dev/rf433）
  linux_app/     # rf_gateway 实时网关
  qt_gui/        # Qt5 可视化界面
  third_party/   # 板端 VisionRuntime 内聚依赖（RKNN/RGA/model）
  docs/          # 系统设计与页面设计文档
```

## 关键运行参数

| 参数 | 默认值 | 说明 |
|---|---|---|
| `--rf-input` | `/dev/rf433` | 当前唯一实时输入路径 |
| `--stable-repeat` | `2` | 稳定分组最少一致帧数 |
| `--stable-window` | `12` | 分组记忆窗口 |
| `--stable-near-bits` | `4` | 近码合并阈值 |

## 协议/事件字段映射与差异约束

| 字段 | hardware | master | pc_sim | 要求 |
|---|---|---|---|---|
| `SYNC` | `AA55` | 驱动解析同步头 | 仿真帧同步头 | 必须一致 |
| `LEN` | LE16 | `rf_frame_t.len` / `pulse_count` | `pulse_count` | 必须一致 |
| `PAYLOAD` | `uint16` 脉冲数组 | `rf_frame_t.pulse[]` / `pulse_us` | 脉冲数组 | 必须一致 |
| `CRC` | XOR | 驱动校验 | 基线校验 | 必须一致 |
| `addr/key` | 无 | 解码输出 | 解码输出 | 语义一致 |
| `conf` | 无 | 解码置信度 | 解码置信度 | 允许轻微差异 |
| `seq/timestamp` | 无 | 实时序号与时间戳 | 仿真序号与时间轴 | 允许实现差异 |

允许差异项：

- `seq` 计数起点与步进可不同。
- `timestamp` 来源可不同（实时时钟或离线时间轴）。
- `conf` 可因样本窗口策略出现小幅差异。

## 构建

```bash
cmake -S . -B build -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=ON
cmake --build build -j
```

## 运行

```bash
./build/linux_app/rf_gateway --rf-input /dev/rf433
./build/qt_gui/rf_dashboard_qt5 --rf-input /dev/rf433
```

## 当前验证边界

- 已核对：`/dev/rf433` 唯一输入、波形来源、在线判定、vision 本地直连代码路径、视频设备输入限制、三端字段映射。
- 未完成：`hardware -> UART -> serdev -> /dev/rf433 -> rf_gateway -> Qt` 的端到端实机验收。
- 未完成：真实板侧 vision 直连链路的实机联调与 UI 验收。

## 排错清单

- `/dev/rf433` 不存在：检查 `linux_driver/rf433_drv.ko` 是否加载。
- 仅有 CRC/LEN 错误：回查下位机协议编码与线序。
- 只有 gateway 启动日志但 RF 仍离线：检查驱动 `online` 位和是否真的收到 `[RF]` 帧。
- UI 无 RF 事件：检查 `rf_gateway` stdout 是否包含 `[RF]` 和真实 `pulse_us=`。
- `--rf-input` 报非法：确认传参为 `/dev/rf433`。
- 与 `pc_sim` 结果不一致：优先验证板端实时链路，再用 `pc_sim` 做离线对照。

## 相关文档

- `README_IOT.md`
- `linux_driver/README.md`
- `docs/project2_iot_design.md`
- `docs/rk3568_vision_dashboard.md`
