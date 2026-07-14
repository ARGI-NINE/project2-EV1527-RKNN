# MQTT 发布教程：从代码路径到联调验收

## 1. 读者与结果

本文面向部署 `rf_gateway` 或板端 Qt/Vision 的开发者。完成后，你应能准备 broker、订阅全部 Project2 topic、判断 retain/QoS、把 stdout JSON 与 broker 消息对应起来，并在断线时区分“本地处理成功”和“MQTT 发布成功”。

本文只给出命令，不会替你连接外部服务。所有地址来自当前源码。

## 2. 前置条件

- 可访问的 MQTT broker：`192.168.30.26:1883`。
- broker 允许当前网络匿名、无 TLS 的 MQTT 连接；当前代码没有用户名、密码、证书或 CLI broker override。如果服务要求鉴权/TLS，必须先扩展代码和部署配置，不能只改订阅命令。
- 观察端安装 Mosquitto clients（`mosquitto_sub`）。
- RF 发布需要 `rf_gateway` 已构建且 `/dev/rf433` 可用；Vision 发布需要板端 Qt/Vision 运行环境。

## 3. 固定配置与 topic

RF 常量在 `linux_app/main.c`，Vision 常量在 `qt_gui/vision/vision_runtime.cpp`：

| 完整 topic | 生产者 | QoS | retain | 触发 |
|---|---|---:|---:|---|
| `argi/device/rk3568-001/status` | `rf_gateway` | 0 | 是 | startup、driver stats；仅当事件循环返回并进入清理路径时还有 shutdown |
| `argi/device/rk3568-001/rf/event` | `rf_gateway` | 0 | 否 | 解码、confidence、稳定与去重都通过 |
| `argi/device/rk3568-001/rf/stats` | `rf_gateway` | 0 | 否 | stats timer callback |
| `argi/device/rk3568-001/vision/detection` | `VisionRuntime` | 0 | 否 | RGB 转换成功且检测组非空 |
| `argi/device/rk3568-001/stream/status` | `VisionRuntime` | 0 | 是 | `state` 变化；失败细节通过可选 `reason` 字段表达 |

QoS 0 来自两处 `mosquitto_publish(..., 0, retain)`。retained status 使新订阅者能看到最后状态；事件/统计/检测只代表当时发生的消息，不应被旧事件冒充新事件。

## 4. 启动订阅观察

在 broker 可达的观察机运行：

```bash
mosquitto_sub -h 192.168.30.26 -p 1883 \
  -t 'argi/device/rk3568-001/#' -v
```

分别观察也可用：

```bash
mosquitto_sub -h 192.168.30.26 -p 1883 -t 'argi/device/rk3568-001/status' -v
mosquitto_sub -h 192.168.30.26 -p 1883 -t 'argi/device/rk3568-001/rf/+' -v
mosquitto_sub -h 192.168.30.26 -p 1883 -t 'argi/device/rk3568-001/vision/detection' -v
mosquitto_sub -h 192.168.30.26 -p 1883 -t 'argi/device/rk3568-001/stream/status' -v
```

若 broker 实际开启凭据，`mosquitto_sub` 可添加 `-u/-P` 来观察，但生产者仍无法登录；必须让生产配置与观察配置一致。

## 5. 构建并启动 RF 发布者

```bash
cmake -S project2_master -B build/master \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/master
ctest --test-dir build/master --output-on-failure

./build/master/linux_app/rf_gateway --rf-input /dev/rf433
```

`main -> mqtt_publisher_init` 异步连接 broker；`emit_protocol_message` 只有在 `mqtt_publisher_is_connected` 为真时调用 publish，但无论结果如何都会向 stdout 写一行 envelope：

```json
{
  "type": "rf_event",
  "topic": "argi/device/rk3568-001/rf/event",
  "mqtt_published": false,
  "payload": {"type": "rf_event", "device_id": "rk3568-001"}
}
```

示例省略了完整 payload。判断 broker 是否真的收到应看 `mqtt_published`、subscriber 和 stderr 三者；stdout 有 event 只证明本地生成成功。

## 6. RF payload 字段

### `device_status`

包含 `device_id/type/rf_input/broker/port`、`rf_online/mqtt_connected`、五个稳定参数、driver seq/queue/stats、`app_drv_drop/published_events/reason`。driver 字段在 ioctl 尚未成功时为零/false。

### `rf_stats`

包含应用计数 `frames_total/decode_ok/decode_no_frame/decode_err/low_conf_drop/stable_drop/dup_drop/published_events/drv_drop`，driver stats/status，epoll/read 错误计数，以及 C decoder 的 attempts/accepts/耗时和 `reason`。`driver_crc_err` 是 legacy 字段名，表示 UART XOR checksum 不匹配。

### `rf_event`

包含：

- `device_id/type/rf_input`；
- `addr/key`；
- 兼容别名 `conf/confidence`、`src/source`；
- app `seq`、driver `drv_seq`、`timestamp_ns`、`decode_us`；
- `mqtt_connected`、`pulse_count`、完整 `pulse_us`。

完整 EV1527 事件通常约 50 pulse。文档或消费者若只展示前几项，必须标明数组已截断，不能把 8 pulse 说成可解码完整帧。

## 7. 启动 Vision 发布者

目标板依赖满足后，先用 `BUILD_QT5_GUI=ON` 生成板端 target：

```bash
cmake -S project2_master -B build/master-board \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=ON -DBUILD_TESTING=ON
cmake --build build/master-board
```

再运行：

```bash
./build/master-board/qt_gui/rf_dashboard_qt5 \
  --rf-input /dev/rf433 \
  --vision-device /dev/video9 \
  --vision-rtsp-url rtsp://192.168.30.26:8554/rk3568-001/cam0
```

Vision 使用独立 client `rk3568-001-vision-runtime`，异步连接并设置 1..5 秒指数式重连 delay。连接失败只禁用/降级 Vision MQTT，推理和本地显示仍可继续。

`vision_detection` 字段：`device_id/type/frame_id/ts_us/objects/stream_url`；每个 object 有 `class/conf/box[left,top,right,bottom]`。只有 AI 输出帧成功转 RGB、并且 `detGroup.count > 0` 才调用 `publishDetection`。NPU 有检测但 RGA RGB 转换失败时不会发布。

`stream_status` 字段：`device_id/type/state/protocol=rtsp/codec=h264/url`，失败时还可有 `reason`。encoder open 失败时实际 payload 是 `state="offline"`、`reason="open_failed"`；该组合可随每轮重试重复发布，不能把 `open_failed` 当成 `state` 值。订阅端应按两字段组合幂等处理。

## 8. 验收步骤

1. 先启动 wildcard subscriber，确认 retained status 是否立即出现；旧 retained 只表示上次状态。
2. 启动 gateway，观察新的 `status`，其 `mqtt_connected` 最终应为 true。
3. 触发合法重复 RF 帧；subscriber 应出现 non-retained `rf/event`，stdout 同行 `mqtt_published=true`。
4. 等待 stats interval，确认 `rf/stats` 到达且计数逻辑合理。
5. 启动 Vision；有真实检测且 RGB 转换成功时出现 `vision/detection`。
6. 启停 RTSP server 或禁用 RTSP，确认 retained `stream/status` 与 GUI 日志一致。

## 9. 断线与排错

- `Connection refused/timeout`：检查地址、端口、防火墙和 broker listener。
- broker 要求认证/TLS：当前 producer 不支持，不能靠重试解决。
- subscriber 收到 status 但无 RF event：检查 `/dev/rf433`、decoder/stable/confidence，而不是 MQTT topic。
- stdout `mqtt_published=false`：发布时尚未连接或调用失败；本地数据仍有效。
- 有 detection UI 无 topic：确认 RGB 转换成功、有非空 detection，并看 `[VISION_MQTT]`。
- `stream/status` 重复出现 `state="offline"`、`reason="open_failed"`：表示每次 RTSP open retry 都失败，检查 server 是否接受写入。

两处 publisher 都使用 libmosquitto background loop 与 reconnect callback，但 QoS 0 不提供 broker 持久确认。需要端到端可靠交付时必须新增协议设计，而不是把 retain 用到事件 topic。

## 10. 清理

subscriber 和 GUI 可按各自生命周期停止。当前 `rf_gateway` 没有 signal handler 或 stop flag，`rf_epoll_run` 遇到 `EINTR` 会继续，因此不能把 `Ctrl+C` 描述为 gateway 的优雅退出，也不能保证它发布 retained shutdown status。只有事件循环因其它路径返回到 `main` 时，清理代码才会尝试发布 shutdown 并释放 Mosquitto/fd；默认 SIGINT 终止、进程崩溃或断网时，broker 都可能保留上次状态，因为当前代码没有配置 MQTT Last Will。清理测试 retained 状态需由 broker 管理员用空 retained publish 等明确操作完成，勿在生产环境误删。

## 11. 验证边界

仓库主机构建验证 libmosquitto 调用可以编译，但没有在此流程连接真实 broker。地址可达性、ACL、retain 与重连必须按本教程在部署网络实测。
