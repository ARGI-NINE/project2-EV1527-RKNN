# RK3568 视觉看板运行手册

## 用途

快速部署板端 `rf_dashboard_qt5`。原理和所有权细节见 [Qt/Vision 深入指南](project2_master_qt_vision_deep_dive.md)。

## 前置检查

```bash
ls -l /dev/rf433 /dev/video9
```

还需 Qt5、RGA、RKNN runtime、MPP、FFmpeg/libavformat 与模型/labels。模型搜索可执行文件旁 `model/`，再查项目 third_party assets；label 必须与选定 model 同目录。

## 构建与运行

```bash
cmake -S project2_master -B build/master-board \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=ON -DBUILD_TESTING=ON
cmake --build build/master-board

./build/master-board/qt_gui/rf_dashboard_qt5 \
  --rf-input /dev/rf433 \
  --vision-device /dev/video9 \
  --vision-rtsp-url rtsp://192.168.30.26:8554/rk3568-001/cam0
```

测试本地视频可把 `--vision-device` 指向可读文件；关闭推流用 `--disable-vision-rtsp`。

## 成功标准

- RF 页收到 gateway 的 NDJSON 并更新 event/waveform/stats。
- Vision 页显示 camera online、model loaded、frame count 与 FPS。
- RGB 转换成功时显示 annotated frame；检测非空时才发布 `vision/detection`。
- RTSP server 可写时 stream status online 且播放器可看到 H.264；禁用/失败不会停止本地推理。

## 固定默认

RF `/dev/rf433`；camera `/dev/video9`；MQTT `192.168.30.26:1883`；RTSP `rtsp://192.168.30.26:8554/rk3568-001/cam0`。stream status retained，detection non-retained。encoder 打不开时 `open_failed` 可随重试重复。

## 排错顺序

1. GUI 不启动：先查 Qt platform plugin/display，再查板端动态库路径。
2. RF offline：单独运行 `rf_gateway --rf-input /dev/rf433`。
3. model init failed：检查 model/labels 同目录、RKNN runtime 与目标芯片模型。
4. camera offline：权限、占用、格式和 `/dev/video*` 节点。
5. 有推理无 MQTT：确认 RGB 转换成功、检测非空、broker 连接。
6. RTSP offline：按 [RTSP 教程](rtsp_push_tutorial_zh.md) 分离 encoder、server 与 client 问题。

## 停止与限制

关闭窗口触发 Vision stop、queue notify、thread join 和资源反向释放。目标验收必须在 RK3568 完成；主机编译和 portable CTest 不能证明 camera/NPU/RGA/MPP/RTSP 运行。
