# rf433_drv（serdev 上层 RF 驱动）

## 统一口径（2026-04-21）

- `rf433_drv` 是 `master` 实时链路中的内核上层驱动，位于 UART 物理链路之上。
- `master` 用户态当前唯一 RF 输入是 `/dev/rf433`；`/dev/ttyS9` 只属于更下层 UART 基础设施。
- `pc_sim` 只做离线基线，对齐的是协议字段语义，不直接依赖本驱动。
- 驱动维护 `online` 位；用户态在线状态应基于该位和真实 RF 帧，而不是基于进程启动。
- 当前仍未完成整机端到端实机验收，因此本文档不把代码口径同步写成已完成实机联调。

## 定位

`linux_driver/rf433_drv.c` 是 `master` 实时链路中的内核上层驱动，职责是：

1. 从 serdev 回调接收串口字节流。
2. 在内核态完成 `AA55 + LEN + PAYLOAD + CRC` 帧解析。
3. 通过 misc 设备导出 `/dev/rf433` 帧接口给用户态。
4. 维护统计信息和 `online` 状态。

## 链路语义

```text
UART 硬件基础 -> serdev 控制器 -> rf433_drv(本目录) -> /dev/rf433 -> linux_app/rf_gateway
```

该语义是 `master` 默认运行路径。

## 功能摘要

| 特性 | 说明 |
|---|---|
| 设备节点 | `/dev/rf433` |
| 输入协议 | `AA55 + LEN(LE16) + PAYLOAD + XOR CRC` |
| 队列 | `kfifo`，队列满时丢弃最旧帧（drop oldest）并计数，保留最新帧 |
| 读接口 | `read()` 一次返回一帧 |
| 事件接口 | `poll()/epoll` |
| 统计接口 | `ioctl + sysfs` |
| 在线状态 | 驱动内部 `online` 位 |

## 与三端关系

| 端 | 与本驱动关系 |
|---|---|
| hardware | 提供 UART 上行字节流 |
| master | 通过 `/dev/rf433` 消费帧 |
| pc_sim | 仅离线对照，不直接依赖本驱动 |

## 字段映射（驱动视角）

| 协议字段 | 驱动内部 | 用户态字段 |
|---|---|---|
| `LEN` | `expected_pulses` | `rf433_frame.pulse_count` |
| `PAYLOAD[i]` | `payload_buf` | `rf433_frame.pulse[i]` |
| `CRC` | `crc_accum` 比对 | 校验通过后才入队 |
| 帧序号 | `priv->seq` | `rf433_frame.seq` |
| 在线状态 | `priv->online` | `rf433_status.online` |

允许差异项：帧时间戳来源为驱动时钟，可能与 `pc_sim` 离线时间轴不同。

## 在线位语义

- 合法完整帧入队后，驱动置 `online=true`。
- 超过超时窗口未收到完整帧时，驱动清除 `online`。
- 因此 `/dev/rf433` 设备存在、`rf_gateway` 进程存在、甚至串口打开成功，都不等于 RF 链路在线。

## 构建与加载

```bash
make KDIR=/path/to/kernel/build
sudo insmod rf433_drv.ko
ls -l /dev/rf433
```

## 当前验证边界

- 已核对：协议状态机、`/dev/rf433` 导出、队列策略、`online` 位导出、ioctl 字段。
- 未核对完成：真实板卡上的长时间收发稳定性、超时边界、整机端到端联调。
- 不能写成：驱动已完成实机 E2E 验收。

## 排错清单

- `/dev/rf433` 不生成：检查 DTS 绑定与 probe 日志。
- 持续 `crc_err`：检查下位机协议编码。
- `online` 长期为 0：检查是否真的收到了完整合法帧，而不是只拉起了进程。
- 帧断续：检查 `drop_cnt`、串口噪声和供电。
- 用户态读取失败：核对结构体版本与 ioctl 头文件一致性。

## 互引

- `DESIGN_rf433_drv_v2.md`锛氭湰鐩綍涓嬬殑椹卞姩璁捐璇存槑銆?
- `project2_master` 鏍圭洰褰?`README.md`锛氭暣浣?master 杩愯鍙ｅ緞涓庤竟鐣屾憳瑕併€?
- `docs/project2_iot_design.md`锛歁aster 瀹炴椂 IoT 璁捐鍙ｅ緞鎽樿銆?
