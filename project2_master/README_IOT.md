# Project2 IoT 接口速查

## 固定端点

- Device ID：`rk3568-001`
- MQTT：`192.168.30.26:1883`，无当前代码级认证/TLS，QoS 0
- Topic root：`argi/device/rk3568-001`
- RTSP：`rtsp://192.168.30.26:8554/rk3568-001/cam0`
- RF device：`/dev/rf433`
- Vision default：`/dev/video9`

完整设计见 [docs/project2_iot_design.md](docs/project2_iot_design.md)，实操见 [MQTT](docs/mqtt_publish_tutorial_zh.md) 与 [RTSP](docs/rtsp_push_tutorial_zh.md)。

## Topic

| Topic | Type | Retain |
|---|---|---:|
| `.../status` | `device_status` | yes |
| `.../rf/event` | `rf_event` | no |
| `.../rf/stats` | `rf_stats` | no |
| `.../vision/detection` | `vision_detection` | no |
| `.../stream/status` | `stream_status` | yes |

## RF event 示例

```json
{
  "device_id": "rk3568-001",
  "type": "rf_event",
  "rf_input": "/dev/rf433",
  "addr": "0x35A1BC",
  "key": "12",
  "conf": 0.94,
  "confidence": 0.94,
  "src": "c",
  "source": "c",
  "seq": 7,
  "drv_seq": 18,
  "timestamp_ns": 0,
  "decode_us": 0,
  "mqtt_connected": true,
  "pulse_count": 50,
  "pulse_us": [
    320, 9920, 320, 960, 960, 320, 320, 960,
    320, 960, 960, 320, 320, 960, 960, 320,
    320, 960, 320, 960, 960, 320, 320, 960,
    960, 320, 320, 960, 320, 960, 960, 320,
    960, 320, 320, 960, 320, 960, 960, 320,
    320, 960, 960, 320, 320, 960, 960, 320,
    320, 960
  ]
}
```

这是结构示例，不保证该脉冲数组对应显示的地址/按键；重点是完整 EV1527 候选约 50 pulse，而不是旧文档中的 8-pulse 假完整事件。

## Vision detection 示例

```json
{
  "device_id": "rk3568-001",
  "type": "vision_detection",
  "frame_id": 42,
  "ts_us": 123456789,
  "objects": [{"class": "person", "conf": 0.87, "box": [100, 80, 420, 680]}],
  "stream_url": "rtsp://192.168.30.26:8554/rk3568-001/cam0"
}
```

只有 RGB 转换成功且 objects 非空时发布。

## 本地 stdout envelope

RF gateway 每个 payload 都包装为：

```json
{"type":"rf_event","topic":"argi/device/rk3568-001/rf/event","mqtt_published":true,"payload":{}}
```

Qt 消费这个 NDJSON。`mqtt_published=false` 不代表本地 payload 无效。

## 兼容与限制

- `conf/confidence`、`src/source` 是当前同时输出的兼容字段。
- `driver_crc_err` 名称保留兼容，算法是 UART XOR checksum。
- status retained 不等于实时在线，也没有 Last Will。
- live broker/RTSP/RK3568 行为必须在部署环境验证。
