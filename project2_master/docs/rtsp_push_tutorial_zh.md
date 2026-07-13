# `project2_master` RTSP 推流教程

这篇教程只讲当前仓库里已经存在的 RTSP 推流实现，而且只讲板端 VisionRuntime 这条链。

它不会把下面这些尚未实现的能力写成现状：

- recorder
- 事件录像
- `record_done`
- MQTT command 控制推流或录像

如果你第一次读这条代码，先记住一句话：

当前 RTSP 不是“把摄像头原始帧直接发出去”，而是“把推理后的 annotated NV12 frame 编成 H.264，再通过 FFmpeg 作为 RTSP client 推到目标地址”。

## 1. 先讲概念：RTSP、编码器、时间戳分别是什么

### 1.1 RTSP 在这里扮演什么角色

RTSP 是媒体会话控制协议。对当前项目来说，它只承担一件事：

- 把板端视觉链产出的 H.264 视频推到一个 RTSP 地址

当前代码不是在本地起一个 RTSP server 让别人来拉，而是：

- 用 FFmpeg 的 RTSP 输出能力
- 主动往 `rtsp://...` 目标写

默认目标地址是：

- `rtsp://192.168.30.26:8554/rk3568-001/cam0`

### 1.2 为什么还要编码

输入帧可能来自：

- V4L2 摄像头
- 本地视频文件解码

推理和叠框后拿到的是 CPU 可见的图像缓冲，而网络传输不适合直接发原始像素流，所以还要：

- 把 NV12 喂给 Rockchip MPP
- 编码成 H.264 packet
- 再用 FFmpeg 写到 RTSP

### 1.3 时间戳为什么重要

如果推流时间戳乱了，接收端可能出现：

- 播放卡顿
- 时间轴跳动
- 帧间隔异常

当前代码的思路是：

- 从采集或解码侧拿 `captureTsUs`
- 把它一路带到 `PostStreamFrame.timestamp_us`
- 最终在 `MppRtspEncoder::writeMppPacket()` 里换算成 `pts/dts`

## 2. 先看总链路

```text
sourceThread
  -> copyFrameToAiPool()
  -> aiPool
  -> aiPool.get()
     -> copyPostInferFrameToPostStreamPool()
        -> PostStreamFramePool
           -> streamThread
              -> MppRtspEncoder::open()
              -> MppRtspEncoder::encodeAndPush()
                 -> MPP H.264 encode
                 -> FFmpeg RTSP output
```

真正的 RTSP 主线只有后半段：

- `copyPostInferFrameToPostStreamPool()`
- `PostStreamFramePool`
- `streamThread`
- `MppRtspEncoder`

## 3. 开关和默认值

RTSP 相关 CLI 开关都在 `qt_gui/app/main.cpp`：

| 开关 | 默认值 | 含义 |
|---|---|---|
| `--vision-rtsp-url` | `rtsp://192.168.30.26:8554/rk3568-001/cam0` | 推流目标地址 |
| `--disable-vision-rtsp` | 无 | 完全禁用 RTSP 支路 |

`AppOptions` 里对应字段是：

- `visionRtspEnabled`
- `visionRtspUrl`

禁用 RTSP 时并不是整个 VisionRuntime 不工作，而是：

- 仍然采帧
- 仍然做推理
- 仍然刷新 UI
- 仍然可以发 detection MQTT
- 只是不会起 `streamThread`

## 4. RTSP 为什么不直接吃原始输入帧

这是设计里最容易误解的点。

当前 RTSP 要的不是：

- 摄像头原始 YUYV
- 解码器吐出来的裸 NV12
- `VisionPage` 里的显示截图

当前 RTSP 要的是：

- 已经经过推理
- 已经有检测结果
- 已经把框和标签烧进画面
- 最终格式还是适合编码的 NV12

这就是文档里必须写“annotated frame”的原因。

## 5. `copyPostInferFrameToPostStreamPool()` 先把 annotated frame 做出来

这是真正的“RTSP 供帧函数”。

它的输入包括：

- `detect_result_group_t group`
- `srcData`
- `width / height / srcFormat`
- `frameId`
- `captureTsUs`
- `streamWidth / streamHeight / streamStride`
- `postStreamBufferPool`
- `postStreamOverlayPool`
- `postStreamFramePool`

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`copyPostInferFrameToPostStreamPool()`
作用：把 `aiPool.get()` 取回的源帧转成可推流的 annotated NV12，并把释放契约一起塞进 `PostStreamFramePool`。

```cpp
int copyPostInferFrameToPostStreamPool(
    const detect_result_group_t &group,
    const void *srcData,
    int width,
    int height,
    int srcFormat,
    uint64_t frameId,
    long long captureTsUs,
    int streamWidth,
    int streamHeight,
    int streamStride,
    FrameCopyPool *postStreamBufferPool,
    FrameCopyPool *postStreamOverlayPool,
    PostStreamFramePool *postStreamFramePool
) {
    const size_t streamBytes = computeNv12Bytes(streamStride, streamHeight);
    const size_t overlayBytes = computeBgraBytes(streamWidth, streamHeight);
    unsigned char *streamBuffer = nullptr;
    unsigned char *overlayBuffer = nullptr;

    if (
        srcData == nullptr ||
        postStreamBufferPool == nullptr ||
        postStreamOverlayPool == nullptr ||
        postStreamFramePool == nullptr ||
        width <= 0 ||
        height <= 0 ||
        streamWidth <= 0 ||
        streamHeight <= 0 ||
        streamStride < streamWidth ||
        streamBytes == 0U ||
        overlayBytes == 0U ||
        streamBytes > postStreamBufferPool->bufferSize() ||
        overlayBytes > postStreamOverlayPool->bufferSize()
    ) {
        return -1;
    }

    streamBuffer = postStreamBufferPool->acquire();
    if (streamBuffer == nullptr) {
        return 1;
    }

    overlayBuffer = postStreamOverlayPool->acquire();
    if (overlayBuffer == nullptr) {
        release_frame_buffer(streamBuffer, release_pooled_buffer, postStreamBufferPool);
        return 1;
    }

    if (rga_resize_convert_vaddr(
            const_cast<void *>(srcData),
            width,
            height,
            srcFormat,
            overlayBuffer,
            streamWidth,
            streamHeight,
            RK_FORMAT_BGRA_8888) != 0) {
        release_frame_buffer(overlayBuffer, release_pooled_buffer, postStreamOverlayPool);
        release_frame_buffer(streamBuffer, release_pooled_buffer, postStreamBufferPool);
        return -1;
    }

    QImage overlayFrame(
        overlayBuffer,
        streamWidth,
        streamHeight,
        streamWidth * 4,
        QImage::Format_ARGB32
    );
    QPainter overlayPainter(&overlayFrame);
    paintDetections(&overlayPainter, group, width, height, streamWidth, streamHeight);

    if (rga_resize_to_nv12_vaddr(
            overlayBuffer,
            streamWidth,
            streamHeight,
            streamWidth,
            streamHeight,
            RK_FORMAT_BGRA_8888,
            streamBuffer,
            streamWidth,
            streamHeight,
            streamStride,
            streamHeight) != 0) {
        release_frame_buffer(overlayBuffer, release_pooled_buffer, postStreamOverlayPool);
        release_frame_buffer(streamBuffer, release_pooled_buffer, postStreamBufferPool);
        return -1;
    }

    release_frame_buffer(overlayBuffer, release_pooled_buffer, postStreamOverlayPool);

    PostStreamFrame frame;
    frame.data = streamBuffer;
    frame.size = streamBytes;
    frame.width = streamWidth;
    frame.height = streamHeight;
    frame.stride = streamStride;
    frame.format = RK_FORMAT_YCbCr_420_SP;
    frame.frame_id = frameId;
    frame.timestamp_us = captureTsUs;
    frame.release_fn = release_pooled_buffer;
    frame.release_ctx = postStreamBufferPool;
    postStreamFramePool->enqueue(std::move(frame));
    return 0;
}
```

这段代码把 5.1 到 5.5 五步一次性串起来了：先从两个池申请 NV12 和 BGRA 缓冲，再用 RGA 做格式变换，用 `QPainter` 在 BGRA 面上烧框，最后回到 NV12 并构造 `PostStreamFrame`。这里最关键的不是“转了一次格式”，而是 `frame.release_fn` / `frame.release_ctx` 被原样带进队列，所以下游线程拿到的不只是像素数据，还有明确的归还路径。

### 5.1 第一步：申请两块缓冲

它会分别申请：

- 一块 NV12 输出缓冲 `streamBuffer`
- 一块 BGRA overlay 工作缓冲 `overlayBuffer`

两块都来自各自的 `FrameCopyPool`，目的是隔离生命周期，而不是让多个线程共享同一片图像内存。

### 5.2 第二步：把源帧转成 BGRA 工作面

调用：

- `rga_resize_convert_vaddr(..., RK_FORMAT_BGRA_8888)`

这样做的原因很实际：

- Qt 的 `QPainter` 在 BGRA/ARGB 工作面上画框最方便
- 直接对 NV12 画框既不直观，也不利于复用现有绘制逻辑

### 5.3 第三步：在 BGRA 上画框

代码里复用的是：

- `paintDetections(...)`

画上去的信息包括：

- 目标框
- 标签文字
- 置信度百分比

这里画出来的是“将来要进入编码器的那一版画面”，不是只给 UI 用的临时显示。

### 5.4 第四步：把 BGRA 再转回 NV12

调用：

- `rga_resize_to_nv12_vaddr(...)`

输出目标是：

- `streamBuffer`

到这一步，annotated frame 已经变成了编码器喜欢的 NV12。

### 5.5 第五步：构造 `PostStreamFrame`

最后塞进队列的字段包括：

| 字段 | 含义 |
|---|---|
| `data` | NV12 数据指针 |
| `size` | 有效字节数 |
| `width` | 目标宽 |
| `height` | 目标高 |
| `stride` | 行跨度 |
| `format` | 当前固定是 `RK_FORMAT_YCbCr_420_SP` |
| `frame_id` | 帧号 |
| `timestamp_us` | 捕获时间戳 |
| `release_fn` | 释放函数 |
| `release_ctx` | 释放上下文 |

这一步非常关键，因为它把“画面内容”和“释放契约”一起打包交给下游线程。

## 6. `PostStreamFramePool` 的语义不是简单队列

`PostStreamFramePool` 当前是一个有上限的队列，默认上限来自：

- `kPostStreamQueueSize = 4`

它有三个行为值得专门记住。

代码来源：`project2_master/third_party/rknn_yolov5_rk3568/include/frame_pools.h`
函数 / 类型：`PostStreamFrame`、`PostStreamFramePool::enqueue()`、`waitAndPop()`、`stop()`、`clear()`
作用：定义 RTSP 支路交接帧的数据契约，以及队列满、阻塞等待、停机清理时的真实行为。

```cpp
struct PostStreamFrame {
    unsigned char* data{nullptr};
    size_t size{0};
    int width{0};
    int height{0};
    int stride{0};
    int format{0};
    uint64_t frame_id{0};
    long long timestamp_us{0};
    FrameReleaseFn release_fn{nullptr};
    void* release_ctx{nullptr};
};

class PostStreamFramePool {
public:
    explicit PostStreamFramePool(size_t max_queue_size)
        : max_queue_size_(max_queue_size == 0 ? 1 : max_queue_size) {}

    void enqueue(PostStreamFrame&& frame) {
        PostStreamFrame dropped;
        bool has_dropped = false;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (stop_) {
                has_dropped = true;
                dropped = frame;
            } else {
                if (queue_.size() >= max_queue_size_) {
                    dropped = queue_.front();
                    queue_.pop_front();
                    has_dropped = true;
                    dropped_count_.fetch_add(1, std::memory_order_relaxed);
                }
                queue_.push_back(frame);
            }
        }

        if (has_dropped) {
            release_frame_buffer(dropped.data, dropped.release_fn, dropped.release_ctx);
        }
        cv_.notify_one();
    }

    bool waitAndPop(PostStreamFrame* out_frame) {
        if (!out_frame) {
            return false;
        }

        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (queue_.empty()) {
            return false;
        }

        *out_frame = queue_.front();
        queue_.pop_front();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
    }

    void clear() {
        std::deque<PostStreamFrame> pending;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            pending.swap(queue_);
        }

        for (PostStreamFrame& frame : pending) {
            release_frame_buffer(frame.data, frame.release_fn, frame.release_ctx);
            frame = PostStreamFrame{};
        }
    }
};
```

这里能直接看出三个文档里很容易写泛的点。第一，`PostStreamFrame` 不只是 `data + size`，而是把 `frame_id`、`timestamp_us`、`release_fn`、`release_ctx` 都一起交给下游。第二，`enqueue()` 在满队列时不是阻塞上游，而是丢 `front()` 并立即释放旧帧。第三，`stop()` 和 `clear()` 的职责分离得很清楚：前者只负责唤醒等待者，后者才真正回收队列里遗留的帧。

### 6.1 队列满了会丢最旧帧

当 `enqueue()` 时队列已满：

- 丢掉 `queue_.front()`
- 递增 `dropped_count_`
- 保留新帧

这说明 RTSP 支路优先追最新画面，而不是保证每帧必达。

### 6.2 `waitAndPop()` 会阻塞等待

`streamThread` 用它阻塞等待帧或 stop 信号。

这使得 RTSP 线程空闲时不会忙等耗 CPU。

### 6.3 `stop()` 和 `clear()`

`stop()` 会：

- 设置 `stop_ = true`
- 唤醒等待线程

`clear()` 会：

- 清空队列中剩余帧
- 调回每帧自带的释放函数

这也是为什么 `PostStreamFrame` 里必须带 `release_fn/release_ctx`。

## 7. `streamThread` 是 RTSP 支路的调度器

`VisionRuntime::workerLoop()` 只有在 `rtspEnabled` 为真时才创建 `streamThread`。

线程体内部维护四个状态变量：

- `MppRtspEncoder encoder`
- `bool encoderOpen`
- `bool streamWasOnline`
- `nextRetryAt`

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`VisionRuntime::workerLoop()` 里的 `streamThread` lambda
作用：作为 RTSP 支路的独立消费者，负责懒打开编码器、失败重试、在线离线状态发布和帧回收。

```cpp
if (rtspEnabled) {
    streamThread = std::thread([&]() {
        MppRtspEncoder encoder;
        bool encoderOpen = false;
        bool streamWasOnline = false;
        auto nextRetryAt = std::chrono::steady_clock::time_point::min();

        while (true) {
            PostStreamFrame frame;
            if (!postStreamFramePool.waitAndPop(&frame)) {
                break;
            }

            const auto now = std::chrono::steady_clock::now();
            if (!encoderOpen) {
                if (now < nextRetryAt) {
                    release_frame_buffer(frame.data, frame.release_fn, frame.release_ctx);
                    continue;
                }

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

                encoderOpen = true;
                streamWasOnline = true;
                backend_->addLog("INFO", "VISION", QStringLiteral("RTSP push online: %1").arg(rtspUrl));
                publishStreamStatus(backend_, &mqtt, QStringLiteral("online"), rtspUrl);
            }

            if (encoder.encodeAndPush(frame) != 0) {
                backend_->addLog("WARN", "VISION", QStringLiteral("RTSP push failed; stream branch will retry"));
                publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), rtspUrl, QStringLiteral("push_failed"));
                encoder.close();
                encoderOpen = false;
                nextRetryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(kRtspRetryDelayMs);
                release_frame_buffer(frame.data, frame.release_fn, frame.release_ctx);
                continue;
            }

            release_frame_buffer(frame.data, frame.release_fn, frame.release_ctx);
        }

        if (encoderOpen || streamWasOnline) {
            publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), rtspUrl, QStringLiteral("stopped"));
        }
        encoder.close();
    });
} else {
    backend_->addLog("INFO", "VISION", QStringLiteral("RTSP push disabled by option"));
    publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), rtspUrl, QStringLiteral("disabled"));
}
```

这段线程体把“RTSP 只是支路，不是主链致命点”写得非常直白。无论是 `open_failed` 还是 `push_failed`，处理方式都是发状态、延迟重试、释放当前帧，而不是让 `workerLoop()` 直接退出。只有 `waitAndPop()` 因 `stop()` 被唤醒且队列为空时，这个线程才自然收尾。

### 7.1 为什么是懒打开 encoder

当前代码不是一启动视觉链就立刻打开 RTSP。

它要等第一帧 `PostStreamFrame` 到来再 `encoder.open(...)`。

这样做有两个好处：

- 只有真的有流量时才建立输出会话
- encoder 初始化参数直接来自实际送来的帧几何信息

### 7.2 打不开时怎么处理

如果 `encoder.open(...) != 0`：

- 记 `WARN` 日志
- 发布 `stream/status = offline, reason=open_failed`
- 设置下次重试时间 `now + 3000ms`
- 当前帧释放掉

这不是致命错误，不会退出主视觉链。

### 7.3 推流成功上线时怎么处理

首次打开成功时：

- `encoderOpen = true`
- `streamWasOnline = true`
- 记 `INFO` 日志 `RTSP push online`
- 发布 `stream/status = online`

### 7.4 编码或写流失败时怎么处理

如果 `encoder.encodeAndPush(frame) != 0`：

- 记 `WARN`
- 发布 `stream/status = offline, reason=push_failed`
- `encoder.close()`
- `encoderOpen = false`
- 进入重试等待窗口

同样，这仍然不是整条视觉链的致命错误。

### 7.5 退出时怎么处理

线程退出前，如果：

- `encoderOpen == true`
- 或 `streamWasOnline == true`

就会再发一次：

- `stream/status = offline, reason=stopped`

## 8. `MppRtspEncoder` 到底做了什么

这个类分成四个主要职责：

1. 初始化 Rockchip MPP H.264 编码器
2. 初始化 FFmpeg RTSP 输出上下文
3. 把 `PostStreamFrame` 送入编码器
4. 把编码后的 packet 写到 RTSP

### 8.1 `open()`

`open()` 先记下：

- `rtsp_url_`
- `width_ / height_`
- `hor_stride_ / ver_stride_`
- `fps_num_ / fps_den_`
- `bitrate_bps_`

其中 stride 会做对齐：

- `hor_stride_ = align_up(max(width, hor_stride), 16)`

码率如果传进来是 `0`，会调用 `clamp_bitrate()` 自动估算并夹在 1 Mbps 到 8 Mbps 之间。

代码来源：`project2_master/third_party/rknn_yolov5_rk3568/src/mpp_encoder_rtsp.cc`
函数：`MppRtspEncoder::open()`
作用：锁定 RTSP URL、帧几何、FPS、码率，并串起 `initMpp()` 与 `initRtspOutput()` 两段初始化。

```cpp
int MppRtspEncoder::open(const char* rtsp_url,
                         int width,
                         int height,
                         int hor_stride,
                         int ver_stride,
                         int fps_num,
                         int fps_den,
                         int bitrate_bps) {
    close();

    rtsp_url_ = rtsp_url ? rtsp_url : "";
    width_ = width;
    height_ = height;
    hor_stride_ = align_up(std::max(width, hor_stride), 16);
    ver_stride_ = std::max(height, ver_stride);
    fps_num_ = (fps_num > 0) ? fps_num : 30;
    fps_den_ = (fps_den > 0) ? fps_den : 1;
    bitrate_bps_ = clamp_bitrate(width_, height_, fps_num_, fps_den_, bitrate_bps);
    first_capture_ts_us_ = -1;
    last_capture_ts_us_ = -1;
    io_opened_ = false;
    header_written_ = false;

    if (rtsp_url_.empty() || width_ <= 0 || height_ <= 0) {
        fprintf(stderr, "RTSP encoder: invalid open arguments\n");
        return -1;
    }

    if (initMpp() != 0) {
        close();
        return -1;
    }

    if (initRtspOutput() != 0) {
        close();
        return -1;
    }

    return 0;
}
```

这里的关键不是简单“开一下编码器”，而是把后续所有时序和 buffer 契约都钉死：`hor_stride_` 会做 16 字节对齐，`bitrate_bps_` 会在这里最终确定，`first_capture_ts_us_` / `last_capture_ts_us_` 也会在每次重开时归零，所以一次 `open()` 本质上就是一次全新的推流会话。

### 8.2 `initMpp()`

这一段把编码器准备好：

- `mpp_create`
- `mpp_init(..., MPP_CTX_ENC, MPP_VIDEO_CodingAVC)`
- `mpp_enc_cfg_init`
- 设置低延迟
- 设置宽高和 stride
- 设置输入格式 `MPP_FMT_YUV420SP`
- 设置 CBR
- 设置 `fps_in` / `fps_out`
- 设置 `gop`
- 申请一块内部 `input_buffer_`

所以从这里开始，输入格式已经被钉死成：

- H.264 encoder 吃 NV12

### 8.3 `loadHeadersIntoStream()`

这一步从 MPP 拿 H.264 headers：

- 优先 `MPP_ENC_GET_HDR_SYNC`
- 不行再 `MPP_ENC_GET_EXTRA_INFO`

然后填进 `video_stream_->codecpar->extradata`

这一步是 FFmpeg 写 RTSP header 前必须做的准备。

### 8.4 `initRtspOutput()`

FFmpeg 侧做这些事情：

- `avformat_network_init()`
- `avformat_alloc_output_context2(..., "rtsp", rtsp_url_)`
- 新建 `AVStream`
- 设 `codec_id = H264`
- 设 `format = AV_PIX_FMT_NV12`
- 设 `time_base = 1/90000`
- 打开 `avio`
- `avformat_write_header()`

并且显式设置了几个 RTSP 输出选项：

- `rtsp_transport = tcp`
- `muxdelay = 0`
- `pkt_size = 1200`

代码来源：`project2_master/third_party/rknn_yolov5_rk3568/src/mpp_encoder_rtsp.cc`
函数：`MppRtspEncoder::initRtspOutput()`
作用：把 MPP 产出的 H.264 码流挂到 FFmpeg 的 RTSP 输出上下文，并真正向目标 URL 发起写入。

```cpp
int MppRtspEncoder::initRtspOutput() {
    avformat_network_init();

    int ret = avformat_alloc_output_context2(&fmt_ctx_, nullptr, "rtsp", rtsp_url_.c_str());
    if (ret < 0 || !fmt_ctx_) {
        fprintf(stderr, "RTSP encoder: avformat_alloc_output_context2 failed ret=%d\n", ret);
        return -1;
    }

    video_stream_ = avformat_new_stream(fmt_ctx_, nullptr);
    if (!video_stream_) {
        fprintf(stderr, "RTSP encoder: avformat_new_stream failed\n");
        return -1;
    }

    video_stream_->time_base = AVRational{1, 90000};
    video_stream_->avg_frame_rate = AVRational{fps_num_, fps_den_};
    video_stream_->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    video_stream_->codecpar->codec_id = AV_CODEC_ID_H264;
    video_stream_->codecpar->width = width_;
    video_stream_->codecpar->height = height_;
    video_stream_->codecpar->format = AV_PIX_FMT_NV12;
    video_stream_->codecpar->bit_rate = bitrate_bps_;

    if (loadHeadersIntoStream() != 0) {
        return -1;
    }

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "muxdelay", "0", 0);
    av_dict_set(&opts, "pkt_size", "1200", 0);

    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open2(&fmt_ctx_->pb, rtsp_url_.c_str(), AVIO_FLAG_WRITE, nullptr, &opts);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errbuf, sizeof(errbuf));
            fprintf(stderr, "RTSP encoder: avio_open2 failed: %s\n", errbuf);
            av_dict_free(&opts);
            return -1;
        }
        io_opened_ = true;
    }

    fmt_ctx_->flags |= AVFMT_FLAG_FLUSH_PACKETS;
    ret = avformat_write_header(fmt_ctx_, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "RTSP encoder: avformat_write_header failed: %s\n", errbuf);
        return -1;
    }
    header_written_ = true;

    return 0;
}
```

这段代码说明当前实现不是“板端起一个 RTSP server”，而是 FFmpeg 作为 RTSP client 主动往 `rtsp_url_` 写。`avformat_write_header()` 成功之后，`header_written_` 才会置真，后面 `close()` 才知道是否应该调用 `av_write_trailer()` 收尾。

### 8.5 `encodeAndPush()`

这是帧级主逻辑，顺序如下：

1. 检查 `frame.data` 非空，格式必须是 NV12
2. 如果当前 encoder 几何信息和帧不匹配，调用 `reopenForFrame()`
3. 计算最小 payload 大小，防止短帧
4. 取出 `input_buffer_` 指针
5. 先把整块缓冲清零
6. 逐行复制 Y 平面
7. 再逐行复制 UV 平面
8. 构造 `MppFrame`
9. `mpp_frame_set_pts(frame.timestamp_us)`
10. `encode_put_frame()`
11. 循环 `encode_get_packet()`
12. 每个 packet 调 `writeMppPacket()`

这里有两个非常重要的事实。

第一，它不是全链路零拷贝。

虽然上游已经准备好了 NV12，但这里仍然会拷进 MPP 的内部输入缓冲。

第二，它是同步 drain packet 的。

也就是：

- 喂一帧
- 立刻尽量把这一帧对应的 packet 取干净

代码来源：`project2_master/third_party/rknn_yolov5_rk3568/src/mpp_encoder_rtsp.cc`
函数：`MppRtspEncoder::encodeAndPush()`
作用：把单个 `PostStreamFrame` 复制进 MPP 输入缓冲，喂给 H.264 编码器，再同步取包并写到 RTSP。

```cpp
int MppRtspEncoder::encodeAndPush(const PostStreamFrame& frame) {
    if (!frame.data || frame.format != RK_FORMAT_YCbCr_420_SP ||
        frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width) {
        return -1;
    }
    if (!input_buffer_ || !mpi_ || !mpp_ctx_ ||
        frame.width != width_ || frame.height != height_) {
        if (reopenForFrame(frame) != 0) {
            fprintf(stderr,
                    "RTSP encoder: failed to reopen for frame w=%d h=%d stride=%d\n",
                    frame.width, frame.height, frame.stride);
            return -1;
        }
    }

    const int src_stride = frame.stride > 0 ? frame.stride : frame.width;
    const size_t min_bytes = static_cast<size_t>(src_stride) * static_cast<size_t>(frame.height) * 3U / 2U;
    if (frame.size < min_bytes) {
        fprintf(stderr,
                "RTSP encoder: short frame payload size=%zu required=%zu\n",
                frame.size, min_bytes);
        return -1;
    }

    unsigned char* dst = static_cast<unsigned char*>(mpp_buffer_get_ptr(input_buffer_));
    if (!dst) {
        fprintf(stderr, "RTSP encoder: null MPP input buffer\n");
        return -1;
    }

    const size_t dst_bytes =
        static_cast<size_t>(hor_stride_) * static_cast<size_t>(ver_stride_) * 3U / 2U;
    memset(dst, 0, dst_bytes);

    const unsigned char* src = frame.data;
    for (int row = 0; row < height_; ++row) {
        memcpy(dst + row * hor_stride_, src + row * src_stride, width_);
    }

    const unsigned char* src_uv = src + static_cast<size_t>(src_stride) * static_cast<size_t>(height_);
    unsigned char* dst_uv = dst + static_cast<size_t>(hor_stride_) * static_cast<size_t>(ver_stride_);
    for (int row = 0; row < height_ / 2; ++row) {
        memcpy(dst_uv + row * hor_stride_, src_uv + row * src_stride, width_);
    }

    MppFrame mpp_frame = nullptr;
    MPP_RET ret = mpp_frame_init(&mpp_frame);
    if (ret != MPP_OK || !mpp_frame) {
        fprintf(stderr, "RTSP encoder: mpp_frame_init failed ret=%d\n", ret);
        return -1;
    }

    mpp_frame_set_width(mpp_frame, width_);
    mpp_frame_set_height(mpp_frame, height_);
    mpp_frame_set_hor_stride(mpp_frame, hor_stride_);
    mpp_frame_set_ver_stride(mpp_frame, ver_stride_);
    mpp_frame_set_fmt(mpp_frame, MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(mpp_frame, input_buffer_);
    mpp_frame_set_pts(mpp_frame, frame.timestamp_us);

    ret = mpi_->encode_put_frame(mpp_ctx_, mpp_frame);
    mpp_frame_deinit(&mpp_frame);
    if (ret != MPP_OK) {
        fprintf(stderr, "RTSP encoder: encode_put_frame failed ret=%d\n", ret);
        return -1;
    }

    int write_ret = 0;
    bool saw_packet = false;
    bool need_more_packets = true;
    while (need_more_packets) {
        MppPacket packet = nullptr;
        ret = mpi_->encode_get_packet(mpp_ctx_, &packet);
        if (ret != MPP_OK) {
            fprintf(stderr, "RTSP encoder: encode_get_packet failed ret=%d\n", ret);
            return -1;
        }

        if (!packet) {
            if (saw_packet) {
                fprintf(stderr,
                        "RTSP encoder: encode_get_packet returned null before frame drain completed\n");
                return -1;
            }
            break;
        }

        saw_packet = true;
        const bool frame_done =
            !mpp_packet_is_partition(packet) || mpp_packet_is_eoi(packet);
        write_ret = writeMppPacket(packet, first_capture_ts_us_ < 0);
        mpp_packet_deinit(&packet);
        if (write_ret != 0) {
            return write_ret;
        }

        need_more_packets = !frame_done;
    }

    return write_ret;
}
```

这段可以直接证明两件事。第一，上游虽然已经给了 NV12，但这里仍然会逐行拷贝进 `input_buffer_`，所以当前实现不是端到端零拷贝。第二，`mpp_frame_set_pts(mpp_frame, frame.timestamp_us)` 把 `PostStreamFrame.timestamp_us` 继续往下传了，后面的 `writeMppPacket()` 才能基于真实采样时间换算 RTSP 时间轴。

### 8.6 `writeMppPacket()`

这一步把 `MppPacket` 转成 `AVPacket` 并送给 FFmpeg。

当前时间戳逻辑是：

- 第一帧 `capture_ts_us` 记到 `first_capture_ts_us_`
- 后续所有 `pts` 用“相对第一帧的时间差”换算
- `dts = pts`
- `duration` 优先取相邻两帧真实时间差
- 如果真实差值不可用，再退回按 FPS 估算

这保证了 RTSP 输出的时间轴不是简单的“每帧 +1”，而是尽量贴近真实采样时间。

## 9. RTSP 状态 publish 是怎么和推流链挂在一起的

当前 RTSP 状态通过 `publishStreamStatus()` 发到：

- `argi/device/rk3568-001/stream/status`

payload 里关注的是：

- `state`
- `url`
- `reason`

和 RTSP 支路相关的 reason 目前包括：

- `disabled`
- `open_failed`
- `push_failed`
- `post_stream_failed`
- `stopped`

代码来源：`project2_master/qt_gui/vision/vision_runtime.cpp`
函数：`publishStreamStatus()`
作用：把 RTSP 分支状态统一封装成 MQTT retained 消息，让订阅端总能看到最近一次状态。

```cpp
void publishStreamStatus(
    DashboardBackend *backend,
    VisionMqttPublisher *mqtt,
    const QString &state,
    const QString &streamUrl,
    const QString &reason = QString()
) {
    QJsonObject payload{
        {QStringLiteral("device_id"), QString::fromLatin1(kDeviceId)},
        {QStringLiteral("type"), QStringLiteral("stream_status")},
        {QStringLiteral("state"), state},
        {QStringLiteral("protocol"), QStringLiteral("rtsp")},
        {QStringLiteral("codec"), QStringLiteral("h264")},
        {QStringLiteral("url"), streamUrl}
    };

    if (!reason.isEmpty()) {
        payload.insert(QStringLiteral("reason"), reason);
    }
    logPublishedMessage(backend, mqtt, QString::fromLatin1(kTopicStreamStatus), payload, true);
}
```

这段函数本身很短，但它把文档里最该讲清的协议事实钉死了：主题固定是 `stream/status`，`type` 固定是 `stream_status`，`protocol` 和 `codec` 也都是硬编码的 `rtsp` / `h264`，并且最后一个参数明确传了 `retain = true`。所以订阅者看到的不是“瞬时事件流”，而是最近一次 RTSP 状态快照。

这类状态消息用 `retain = true`，很适合外部监控系统做“最后状态”判断。

## 10. 哪些错误会只杀 RTSP，哪些会杀整条视觉链

### 10.1 只影响 RTSP 支路

- `copyPostInferFrameToPostStreamPool()` 返回致命错误
- `MppRtspEncoder::open()` 失败
- `MppRtspEncoder::encodeAndPush()` 失败
- 用户显式 `--disable-vision-rtsp`

表现：

- detection 仍可能继续发
- `VisionSnapshot` 仍继续刷新
- 页面仍有画面

### 10.2 会导致整个 VisionRuntime 结束

- 输入源打不开
- 模型没加载成功
- `aiPool` 初始化失败
- sourceThread 采集或回队失败
- 主循环 RGB 转换链路出现致命配置错误

这时 RTSP 自然也会跟着结束。

## 11. 当前没有实现的 RTSP 周边能力

这部分必须写保守。

当前没有实现：

- RTSP 拉流客户端
- 本地内置 RTSP server
- recorder
- 事件前录后录
- `record_done`
- 通过 MQTT 命令启停推流

所以不能写成：

- “当前代码支持事件录像推送”
- “当前代码收到 MQTT 命令后可开启录像并上报完成”

这些都是错误表述。

## 12. 实际排障时该看什么

### 12.1 看配置

先确认：

- 是否传了 `--disable-vision-rtsp`
- `--vision-rtsp-url` 是否指向可写入的 RTSP 服务端

### 12.2 看本地日志

重点关注 `VISION` 类日志：

- `RTSP push disabled by option`
- `RTSP encoder open failed; stream branch will retry`
- `RTSP push online`
- `RTSP push failed; stream branch will retry`
- `RTSP post-stream frame build failed; disabling RTSP branch while keeping inference and display active`

### 12.3 看 MQTT 状态消息

如果 broker 正常，订阅：

- `argi/device/rk3568-001/stream/status`

至少应该能看到：

- `offline/disabled`
- 或 `online`
- 或失败原因

### 12.4 看输入几何是否变化

本地视频文件支路里，如果第一帧之后分辨率变化，会直接触发：

- `Video input geometry changed from ...`

这不是 RTSP 自己的问题，但它会让整个视觉链停止。

## 13. 一段最短的心智模型

理解当前 RTSP 支路最短的话是：

`sourceThread` 只负责把源帧送去推理；真正用于推流的帧是在 `aiPool.get()` 之后由 `copyPostInferFrameToPostStreamPool()` 生成的 annotated NV12，再经 `postStreamFramePool` 异步交给 `streamThread`，最后由 `MppRtspEncoder` 用 MPP 编成 H.264，通过 FFmpeg 作为 RTSP client 推到目标 URL；这条链已经实现，但 recorder、record_done 和 MQTT 命令控制都还没做。
