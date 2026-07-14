# Project2 Master 阅读路线

这是一条不重复阅读的路线，适合新维护者在半天内建立当前代码模型。

## 先跑可移植测试

```bash
cmake -S project2_master -B build/master-read \
  -DBUILD_LINUX_APP=OFF -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/master-read
ctest --test-dir build/master-read --output-on-failure
```

先按平台核对结果：Unix 生成 `rf_decode_contract` 与 `postprocess_labels_lifetime`，应为 2/2；Windows 只生成可移植的 `postprocess_labels_lifetime`，应为 1/1。再读实际生成的测试源码，能快速识别稳定契约与板端未覆盖部分。

## 第一遍：RF 数据类型

1. `common/rf_protocol.h/.c`：AA55、LE16、最大 1024 pulse、XOR encoder。`crc8` 只是 legacy 名。
2. `linux_driver/rf433_ioctl.h`：kernel/user ABI 的 frame、stats、status、ioctl。
3. `linux_app/rf_decode.h`：解码输出和统计。

## 第二遍：从字节到事件

1. `linux_driver/rf433_drv.c`：`parser_feed_byte -> parser_emit_frame -> misc read`。
2. `linux_app/rf_source.c`：为什么只允许 `/dev/rf433`。
3. `linux_app/rf_epoll.c`：frame fd 与可选 stats timer。
4. `linux_app/rf_decode.c/rf_decode_c.c`：EV1527 约 50-pulse 解码。
5. `linux_app/main.c`：稳定组、confidence、去重、JSON/MQTT。

配套文档：[driver 设计](../linux_driver/DESIGN_rf433_drv_v2.md)、[userland 深入](project2_master_userland_deep_dive.md)、[共享协议](project2_shared_protocol_deep_dive.md)。

## 第三遍：Qt 与视觉

1. `qt_gui/core/app_options.h` 与 `app/main.cpp`：固定默认路径与 CLI 校验。
2. `qt_gui/rf/rf_gateway_client.cpp`：子进程 stdout envelope。
3. `qt_gui/core/dashboard_backend.*`：UI 状态存储。
4. `qt_gui/vision/vision_runtime.cpp`：资源拥有者、工作线程、AI/显示/RTSP/MQTT 分支。
5. `third_party/.../src/postprocess.cc` 与 `rkYolov5s.cc`：标签进程生命周期和路径冲突。

配套文档：[Qt/Vision 深入](project2_master_qt_vision_deep_dive.md)、[视觉运行说明](rk3568_vision_dashboard.md)、[MQTT](mqtt_publish_tutorial_zh.md)、[RTSP](rtsp_push_tutorial_zh.md)。

## 读代码时保持的边界

- driver 只负责 UART packet 到 `rf433_frame`，不做 EV1527/MQTT。
- MQTT 与 RTSP 失败是支路降级，不允许因此删除主链路资源检查。
- detection 发布还要求 RGB 转换成功；`open_failed` 可随重试重复。
- label 字符串是成功加载后的不可变进程级存储，兼容 deinit 不释放它。
- `stable_group.hits` 是 32 位饱和值，不回绕。

## 完成标准

能从一个 PA0 边沿追到 Qt/MQTT，能解释每层新增字段；能指出哪些测试在主机运行、哪些必须上 RK3568/内核/STM32；能在修改前找到对应 CTest 和验收教程。函数定位可查 [函数索引](project2_master_function_index.md)。
