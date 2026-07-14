# Qt5 与板端 VisionRuntime 深入指南

## 目标与前置条件

本文面向维护 master Qt/视觉链路的开发者。普通主机可阅读/编译部分 Qt 表面；完整运行需要 RK3568 上匹配的 Qt5 Widgets、V4L2、RGA、RKNN、MPP、FFmpeg/libavformat 和模型资产。

## Qt 入口与参数

`app/main.cpp` 定义：

```text
--rf-input /dev/rf433
--vision-device /dev/video9
--vision-rtsp-url rtsp://192.168.30.26:8554/rk3568-001/cam0
--disable-vision-rtsp
```

RF 只能是默认 `/dev/rf433`。Vision 接受 `/dev/video*` 或存在、可读的本地视频文件。`MainWindow::setupRuntime` 创建 `RFGatewayClient` 和 `VisionRuntime`，backend 保存跨页面快照。

## RF 子进程

`RFGatewayClient::startGateway` 启动与 GUI 同目录的 `rf_gateway`，直接传 `defaultRFInputPath()`；旧符号 `resolvedRfInputPath` 已不存在。stdout 按换行拆成 envelope，stderr/异常退出进入诊断日志。`handleProtocolLine` 分派 RF event/status/stats，并且只在 envelope 的 `mqtt_published=true` 时记录 MQTT publish log。

## Vision 启动顺序

`VisionRuntime::workerLoop`：

1. 定位 model 与同目录 label 文件。
2. 初始化 `rknnPool<rkYolov5s>`。
3. 启动独立 Vision Mosquitto publisher；失败只记 warning。
4. 输入是 `/dev/video*` 时用 `V4L2Capture`，文件时用 `MppDecoder`。
5. source thread 把可用帧交给 AI pool；主 worker 取 detection 与 frame。
6. 可选 post-stream branch 构建标注 NV12；stream thread 用 MPP H.264 + libavformat RTSP。
7. 主 worker 检查几何/缓冲，RGA 转 RGB，更新 Qt；只有转换成功且 detection 非空才发布 MQTT。

## 队列、所有权与停止

frame pool/AI pool 有容量与 stop/notify 协议；每个外部 buffer 携带 release callback/context，主 worker 用完后释放。尺寸乘法、NV12 偶数几何、stride/capacity、null frame、库调用结果都保护真实外部边界，不能为了“代码更短”删除。

停止时设置 atomic running=false，通知各 getter/queue，停止 post-stream pool，join source/stream thread，然后按 opened/streaming 状态关闭 capture/decoder/encoder。资源反向释放顺序是稳定性契约。

## 标签生命周期

`postprocess.cc` 是从官方 demo 适配的项目维护版本。label 加载受 mutex 保护：失败可重试；首次成功后字符串不可变并保持整个进程生命周期；`deinitPostProcess` 为兼容 no-op。另一个模型若给出不同 label 路径会显式失败，而不是释放/替换被其它实例使用的 `c_str()`。

`postprocess_labels_lifetime` CTest 覆盖加载、二次使用、兼容 deinit 与冲突路径；未直接覆盖并发首次初始化和失败后重试。

## MQTT 与 RTSP 语义

- detection：RGB 成功 + `detGroup.count > 0`，non-retained。
- stream status：retained；`open_failed` 可在每次 open retry 发布。
- RTSP disabled：发布 offline/disabled（若 MQTT 可用），推理与显示继续。
- post-stream 或 push 失败：记录 warning、关闭/重试支路，不释放仍被其它线程拥有的 frame。

## 构建与运行

目标 SDK/sysroot 配好后：

```bash
cmake -S project2_master -B build/master-board \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=ON -DBUILD_TESTING=ON
cmake --build build/master-board
ctest --test-dir build/master-board --output-on-failure

./build/master-board/qt_gui/rf_dashboard_qt5 \
  --vision-device /dev/video9
```

若 CMake 找不到板端库，修复 toolchain/sysroot，不要链接来自不同系统的预编译库。

## 验收与排错

成功证据：模型加载、camera/file online、frame count/FPS 增长、Qt 显示标注；真实目标时 detection topic 与 RTSP 可按条件观察。

- model not found：确保可执行文件旁 `model/` 或 third_party assets 中模型与 labels 同目录。
- camera open/start 失败：权限、pixel format、设备节点占用。
- geometry changed：代码选择停止以避免 buffer/encoder mismatch；修复输入协商而非绕过检查。
- RGA color conversion failed：UI 可无 detection publish；核对格式/stride/库版本。
- repeated `open_failed`：每轮 retry 的真实结果，检查 RTSP server 写入能力。

主机验证不等价于 RK3568 零拷贝、NPU、摄像头或推流验证。简明运行手册见 [rk3568_vision_dashboard.md](rk3568_vision_dashboard.md)。
