# 函数和模块索引

这份文档只做回查，不重复展开实现。

## 1. 入口与主链

| 函数 / 模块 | 文件 | 作用 | 详细阅读 |
| --- | --- | --- | --- |
| `rf_proto_encode()` | `project2_master/common/rf_protocol.c` | 把 `rf_frame_t` 编成 `AA55/LEN/PAYLOAD/CRC` 字节流 | [project2_shared_protocol_deep_dive.md](project2_shared_protocol_deep_dive.md) |
| `rf433_receive_buf()` | `project2_master/linux_driver/rf433_drv.c` | serdev 收到 UART 字节后逐字节喂给驱动 parser | [project2_master_driver_deep_dive.md](project2_master_driver_deep_dive.md) |
| `main()` | `project2_master/linux_app/main.c` | 用户态入口，组装策略参数、设备入口和 epoll 事件泵 | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |
| `RFGatewayClient::handleProtocolLine()` | `project2_master/qt_gui/rf/rf_gateway_client.cpp` | 把 `rf_gateway` 的 JSON stdout envelope 还原成 Qt 后端状态 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `VisionRuntime::workerLoop()` | `project2_master/qt_gui/vision/vision_runtime.cpp` | 在 Qt 进程内跑本地相机/视频文件输入、双路 fan-out、RKNN 推理和状态发布 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |

## 2. `common/rf_protocol.c`

| 函数 | 作用 | 详细阅读 |
| --- | --- | --- |
| `rf_proto_crc8()` | 对 `LEN + PAYLOAD` 做逐字节异或，生成共享校验值 | [project2_shared_protocol_deep_dive.md](project2_shared_protocol_deep_dive.md) |
| `rf_proto_encode()` | 把脉冲数组写成稳定线上字节布局 | [project2_shared_protocol_deep_dive.md](project2_shared_protocol_deep_dive.md) |

当前 `project2_master/common/rf_protocol.*` 只导出 `rf_frame_t`、`rf_proto_crc8()` 和 `rf_proto_encode()`；流式 parser 在 hardware / driver 侧各自实现，不属于这里的用户态公共符号。

## 3. `linux_driver/rf433_drv.c`

| 函数 | 作用 | 详细阅读 |
| --- | --- | --- |
| `parser_reset()` | 解析错误或半帧超时后回到 `SYNC0` 起点 | [project2_master_driver_deep_dive.md](project2_master_driver_deep_dive.md) |
| `parser_emit_frame()` | 把 payload 重组为 `struct rf433_frame`，推进 `kfifo` 并更新统计 | [project2_master_driver_deep_dive.md](project2_master_driver_deep_dive.md) |
| `parser_feed_byte()` | 驱动里的核心字节流状态机 | [project2_master_driver_deep_dive.md](project2_master_driver_deep_dive.md) |
| `rf433_receive_buf()` | serdev 接收入口，批量消费 UART 字节并驱动状态机 | [project2_master_driver_deep_dive.md](project2_master_driver_deep_dive.md) |
| `rf433_online_timer_fn()` | 周期性检查在线位和半帧超时，避免 parser 卡死 | [project2_master_driver_deep_dive.md](project2_master_driver_deep_dive.md) |
| `rf433_misc_open()` | 把 misc device 的 `private_data` 绑定到驱动实例 | [project2_master_driver_deep_dive.md](project2_master_driver_deep_dive.md) |
| `rf433_misc_read()` | 向用户态导出“整帧设备”语义，而不是原始字节流 | [project2_master_driver_deep_dive.md](project2_master_driver_deep_dive.md) |
| `rf433_misc_poll()` | 告诉用户态何时有新帧可读 | [project2_master_driver_deep_dive.md](project2_master_driver_deep_dive.md) |
| `rf433_misc_ioctl()` | 导出 `GET_STATS`、`GET_STATUS`、`FLUSH_QUEUE` 等控制面 ABI | [project2_master_driver_deep_dive.md](project2_master_driver_deep_dive.md) |
| `rf433_probe()` | 驱动装配入口，初始化 kfifo、misc device、sysfs 和 serdev | [project2_master_driver_deep_dive.md](project2_master_driver_deep_dive.md) |
| `rf433_remove()` | 卸载时撤销 timer、设备节点和 serdev 绑定 | [project2_master_driver_deep_dive.md](project2_master_driver_deep_dive.md) |

## 4. `linux_app/main.c`

| 函数 | 作用 | 详细阅读 |
| --- | --- | --- |
| `main()` | 解析策略参数、校验输入路径、启动 `rf_epoll_run()`，并在退出前打印汇总统计 | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |
| `build_protocol_line()` | 把 `type/topic/mqtt_published/payload` 组装成单行 JSON envelope | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |
| `emit_protocol_message()` | 先尝试 MQTT 发布，再把同一份 payload 打到 stdout JSON envelope | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |
| `on_rf_frame()` | 用户态业务核心，把单帧解码结果做置信度过滤、稳定分组、重复抑制后发布成 `rf_event` JSON envelope | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |
| `on_drv_stats()` | 周期性读取驱动统计，并发出 `device_status` / `rf_stats` JSON envelope | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |

## 5. `linux_app/rf_source.c` 与 `linux_app/rf_epoll.c`

| 函数 | 作用 | 详细阅读 |
| --- | --- | --- |
| `rf_source_is_supported_path()` | 把用户态 RF 输入收紧到白名单设备路径 | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |
| `rf_source_open()` | 打开允许的 RF 输入设备并返回 `fd` | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |
| `rf_source_close()` | 统一回收 RF 输入句柄 | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |
| `consume_frames()` | 从 `/dev/rf433` 读出 `struct rf433_frame`，搬运为 `rf_frame_t` 并回调上层 | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |
| `rf_epoll_run()` | `epoll_wait()` 事件泵，负责持续喂帧和定时统计回调 | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |

## 6. `linux_app/rf_decode.c` 与 `linux_app/rf_decode_c.c`

| 函数 | 作用 | 详细阅读 |
| --- | --- | --- |
| `rf_decode_frame()` | 把算法结果标准化为 `rf_decoded_packet_t`，供 `main.c` 后续发布策略使用 | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |
| `rf_decode_get_runtime_stats()` | 读取累计解码统计 | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |
| `rf_decode_get_last_call_stats()` | 读取最近一次解码调用的阶段统计 | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |
| `build_runs_from_frame()` | 把脉冲序列先折叠成 run 序列，给 EV1527 算法做结构化输入 | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |
| `decode_best_from_runs()` | 在候选结构里做时序评分和最佳结果选择 | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |
| `rf_decode_ev1527_c_with_stats()` | EV1527 识别本体，串起 run 构造、结构筛选和候选输出 | [project2_master_userland_deep_dive.md](project2_master_userland_deep_dive.md) |

## 7. `qt_gui` 入口与共享状态

| 函数 / 方法 | 作用 | 详细阅读 |
| --- | --- | --- |
| `defaultRFInputPath()` | 固定 master 侧默认 RF 输入路径 `/dev/rf433` | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `defaultVisionDevicePath()` | 固定默认视觉输入设备路径 `/dev/video9` | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `isAllowedVisionInputPath()` | 允许 `/dev/video*` 或可读的本地视频文件作为视觉输入 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `applyDarkPalette()` | 统一 Qt 仪表盘主题与控件外观 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `MainWindow::setupUi()` | 组装页签、状态栏和页面骨架 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `MainWindow::setupRuntime()` | 启动 `RFGatewayClient` 和 `VisionRuntime`，把运行时挂到主窗口上 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `MainWindow::updateStatusBar()` | 把 `DashboardBackend` 中的系统快照汇总到状态栏 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `DashboardBackend::addRFEvent()` | 写入最新 RF 解码结果和波形缓存 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `DashboardBackend::updateProtocolStats()` | 汇总驱动 / 解析统计给页面共享状态 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `DashboardBackend::updateVisionState()` | 发布最新视觉推理快照 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `DashboardBackend::setVisionOffline()` | 在视觉运行失败或离线时写入降级快照 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `DashboardBackend::addLog()` | 把系统、RF、VISION、MQTT 日志统一收口 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `DashboardBackend::snapshotRF()` | 给 RF 页面提供只读快照 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `DashboardBackend::snapshotVisionState()` | 给视觉页面提供只读快照 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `DashboardBackend::snapshotSystemStats()` | 从 `/proc` 汇总 CPU / 内存等状态给 UI | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |

## 8. `qt_gui/rf` 与 `qt_gui/vision`

| 函数 / 方法 | 作用 | 详细阅读 |
| --- | --- | --- |
| `RFGatewayClient::start()` | 启动 Qt 侧 RF 网关消费链路 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `RFGatewayClient::resolveGatewayPath()` | 只接受固定位置的 `rf_gateway` 可执行文件 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `RFGatewayClient::resolvedRfInputPath()` | 把非法 RF 输入路径强制回退到默认值 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `RFGatewayClient::buildGatewayArgs()` | 组装 `rf_gateway --rf-input ...` 参数 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `RFGatewayClient::startGateway()` | 真实拉起外部 `rf_gateway` 进程，并连接 stdout / lifecycle 信号 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `RFGatewayClient::parseProtocolEnvelope()` | 解析 `type/topic/mqtt_published/payload` 顶层 JSON envelope | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `RFGatewayClient::parseRFEventPayload()` | 把 `rf_event.payload` 里的 `pulse_us[]` 等字段还原成 `RFEvent` | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `RFGatewayClient::handleProtocolLine()` | 解析 `rf_event`、`device_status`、`rf_stats` 等 stdout JSON envelope 行 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `VisionRuntime::start()` | 启动本地视觉线程 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `VisionRuntime::stop()` | 停止并回收视觉线程 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `VisionRuntime::workerLoop()` | 板端本地视觉主循环：找模型、打开 `/dev/video*` 或本地视频文件、双路 fan-out、推理、发快照 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `WaveformWidget::setPulses()` | 接收波形数据并触发重绘 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |
| `WaveformWidget::paintEvent()` | 把脉冲宽度画成 RF 波形 | [project2_master_qt_vision_deep_dive.md](project2_master_qt_vision_deep_dive.md) |

## 9. 回查时先看什么

- 你想看“共享字节协议长什么样”，先看 `rf_proto_crc8()`、`rf_proto_encode()`，然后回到 `project2_shared_protocol_deep_dive.md`。
- 你想看“`/dev/rf433` 是怎么长出来的”，先看 `parser_feed_byte()`、`parser_emit_frame()`、`rf433_misc_read()`。
- 你想看“`rf_gateway` 为什么会打印 `{\"type\":\"rf_event\",...}`”，先看 `build_protocol_line()`、`emit_protocol_message()`、`on_rf_frame()`、`rf_decode_frame()`。
- 你想看“EV1527 识别本体在哪”，先看 `build_runs_from_frame()`、`decode_best_from_runs()`、`rf_decode_ev1527_c_with_stats()`。
- 你想看“Qt 为什么只是 RF 消费者”，先看 `MainWindow::setupRuntime()`、`RFGatewayClient::startGateway()`、`RFGatewayClient::handleProtocolLine()`、`DashboardBackend::addRFEvent()`。
- 你想看“vision 为什么是 Qt 进程内本地运行时”，先看 `VisionRuntime::start()`、`VisionRuntime::workerLoop()`、`DashboardBackend::updateVisionState()`。
