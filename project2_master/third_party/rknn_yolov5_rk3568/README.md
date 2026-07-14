# RKNN YOLOv5 RK3568 项目适配

## 定位与来源边界

本子树源自 RKNN YOLOv5 demo，并已为 Project2 的 V4L2/文件输入、RGA、模型池、帧池、MPP 解码/编码和 RTSP 输出做项目维护适配。`src/postprocess.cc` 也明确标为 adapted，而不是“官方 demo 未修改副本”。

当前仓库没有在本子树提供一份完整的上游版本号、commit 与许可证清单。因此不能从本 README 推断所有 SDK header、预编译库、model 或上游源码都采用同一许可证；再分发/升级前必须核对对应 Rockchip SDK/demo 与模型的原始许可，并补齐 provenance 记录。

## 项目维护与原始依赖

| 范围 | 分类 |
|---|---|
| `include/*.h`、`src/*.cc` | Project2 构建和维护的适配源码；修改需跑相应 host/board test |
| `model/coco_80_labels_list.txt` | 运行资产和 lifecycle CTest 输入 |
| `model/*.rknn` | 预编译目标模型，不能通过源码审阅验证其生成来源/精度 |
| `3rdparty/` | 原始 SDK headers/prebuilt libraries，视为外部依赖，不在本文声称项目维护实现 |

不要在升级时把 `3rdparty/` 的二进制变化与项目源码适配混成一个无来源更新。

## 模块

- `v4l2_capture.*`：camera buffer/format/stream 生命周期。
- `mpp_decoder.*`：文件 demux 与 MPP H.264/H.265/VP9 decode。
- `preprocess.*`：RGA resize/format conversion 与 NV12 生成。
- `rkYolov5s.*`、`rknnPool.hpp`：模型加载、context/worker 和 inference。
- `postprocess.*`：YOLO decode/NMS/label。
- `frame_pools.h`：跨线程 frame ownership、release callback 和 stop/wakeup。
- `mpp_encoder_rtsp.*`：MPP H.264 与 FFmpeg libavformat RTSP output。

## 模型与 label

VisionRuntime 优先查可执行文件旁 `model/`，再查项目 third_party assets。`rkYolov5s` 由 model 路径推导同目录 `coco_80_labels_list.txt`。

label 存储是 mutex 保护的进程级状态：首次加载失败可重试；成功后不可变并保留到进程结束；兼容 `deinitPostProcess` 不释放字符串；另一个实例若提供冲突 label 路径会初始化失败。这样避免 detection result 引用已释放或被替换的 `c_str()`。

## 可移植测试

```bash
cmake -S project2_master -B build/vendor-test \
  -DBUILD_LINUX_APP=OFF -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/vendor-test
ctest --test-dir build/vendor-test -R postprocess_labels_lifetime --output-on-failure
```

该测试不需要 RKNN runtime，覆盖 label load、空 postprocess、兼容 deinit、二次使用与冲突路径。没有覆盖并发首次初始化、failed-load retry、真实模型输出和 target NPU。

## 板端验收

1. 核对 RKNN/RGA/MPP/FFmpeg runtime 与预编译 headers/libs 匹配目标镜像。
2. 核对模型面向 RK3568，labels 数量/顺序与模型一致。
3. 运行 Qt Vision，观察 model init、camera/file decode、frame FPS 与 detection。
4. 分别验证 RGA RGB path 与 post-stream NV12/RTSP path。
5. 停止时确认 pool notify、thread join、buffer release 和 media close 无 hang/leak。

主机 lifecycle CTest 不能证明模型精度、NPU context、零拷贝或媒体运行。更新依赖时记录上游来源、版本、许可证、hash、目标镜像与回归结果。
