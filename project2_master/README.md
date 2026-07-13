# project2_master

`project2_master` 是当前仓库里面向 RK3568 板侧运行时的主目录。就现有代码事实来看，它已经形成了两条并行主线：

- RF 主线：把 STM32 采集到的 433MHz 脉冲帧，经 UART 和 Linux 内核驱动，送到 `rf_gateway`，再交给 Qt 前端消费。
- Vision 主线：Qt 进程内的本地视觉运行时。

这份 README 只把现状讲清楚，重点放在 RF 真实主链上；Vision/RTSP 只点到边界，不在这里深挖。

## 统一口径（2026-04-21）

- `project2_master` 是 RK3568 板侧实时运行目录，不是离线仿真入口。
- 当前 README 重点讲 RF 真实主链；Vision 只保留与板侧本地运行时有关的边界说明。
- `project2_pc_sim` 是离线对照基线，不替代 `project2_master` 的板侧实时路径。

## 目录定位

这份 README 先把 `project2_master` 的运行边界、主链语义和阅读入口立住，再把细节交给后续深读文档。

## 三端职责边界

| 端 | 当前职责 |
| --- | --- |
| `project2_hardware` | 下位机采集与 UART 上传 |
| `project2_master` | 板侧实时接收、解码、JSON envelope 输出与 Qt 消费 |
| `project2_pc_sim` | 离线 WAV 回放、bridge 模拟与回归对照 |

## 主链路语义

当前 RF 主链仍是 `STM32 -> UART -> /dev/rf433 -> rf_gateway -> stdout JSON envelope -> Qt`；详细展开见下方“先记住一条 RF 主链”。

## 目录结构

当前目录角色说明见下方“目录怎么读”。

## 关键运行参数

当前 README 只前置 `--rf-input /dev/rf433` 这一条固定输入口径；其余策略参数以 `linux_app` 深读文档为准。

## 协议/事件字段映射与差异约束

共享协议只到 pulse frame；`addr/key/conf` 属于用户态解码结果；Qt 消费的是 `rf_gateway` 输出的根级 JSON envelope，而不是 MQTT 回流。

## 构建

详细命令见下方“构建提示”。

## 运行

详细关系见下方“运行时关系”。

## 当前验证边界

当前 README 只同步代码事实，不把端到端实机验收写成已完成。

## 排错清单

常见误读集中在 `/dev/rf433`、共享协议边界、`stdout JSON envelope` 与 MQTT 的关系，细节见下方各节。

## 相关文档

- [README_IOT.md](README_IOT.md)
- [docs/project2_iot_design.md](docs/project2_iot_design.md)
- [docs/project2_master_reading_guide_zh.md](docs/project2_master_reading_guide_zh.md)
- [docs/project2_master_function_index.md](docs/project2_master_function_index.md)

## 先记住一条 RF 主链

```text
STM32 pulse capture
  -> UART bytes
  -> linux_driver/rf433_drv.c (serdev parser)
  -> /dev/rf433
  -> linux_app/rf_gateway
  -> stdout JSON envelope
  -> qt_gui/rf/RFGatewayClient
  -> DashboardBackend
  -> RFStatusPage / SystemLogPage
               \
                -> MQTT publish side-branch (if broker connected)
```

这条链里最容易写错的地方有三点：

1. `/dev/rf433` 不是串口原始字节流，而是内核已经拼好的“整帧脉冲结构体”接口。
2. 共享协议只到 pulse frame 为止。`addr`、`key`、`conf` 不是共享协议字段，而是 `linux_app/rf_decode*.c` 的上层解码结果。
3. MQTT publish 是 `rf_gateway` 的旁路输出。Qt RF 页面不是从 MQTT 订阅回来的，而是直接消费 `rf_gateway` 的 `stdout` JSON 行。

这三点都可以直接被源码托底。

摘自 `linux_driver/rf433_drv.c`：

```c
memset(&frame, 0, sizeof(frame));
frame.timestamp_ns = ktime_get_real_ns();
frame.pulse_count  = priv->expected_pulses;
frame.seq          = ++priv->seq;

for (i = 0; i < priv->expected_pulses; i++) {
    u16 lo = priv->payload_buf[i * 2];
    u16 hi = priv->payload_buf[i * 2 + 1];
    frame.pulse[i] = lo | (hi << 8);
}
```

这段说明 `/dev/rf433` 之前已经不是裸 UART 字节，而是驱动拼好的结构化帧：`pulse[]`、`pulse_count`、`timestamp_ns`、`seq` 都在这里成形。

摘自 `linux_app/main.c`：

```c
"\"addr\":\"%s\","
"\"key\":\"%s\","
"\"conf\":%.4f,"
"\"confidence\":%.4f,"
"\"src\":\"%s\","
"\"source\":\"%s\","
"\"seq\":%u,"
"\"drv_seq\":%u,"
"\"timestamp_ns\":%llu,"
"\"decode_us\":%llu,"
"\"mqtt_connected\":%s,"
"\"pulse_count\":%u,"
"\"pulse_us\":[",
```

这一段则说明 `addr`、`key`、`conf`、`source`、`drv_seq` 和 `pulse_us[]` 都是 `rf_gateway` 用户态 JSON payload 的字段，不是 shared pulse-frame ABI 自带的字段。

## 当前代码已经实现了什么

- 共享 RF 帧格式：`project2_master/common/rf_protocol.*`
- 板侧 RF 驱动：`project2_master/linux_driver/rf433_drv.c`
- 用户态网关：`project2_master/linux_app/main.c` 生成可执行文件 `rf_gateway`
- Qt 消费端：`project2_master/qt_gui/rf/rf_gateway_client.cpp`
- RF 状态页：`project2_master/qt_gui/rf/rf_status_page.cpp`

当前代码没有证据支持下面这些能力已经落地，因此文档不应把它们写成“已实现”：

- MQTT command 订阅和命令执行
- GPIO dry contact 输入
- 事件触发录像和 `record_done` 工作流

## 目录怎么读

| 目录 | 角色 |
| --- | --- |
| `common/` | master 与硬件侧共用的 RF 脉冲帧 ABI，只定义帧格式与编码 |
| `linux_driver/` | serdev 内核驱动，负责把 UART 字节流变成 `/dev/rf433` |
| `linux_app/` | `rf_gateway` 用户态主程序，负责 epoll、解码、JSON envelope、MQTT publish |
| `qt_gui/` | Qt 前端。RF 侧通过拉起 `rf_gateway` 并消费其 `stdout` 工作 |
| `docs/` | 面向代码走读的分层文档 |

## 构建提示

如果你只想验证 RF 主链，不需要先把 Qt 和 Vision 一起拉起来。当前顶层 CMake 默认会进入 Qt 目录，而 Qt 目录又要求板侧 Vision 运行时依赖齐全；因此看 RF 时，先单独构建 `rf_gateway` 更稳妥。

### 只构建 RF 用户态网关

```bash
cmake -S project2_master -B build/project2_master-rf -DBUILD_QT5_GUI=OFF
cmake --build build/project2_master-rf --target rf_gateway
```

### 构建 Linux 驱动

```bash
cd project2_master/linux_driver
make KDIR=/lib/modules/$(uname -r)/build
```

### 构建完整 Qt 前端

```bash
cmake -S project2_master -B build/project2_master-board
cmake --build build/project2_master-board --target rf_dashboard_qt5
```

说明：

- `rf_dashboard_qt5` 当前是板侧程序，Qt 构建会同时要求本地 Vision 运行时依赖。
- `rf_gateway` 由 `linux_app/CMakeLists.txt` 生成。
- `rf_dashboard_qt5` 会从自身程序目录寻找固定路径 `rf_gateway`，不是通过网络 RPC 调它。

## 运行时关系

### `rf_gateway`

直接运行时：

```bash
./rf_gateway --rf-input /dev/rf433
```

它会做三件事：

1. 从 `/dev/rf433` 读取驱动帧。
2. 在用户态做 EV1527 解码、稳定分组和重复抑制。
3. 对每个 `device_status`、`rf_stats`、`rf_event` 同时生成一行 `stdout` JSON envelope，并在 MQTT 已连接时尝试发布到 `argi/device/rk3568-001/...`。

### `rf_dashboard_qt5`

直接运行时：

```bash
./rf_dashboard_qt5 --rf-input /dev/rf433
```

Qt 侧会强制把 RF 输入路径约束回 `/dev/rf433`。就代码事实看，它不会接受任意用户注入的 RF 输入路径，也不会从 MQTT 重新拉取 RF 事件。

摘自 `qt_gui/rf/rf_gateway_client.cpp`：

```cpp
QString RFGatewayClient::resolvedRfInputPath() const {
    const QString defaultPath = defaultRFInputPath();
    if (options_.rfInput.isEmpty() || options_.rfInput == defaultPath) {
        return defaultPath;
    }
    return defaultPath;
}

QStringList RFGatewayClient::buildGatewayArgs() const {
    QStringList args;
    args << "--rf-input" << resolvedRfInputPath();
    return args;
}
```

这段代码说明板侧 Qt 不接受任意 RF 输入路径注入。即使外部传了别的路径，`resolvedRfInputPath()` 也会强制回到默认 `/dev/rf433`。

同文件还有一个直接消费 `rf_gateway` 输出的入口：

```cpp
gateway_ = new QProcess(context_);
gateway_->setProcessChannelMode(QProcess::SeparateChannels);

QObject::connect(gateway_, &QProcess::readyReadStandardOutput, context_, [this]() {
    if (gateway_ == nullptr) {
        return;
    }

    drainProtocolBuffer(gateway_->readAllStandardOutput());
});
```

也就是说，板侧 Qt 的 RF 数据入口就是 `readyReadStandardOutput -> drainProtocolBuffer(...)`，不是从 MQTT 订阅回来的回路。

## 文档阅读顺序

如果你的目标是把 `/dev/rf433 -> rf_gateway -> Qt RF 页面` 读通，建议按下面顺序：

1. [docs/project2_iot_design.md](docs/project2_iot_design.md)
2. [docs/project2_shared_protocol_deep_dive.md](docs/project2_shared_protocol_deep_dive.md)
3. [docs/project2_master_driver_deep_dive.md](docs/project2_master_driver_deep_dive.md)
4. [docs/project2_master_userland_deep_dive.md](docs/project2_master_userland_deep_dive.md)
5. [docs/project2_master_reading_guide_zh.md](docs/project2_master_reading_guide_zh.md)
6. [docs/project2_master_function_index.md](docs/project2_master_function_index.md)

如果你后续要看 Vision，再单独去读已有的 `project2_master_qt_vision_deep_dive.md`；不要把那部分和 RF 主链混在一起理解。
