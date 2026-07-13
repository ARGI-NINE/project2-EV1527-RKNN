# Vendored RK3568 Vision Runtime for `project2_master`

这个目录不是上游完整工程文档，也不是一个独立维护的通用 SDK 包。

它在本仓库里的唯一目的，是给 `project2_master/qt_gui` 提供本地板端视觉运行时所需的最小可用依赖集合，让 `VisionRuntime` 可以在同一个 Qt 可执行文件里完成：

- 摄像头采集
- 本地视频文件解码
- RKNN 推理
- 推理后 annotated frame 的 RTSP 编码与推流

## 1. 本仓库怎样使用这个目录

`project2_master/qt_gui/CMakeLists.txt` 会直接把这里的源码和库编进 `rf_dashboard_qt5`：

- `src/rkYolov5s.cc`
- `src/preprocess.cc`
- `src/postprocess.cc`
- `src/v4l2_capture.cc`
- `src/mpp_decoder.cc`
- `src/mpp_encoder_rtsp.cc`

同时会把这些头文件目录和预编译库目录加入构建：

- `include/`
- `3rdparty/rknn/include`
- `3rdparty/rga/include`
- `3rdparty/mpp/include`
- `3rdparty/rknn/lib/librknnrt.so`
- `3rdparty/rga/lib/librga.so`
- `3rdparty/mpp/lib/librockchip_mpp.so`

这意味着当前设计不是“运行时再启动一个外部视觉服务”，而是：

- Qt 目标直接链接这个 vendored runtime
- `VisionRuntime` 在 Qt 进程内直接调用这些实现

这个“直接编进 Qt 目标”的说法可以被 `project2_master/qt_gui/CMakeLists.txt` 原样托底：

```cmake
target_sources(
  rf_dashboard_qt5
  PRIVATE
    ${PROJECT2_MASTER_VISION_ROOT}/src/rkYolov5s.cc
    ${PROJECT2_MASTER_VISION_ROOT}/src/preprocess.cc
    ${PROJECT2_MASTER_VISION_ROOT}/src/postprocess.cc
    ${PROJECT2_MASTER_VISION_ROOT}/src/v4l2_capture.cc
    ${PROJECT2_MASTER_VISION_ROOT}/src/mpp_decoder.cc
    ${PROJECT2_MASTER_VISION_ROOT}/src/mpp_encoder_rtsp.cc
)

target_include_directories(
  rf_dashboard_qt5
  PRIVATE
    ${PROJECT2_MASTER_VISION_ROOT}/include
    ${PROJECT2_MASTER_VISION_ROOT}/3rdparty/mpp/include
    ${PROJECT2_MASTER_VISION_ROOT}/3rdparty/rknn/include
    ${PROJECT2_MASTER_VISION_ROOT}/3rdparty/rga/include
)

target_link_libraries(
  rf_dashboard_qt5
  PRIVATE
    ${PROJECT2_MASTER_VISION_ROOT}/3rdparty/rknn/lib/librknnrt.so
    ${PROJECT2_MASTER_VISION_ROOT}/3rdparty/rga/lib/librga.so
    ${PROJECT2_MASTER_VISION_ROOT}/3rdparty/mpp/lib/librockchip_mpp.so
)
```

这里没有“启动外部服务”的步骤，只有 `target_sources(...)`、`target_include_directories(...)` 和 `target_link_libraries(...)`。所以本 README 只把它当作 `rf_dashboard_qt5` 的 vendored runtime 资产，而不是单独部署的服务程序。

## 2. 当前真正被 `VisionRuntime` 用到的内容

### 2.1 头文件

- `include/rkYolov5s.hpp`
- `include/rknnPool.hpp`
- `include/preprocess.h`
- `include/postprocess.h`
- `include/v4l2_capture.h`
- `include/mpp_decoder.h`
- `include/mpp_encoder_rtsp.h`
- `include/frame_pools.h`

### 2.2 源文件

- `src/rkYolov5s.cc`
- `src/preprocess.cc`
- `src/postprocess.cc`
- `src/v4l2_capture.cc`
- `src/mpp_decoder.cc`
- `src/mpp_encoder_rtsp.cc`

### 2.3 模型和标签

- `model/yolov5s_relu-640-640-rk3568.rknn`
- `model/yolov5s-640-640.rknn`
- `model/coco_80_labels_list.txt`

`VisionRuntime::resolveModelPath()` 会优先从可执行文件旁边的 `model/` 查找，找不到再回到这个 vendor 根下的 `model/`。

本仓库对这份 runtime 的真实调用点在 `project2_master/qt_gui/vision/vision_runtime.cpp`。摘录如下：

```cpp
const QString modelPath = resolveModelPath();
const QString inputPath = options_.visionDevice.trimmed();
const bool useV4L2 = isAllowedVisionDevicePath(inputPath);

if (modelPath.isEmpty()) {
    backend_->addLog(
        "ERROR",
        "VISION",
        QStringLiteral("VisionRuntime could not find a usable RKNN model inside project2_master assets")
    );
    running_.store(false);
    return;
}

rknnPool<rkYolov5s> aiPool(modelPath.toStdString(), kAiWorkerThreads, kAiQueueSize);
if (aiPool.init() != 0) {
    backend_->addLog("ERROR", "VISION", QStringLiteral("VisionRuntime model init failed: %1").arg(modelPath));
    running_.store(false);
    return;
}
```

这段代码说明本仓库真正依赖的是 `resolveModelPath()` 的模型查找规则，以及 `rknnPool<rkYolov5s>` 这套 C++ 调用面。README 因此只讨论“Qt 如何吃这份 runtime”，不把注意力转到上游工程的其它工具链。

RTSP 支线也是通过同一个 Qt 进程直接调用 vendored encoder，而不是另起独立推流服务：

```cpp
if (encoder.open(
        rtspUrl.toUtf8().constData(),
        frame.width,
        frame.height,
        frame.stride,
        frame.height,
        fpsNum,
        fpsDen,
        kDefaultRtspBitrateBps) != 0) {
    backend_->addLog("WARN", "VISION", QStringLiteral("RTSP encoder open failed; stream branch will retry"));
    publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), rtspUrl, QStringLiteral("open_failed"));
    nextRetryAt = now + std::chrono::milliseconds(kRtspRetryDelayMs);
    release_frame_buffer(frame.data, frame.release_fn, frame.release_ctx);
    continue;
}
```

所以对当前仓库来说，`mpp_encoder_rtsp.*` 的意义不是“提供一个外置 RTSP daemon”，而是“作为 `VisionRuntime` 的一个进程内后处理分支被调用”。

## 3. 当前代码里各文件的角色

| 文件 | 在本仓库中的作用 |
|---|---|
| `v4l2_capture.*` | `/dev/video*` 摄像头采集 |
| `mpp_decoder.*` | 本地视频文件解码，供 `--vision-device <file>` 支路使用 |
| `rkYolov5s.*` + `rknnPool.hpp` | RKNN 模型实例和 worker 池 |
| `frame_pools.h` | AI 输入池、RTSP annotated frame 队列和释放契约 |
| `mpp_encoder_rtsp.*` | 把 post-infer annotated NV12 frame 编成 H.264 并推到 RTSP URL |

## 4. 这个 vendor 目录不表达什么

它不表达：

- 上游项目全部功能都被保留
- 这里的 README 可以代替上游说明
- 所有示例、脚本、工具都在本仓库使用

也不要把这里当成“完整上游镜像”。对当前仓库来说，它只是 `project2_master` 的一组内聚运行时资产。

## 5. 依赖边界

除了这个目录自身提供的头文件、源码、预编译库和模型，`qt_gui` 还依赖外部系统库：

- Qt5 Core/Gui/Widgets
- `libavformat`
- `libavcodec`
- `libavutil`
- `libmosquitto`
- `Threads`

因此，“把这个目录拷过去”本身不等于可运行，外部系统依赖仍然要满足。

## 6. 更新这个 vendor 目录时要同步检查什么

如果你更新了这里的内容，至少同步检查：

1. `project2_master/qt_gui/CMakeLists.txt` 里的源码清单、include 路径和链接库。
2. `project2_master/qt_gui/vision/vision_runtime.cpp` 里使用的接口是否仍兼容。
3. `mpp_encoder_rtsp.*` 与 `frame_pools.h` 的 `PostStreamFrame` 契约是否仍一致。
4. `model/` 下模型文件名是否仍匹配 `resolveModelPath()` 的查找规则。

## 7. 与文档的对应关系

如果你要结合代码理解这个目录，建议按下面顺序看：

1. [../../docs/rk3568_vision_dashboard.md](../../docs/rk3568_vision_dashboard.md)
2. [../../docs/mqtt_publish_tutorial_zh.md](../../docs/mqtt_publish_tutorial_zh.md)
3. [../../docs/rtsp_push_tutorial_zh.md](../../docs/rtsp_push_tutorial_zh.md)
4. [../../docs/project2_master_qt_vision_deep_dive.md](../../docs/project2_master_qt_vision_deep_dive.md)

这更符合本仓库“先看 Qt 如何调用 vendor，再回头看 vendor 提供了什么”的阅读方向。
