# RTSP 推流教程：MPP H.264 到 libavformat

## 1. 目标与前置条件

本文面向板端部署者。完成后应能让 `VisionRuntime` 把标注后的 NV12 帧用 MPP 编码为 H.264，经 FFmpeg `libavformat` 写入 RTSP server，并用 `ffprobe`/播放器确认。

需要：RK3568、匹配的 MPP/RGA/FFmpeg 库、可用输入和模型、一个允许 publisher 写入的 RTSP server，以及观察端 `ffprobe` 或播放器。默认 URL：

```text
rtsp://192.168.30.26:8554/rk3568-001/cam0
```

项目不会启动 RTSP server；必须先按 server 产品文档配置 path/publish 权限。本文不连接外部服务。

## 2. 代码路径

```text
AI result + source frame
 -> copyPostInferFrameToPostStreamPool
 -> 标注/缩放后的 NV12 PostStreamFrame
 -> stream thread
 -> MppRtspEncoder::open
    -> MPP H.264 encoder
    -> avformat RTSP output / avio_open2
 -> push(frame)
```

这是库内 libavformat，不是 shell 启动 `ffmpeg` 子进程。encoder 检查 URL、尺寸、NV12 几何、stride、buffer、MPP/AVFormat 每步返回值；这些保护不能删除。

## 3. 启动

先确认 server 已监听 8554 并允许写入该 path。板端用匹配 toolchain/sysroot 构建 GUI：

```bash
cmake -S project2_master -B build/master-board \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=ON -DBUILD_TESTING=ON
cmake --build build/master-board
```

然后运行：

```bash
./build/master-board/qt_gui/rf_dashboard_qt5 \
  --vision-device /dev/video9 \
  --vision-rtsp-url rtsp://192.168.30.26:8554/rk3568-001/cam0
```

不需要推流时：

```bash
./build/master-board/qt_gui/rf_dashboard_qt5 \
  --vision-device /dev/video9 --disable-vision-rtsp
```

禁用只关闭 RTSP 支路，本地推理/显示仍运行。

## 4. 观察

在能访问 server 的机器：

```bash
ffprobe -v error -rtsp_transport tcp \
  -show_streams rtsp://192.168.30.26:8554/rk3568-001/cam0
```

或：

```bash
ffplay -rtsp_transport tcp rtsp://192.168.30.26:8554/rk3568-001/cam0
```

同时观察 MQTT/日志：

```bash
mosquitto_sub -h 192.168.30.26 -p 1883 \
  -t 'argi/device/rk3568-001/stream/status' -v
```

成功标准：GUI frame/FPS 正常；`stream/status` 为 online；`ffprobe` 显示 H.264 video；播放器看到与本地检测相符的标注画面。

## 5. 状态语义

stream status payload 含 `device_id/type/state/protocol=rtsp/codec=h264/url`，失败时有 `reason`。它 retained。

- `open_failed`：encoder/AVFormat/server open 失败；每次重试都可能再次发布。
- `push_failed`：已 open 后写 frame 失败，支路会关闭并重试。
- `post_stream_failed`：标注后的 stream frame 构建失败，RTSP branch 被禁用，推理/显示保留。
- `stopped`：stream thread 正常停止。
- `disabled`：CLI 禁用 RTSP。

MQTT 本身断开时，GUI 日志仍是主要证据，不能因没收到 status 就断言 stream 没运行。

## 6. 排错

1. `open_failed`：确认 server 运行、地址/端口/path、发布权限、防火墙；用 server 日志确认是拒绝写入还是网络不可达。
2. MPP init/config 失败：检查 RK3568 MPP runtime、设备权限、分辨率/stride 与库版本。
3. `push_failed`：检查 server 中途断开、网络、AVFormat 错误和输入 frame 连续性。
4. ffprobe 404/无流：确认 producer 已 online；多数 server 只有 publisher 成功后才创建 path。
5. 画面花屏：检查 NV12 偶数几何、stride/vertical stride 和 source format；不要绕过 size checks。
6. 推流失败但 GUI 正常：这是预期降级边界，聚焦 stream thread/server。

## 7. 清理

关闭 GUI 后，worker 会停止 queue、join stream thread、写 trailer/关闭 AVIO 并释放 MPP。再停止 subscriber/player；RTSP server 是否删除 path 由 server 管理。不要在 GUI 仍写流时卸载媒体驱动或直接杀服务。

## 8. 验证边界

主机端未连接真实 server，也不能模拟 RK3568 MPP。本文命令必须在部署环境执行，记录 server 日志、stream status 与 `ffprobe` 结果，才能声称推流通过。
