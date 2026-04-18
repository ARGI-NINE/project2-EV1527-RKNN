# RF433 IoT 网关 — 板侧部署指南

本文档聚焦 **RF433 信号从 STM32 采集到 MQTT 云端上报**的完整链路。
仅涉及 RK3568 开发板真实硬件部署。
RF 输入仅允许 `/dev/ttyS9` 实时串口，其他输入全部拒绝。

---

## 1. 信号链路

```
RF433 无线信号
  │
  ▼
STM32F103C8T6（Input Capture 采集 OOK 脉冲）
  │  USART1 → AA55 协议帧
  ▼
RK3568 UART9（/dev/ttyS9, 9600 8N1）
  │
  ▼
rf_gateway（C 程序）
  ├─ rf_source：termios 串口读取
  ├─ rf_epoll：epoll 事件循环
  ├─ rf_decode：EV1527 C 解码
  └─ main：稳定分组 + 去重 + stdout 发布
  │
  ▼
MQTT Broker（云端）
```

---

## 2. 串口配置

| 参数     | 值             | 说明                 |
|:---------|:---------------|:--------------------|
| 设备节点 | `/dev/ttyS9`   | RK3568 UART9        |
| 波特率   | 9600           | `B9600`             |
| 数据位   | 8              | `CS8`               |
| 校验位   | 无             | `~PARENB`           |
| 停止位   | 1              | `~CSTOPB`           |
| 流控     | 无             | `~CRTSCTS`          |
| 模式     | Raw            | `cfmakeraw()`       |
| 打开方式 | `O_RDWR | O_NOCTTY | O_NONBLOCK` | 与 RK3568 UART9 规范对齐 |

---

## 3. rf_gateway 命令行参数

```bash
./rf_gateway \
    --rf-input /dev/ttyS9 \
    --stable-repeat 2 \
    --stable-window 12 \
    --stable-near-bits 4 \
    --preferred-code 0x12D1B1 \
    --min-publish-confidence 0.72 \
    --publish-gap 6
```

| 参数                       | 默认值      | 说明                                 |
|:---------------------------|:------------|:-------------------------------------|
| `--rf-input`               | /dev/ttyS9  | 串口设备路径（仅允许 `/dev/ttyS9`）   |
| `--stable-repeat`          | 2           | 稳定分组需要的最少一致帧数             |
| `--stable-window`          | 12          | 分组记忆窗口（帧数）                   |
| `--stable-near-bits`       | 4           | Hamming 距离合并阈值（24bit 编码空间） |
| `--preferred-code`         | 0x12D1B1    | 优先编码（分组中出现则优先输出）        |
| `--min-publish-confidence` | 0.72        | 最低发布置信度                         |
| `--publish-gap`            | 6           | 同一编码重复发布最小帧间隔             |

---

## 4. MQTT 发布格式

rf_gateway 解码成功后向 stdout 输出 `[RF]` 行，MQTT 模块据此构造 JSON 并发布：

**stdout 行格式：**

```
[RF] addr=0x12D1B1 key=1 conf=0.95 source=C pulses=50 seq=42 decode_us=123
```

**MQTT JSON 负载：**

```json
{
  "addr": "0x12D1B1",
  "key": 1,
  "confidence": 0.95,
  "source": "C",
  "pulses": 50,
  "seq": 42,
  "decode_us": 123,
  "timestamp": 1713340800
}
```

**统计行：**

```
[RF_STATS] frames_total=1000 decode_ok=800 ... published=42
[RF_PARSE_STATS] crc_errors=3 parse_errors=7 last_error=crc_mismatch
[RF_IO_STATS] read_eintr=0 read_eagain=1024 read_eof=0 read_error=0 epoll_eintr=1 epoll_error=0
```

---

## 5. 构建（仅网关，不需要 Qt5）

```bash
export CROSS_COMPILE=aarch64-linux-gnu-
cmake -S . -B build \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=${CROSS_COMPILE}gcc \
    -DBUILD_LINUX_APP=ON \
    -DBUILD_QT5_GUI=OFF
cmake --build build
```

产物：`build/linux_app/rf_gateway`

---

## 6. 板侧部署

```bash
# 拷贝到开发板
scp build/linux_app/rf_gateway root@<board-ip>:/usr/local/bin/

# 运行
ssh root@<board-ip>
rf_gateway --rf-input /dev/ttyS9
```

---

## 7. 注意事项

- 本目录仅按 `/dev/ttyS9` 实时串口链路运行。
- 本目录仅支持 `/dev/ttyS9` 实时串口，所有非 `/dev/ttyS9` 输入路径均已封堵。
- 任何非 `/dev/ttyS9` 输入都不在 master 支持范围内。
- Qt 端仅允许固定网关位置：`rf_dashboard_qt5` 所在目录下的 `./rf_gateway`，不提供 `--gateway` 注入入口。
- STM32 下位机固件位于 `../project2_hardware/`。

---

## 8. 运行边界矩阵

| 维度 | project2_master（本目录） | project2_hardware |
|:--|:--|:--|
| RF 输入 | `/dev/ttyS9`（唯一） | RF 接收 + TIM2 捕获 |
| 其他输入 | 全部拒绝 | 不涉及 |
| 串口角色 | 上位机读取与解码 | 下位机发送 AA55 帧 |
| 当前实现状态 | 已收敛并启用硬约束 | 保持现状 |

---

## 9. 当前实现状态（2026-04）

- `linux_app/rf_source.c`：仅允许 `/dev/ttyS9`，并固定 `O_RDWR|O_NOCTTY|O_NONBLOCK`。
- `linux_app/main.c`：`--rf-input` 仅接受 `/dev/ttyS9`，其余路径直接拒绝。
- `qt_gui`：默认并强制透传 `/dev/ttyS9`，阻断 GUI 侧其他输入。
- `linux_driver/rf433_drv.c`：仅历史样例，不参与当前运行链路，不能替代厂商 UART 驱动（详见 `linux_driver/README.md`）。

---

## 10. 运行与验证步骤

```bash
# 1) 最小构建验证
cmake -S . -B build_verify -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=OFF
cmake --build build_verify --target rf_gateway

# 2) 正向运行（通过）
./build_verify/linux_app/rf_gateway --rf-input /dev/ttyS9

# 3) 负向校验（应失败）
# 将 --rf-input 设置为任何非 /dev/ttyS9 值，程序应拒绝启动并返回失败码
```
