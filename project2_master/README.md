# Project2 Master：RK3568 驱动、网关与 Qt 视觉

本目录是板端主控实现。RF 路径从 serdev 驱动到 `/dev/rf433`、`rf_gateway`、MQTT/Qt；视觉路径在 Qt 进程内完成 V4L2/文件输入、MPP/RGA/RKNN、显示和可选 RTSP。

## 目录

| 路径 | 内容 |
|---|---|
| `common/` | master 使用的 AA55 encoder |
| `linux_driver/` | `rf433_drv` 与共享 ioctl ABI |
| `linux_app/` | `rf_gateway_core`、网关 CLI、Mosquitto publisher |
| `qt_gui/` | Qt5 RF/视觉/日志界面与板端 VisionRuntime |
| `tests/` | RF decode 与标签生命周期 CTest |
| `third_party/rknn_yolov5_rk3568/` | 项目维护的 RKNN demo 适配和依赖边界 |

## 构建选项

顶层 CMake 选项都是布尔值，默认均为 ON：`BUILD_LINUX_APP`、`BUILD_QT5_GUI`、CTest 的 `BUILD_TESTING`。只做主机契约验证：

```bash
cmake -S project2_master -B build/master-tests \
  -DBUILD_LINUX_APP=OFF -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/master-tests
ctest --test-dir build/master-tests --output-on-failure
```

Linux RF 网关需要 libmosquitto：

```bash
cmake -S project2_master -B build/master \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/master
./build/master/linux_app/rf_gateway --help
```

`rf_gateway_core` 当前包含 `rf_epoll.c`、`rf_decode.c`、`rf_decode_c.c`、`rf_source.c` 和 `../common/rf_protocol.c`。master CLI 只接受 `/dev/rf433`，不能用普通文件冒充板端输入。

Qt 构建依赖 Qt5 Widgets 以及匹配 RK3568 的 RGA/RKNN/MPP/FFmpeg 头文件和库；普通 PC 可验证可移植测试，但不能代表板端 GUI/视觉 target 可运行。板端需要另用 `-DBUILD_QT5_GUI=ON` 配置并构建（见 [视觉运行手册](docs/rk3568_vision_dashboard.md)），下方 Qt 命令假设该 target 已生成。

## 运行

驱动和设备准备见 [linux_driver/README.md](linux_driver/README.md)。网关：

```bash
./build/master/linux_app/rf_gateway --rf-input /dev/rf433 \
  --stable-repeat 2 --stable-window 12 --stable-near-bits 4 \
  --min-publish-confidence 0.72 --publish-gap 6
```

Qt：

```bash
./build/master/qt_gui/rf_dashboard_qt5 \
  --rf-input /dev/rf433 --vision-device /dev/video9 \
  --vision-rtsp-url rtsp://192.168.30.26:8554/rk3568-001/cam0
```

`--disable-vision-rtsp` 可只保留本地推理/显示。RF 路径固定 `/dev/rf433`；vision 接受 `/dev/video*` 或可读本地视频文件。

## 固定服务配置与行为

MQTT broker `192.168.30.26:1883`，device `rk3568-001`，根 topic `argi/device/rk3568-001`。`status` 和 `stream/status` retained；`rf/event`、`rf/stats`、`vision/detection` non-retained。broker 断开时本地 JSON/GUI 继续运行，envelope 中 `mqtt_published=false`。

检测 publish 要同时满足 RGB 转换成功和检测组非空。RTSP encoder 打不开时每次重试都可能 publish `open_failed`，而不是只有首次状态变化一次。

## 阅读与排错入口

- [阅读路线](docs/project2_master_reading_guide_zh.md)
- [driver 操作教程](linux_driver/README.md) / [driver 设计](linux_driver/DESIGN_rf433_drv_v2.md)
- [userland 深入](docs/project2_master_userland_deep_dive.md)
- [Qt/Vision 深入](docs/project2_master_qt_vision_deep_dive.md)
- [MQTT 教程](docs/mqtt_publish_tutorial_zh.md) / [RTSP 教程](docs/rtsp_push_tutorial_zh.md)
- [函数索引](docs/project2_master_function_index.md)

当前主机验证覆盖 CMake/CTest、协议/解码、标签生命周期及 host-compilable Qt 表面；不覆盖目标内核、真实摄像头/RKNN/RGA/MPP、MQTT 服务或 RTSP server。
