# Project2 IoT 设计说明

## 范围

本页描述当前代码的设备数据模型、topic 与降级策略，不复制实现。部署步骤见 [MQTT 教程](mqtt_publish_tutorial_zh.md)，视频推流见 [RTSP 教程](rtsp_push_tutorial_zh.md)。

## 两个 producer

1. `linux_app/rf_gateway`：读取固定 `/dev/rf433`，发布 device status、RF event、RF stats，同时输出 stdout NDJSON 给 Qt。
2. `qt_gui/vision/VisionRuntime`：板端推理，发布 detection 与 stream status。

二者都连接 `192.168.30.26:1883`，device ID `rk3568-001`，QoS 0，无代码级认证/TLS。RF producer 的根 topic 为 `argi/device/rk3568-001`；Vision 使用对应完整 topic 常量。

## Topic 契约

| topic 后缀 | type | retain | 主要用途 |
|---|---|---:|---|
| `status` | `device_status` | 是 | RF/driver/MQTT 当前快照 |
| `rf/event` | `rf_event` | 否 | 稳定解码事件与完整 pulse |
| `rf/stats` | `rf_stats` | 否 | driver/app/epoll/decode 计数 |
| `vision/detection` | `vision_detection` | 否 | frame、时间、objects、stream URL |
| `stream/status` | `stream_status` | 是 | RTSP state/reason/url |

状态 retained，瞬时事件不 retained。当前没有 Last Will；消费者必须结合消息时间/业务心跳判断陈旧 retained 状态。

## RF 数据流

```text
/dev/rf433 record
 -> decode
 -> confidence gate
 -> Hamming-near stable group（hits uint32 饱和）
 -> publish gap
 -> payload
 -> MQTT + stdout envelope
```

stdout envelope 包含 `type/topic/mqtt_published/payload`。MQTT 断开不阻止本地 JSON；Qt 不应假设每条本地 event 已上云。

## Vision 数据流

```text
V4L2 或可读视频文件
 -> MPP/RGA/RKNN
 -> detection group + source frame
 -> RGB 转换成功 -> Qt annotated frame
 -> detGroup 非空 -> vision/detection
 -> annotated NV12 -> MPP H.264 -> libavformat RTSP
```

推理、显示、MQTT、RTSP 是有明确依赖的支路。RTSP 失败会关闭/重试 stream branch，不应使 RF 或推理主链路崩溃；post-stream frame 构建失败会禁用 RTSP branch 并保留 inference/display。

## 运行配置

固定默认：RF `/dev/rf433`、Vision `/dev/video9`、RTSP `rtsp://192.168.30.26:8554/rk3568-001/cam0`。Qt CLI 允许 vision 指向 `/dev/video*` 或可读本地视频文件，并允许 `--disable-vision-rtsp`；RF input 不允许替换为任意文件。

## 可靠性与限制

- AA55 UART 的 checksum 是 XOR，不是强 CRC；它用于传输差错检测而非安全。
- MQTT QoS 0 不保证交付；retain 只保存最后状态。
- driver、decoder、MQTT 与 RTSP 计数/日志分别表示不同层，不能相互替代。
- Vision label 在首次成功加载后成为不可变进程级数据；随后冲突路径使模型初始化失败，防止实例读取不同类别表。
- 真实 broker、RTSP server、RK3568 media/NPU 与硬件 RF 仍需目标环境验收。
