#include "vision_runtime.h"

#include "dashboard_backend.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPen>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <utility>

#ifdef DASHBOARD_HAVE_LOCAL_VISION_RUNTIME
#include "frame_pools.h"
#include "mpp_decoder.h"
#include "mpp_encoder_rtsp.h"
#include "preprocess.h"
#include "rkYolov5s.hpp"
#include "rknnPool.hpp"
#include "v4l2_capture.h"

#include <linux/videodev2.h>
#include <mosquitto.h>
#include <rga.h>
#include <sys/time.h>
#endif

namespace dashboard {

namespace {

constexpr int kCaptureWidth = 640;
constexpr int kCaptureHeight = 480;
constexpr int kCaptureBuffers = 4;
constexpr int kStreamWidth = 640;
constexpr int kStreamHeight = 540;

VisionSnapshot makeStatusSnapshot(const QString &message, bool cameraOnline, bool modelLoaded) {
    VisionSnapshot snapshot;
    snapshot.cameraOnline = cameraOnline;
    snapshot.modelLoaded = modelLoaded;
    snapshot.errorMsg = message;
    return snapshot;
}

void sleepShort() {
    QThread::msleep(800);
}

#ifdef DASHBOARD_HAVE_LOCAL_VISION_RUNTIME

constexpr int kAiWorkerThreads = 1;
constexpr int kAiQueueSize = 4;
constexpr int kStreamQueueSize = 4;
constexpr int kPoolPreallocCount = 6;
constexpr int kPoolCachedCount = 8;
constexpr int kDefaultFpsNum = 30;
constexpr int kDefaultFpsDen = 1;
constexpr int kDefaultRtspBitrateBps = 0;
constexpr char kDeviceId[] = "rk3568-001";
constexpr char kMqttHost[] = "192.168.30.26";
constexpr int kMqttPort = 1883;
constexpr char kRtspPushUrl[] = "rtsp://192.168.30.26:8554/rk3568-001/cam0";
constexpr char kTopicVisionDetection[] = "argi/device/rk3568-001/vision/detection";
constexpr char kTopicStreamStatus[] = "argi/device/rk3568-001/stream/status";

bool tryMultiplySize(size_t lhs, size_t rhs, size_t *result) {
    if (result == nullptr) {
        return false;
    }
    if (lhs != 0U && rhs > (std::numeric_limits<size_t>::max() / lhs)) {
        return false;
    }
    *result = lhs * rhs;
    return true;
}

size_t computeNv12Bytes(int stride, int verStride) {
    if (stride <= 0 || verStride <= 0) {
        return 0U;
    }

    size_t lumaBytes = 0U;
    if (!tryMultiplySize(static_cast<size_t>(stride), static_cast<size_t>(verStride), &lumaBytes)) {
        return 0U;
    }
    if (lumaBytes > (std::numeric_limits<size_t>::max() - (lumaBytes / 2U))) {
        return 0U;
    }
    return lumaBytes + (lumaBytes / 2U);
}

size_t computeRgbBytes(int width, int height) {
    if (width <= 0 || height <= 0) {
        return 0U;
    }

    size_t pixels = 0U;
    if (!tryMultiplySize(static_cast<size_t>(width), static_cast<size_t>(height), &pixels)) {
        return 0U;
    }
    if (pixels > (std::numeric_limits<size_t>::max() / 3U)) {
        return 0U;
    }
    return pixels * 3U;
}

QString localVisionRoot() {
#ifdef DASHBOARD_LOCAL_VISION_ROOT
    return QStringLiteral(DASHBOARD_LOCAL_VISION_ROOT);
#else
    return QString();
#endif
}

QString resolveModelPath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/model/yolov5s_relu-640-640-rk3568.rknn"),
        appDir + QStringLiteral("/model/yolov5s-640-640.rknn"),
        localVisionRoot() + QStringLiteral("/model/yolov5s_relu-640-640-rk3568.rknn"),
        localVisionRoot() + QStringLiteral("/model/yolov5s-640-640.rknn")
    };

    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

int toRgaFormat(int pixelFormat) {
    switch (pixelFormat) {
    case V4L2_PIX_FMT_YUYV:
        return RK_FORMAT_YUYV_422;
    case V4L2_PIX_FMT_NV12:
        return RK_FORMAT_YCbCr_420_SP;
    case V4L2_PIX_FMT_BGR24:
        return RK_FORMAT_BGR_888;
    default:
        return -1;
    }
}

long long nowWallTimeUs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<long long>(tv.tv_sec) * 1000000LL + static_cast<long long>(tv.tv_usec);
}

int alignUp(int value, int align) {
    if (align <= 0 || value <= 0) {
        return value;
    }
    if (value > (std::numeric_limits<int>::max() - (align - 1))) {
        return 0;
    }
    return ((value + align - 1) / align) * align;
}

size_t computeFrameSizeBytes(int width, int height, int format) {
    if (width <= 0 || height <= 0) {
        return 0U;
    }

    size_t wh = 0U;
    if (!tryMultiplySize(static_cast<size_t>(width), static_cast<size_t>(height), &wh)) {
        return 0U;
    }
    switch (format) {
    case RK_FORMAT_YUYV_422:
        if (wh > (std::numeric_limits<size_t>::max() / 2U)) {
            return 0U;
        }
        return wh * 2U;
    case RK_FORMAT_YCbCr_420_SP:
        if (wh > (std::numeric_limits<size_t>::max() - (wh / 2U))) {
            return 0U;
        }
        return wh + (wh / 2U);
    case RK_FORMAT_BGR_888:
    case RK_FORMAT_RGB_888:
        if (wh > (std::numeric_limits<size_t>::max() / 3U)) {
            return 0U;
        }
        return wh * 3U;
    default:
        if (wh > (std::numeric_limits<size_t>::max() / 3U)) {
            return 0U;
        }
        return wh * 3U;
    }
}

bool isVideoFileInput(const QString &path) {
    return !isAllowedVisionDevicePath(path);
}

QString describeVisionInputError(const QString &path) {
    if (path.isEmpty()) {
        return QStringLiteral("Vision input path is empty");
    }
    if (isAllowedVisionDevicePath(path)) {
        return QString();
    }

    const QFileInfo info(path);
    if (!info.exists()) {
        return QStringLiteral("Vision video file does not exist: %1").arg(path);
    }
    if (!info.isFile()) {
        return QStringLiteral("Vision input is not a regular file: %1").arg(path);
    }
    if (!info.isReadable()) {
        return QStringLiteral("Vision video file is not readable: %1").arg(path);
    }
    return QString();
}

QString describeGeometryChange(
    int expectedWidth,
    int expectedHeight,
    int currentWidth,
    int currentHeight
) {
    return QStringLiteral(
               "Video input geometry changed from %1x%2 to %3x%4; this build stops instead of risking buffer or encoder mismatches"
           )
        .arg(expectedWidth)
        .arg(expectedHeight)
        .arg(currentWidth)
        .arg(currentHeight);
}

class VisionMqttPublisher {
public:
    VisionMqttPublisher() = default;
    ~VisionMqttPublisher() {
        close();
    }

    bool open(const char *clientId, const char *host, int port) {
        int rc = MOSQ_ERR_SUCCESS;

        close();
        rc = mosquitto_lib_init();
        if (rc != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "[VISION_MQTT] mosquitto_lib_init failed: %s\n", mosquitto_strerror(rc));
            return false;
        }
        libInitialized_ = true;

        mosq_ = mosquitto_new(clientId, true, this);
        if (mosq_ == nullptr) {
            fprintf(stderr, "[VISION_MQTT] mosquitto_new failed\n");
            close();
            return false;
        }

        mosquitto_connect_callback_set(mosq_, &VisionMqttPublisher::handleConnect);
        mosquitto_disconnect_callback_set(mosq_, &VisionMqttPublisher::handleDisconnect);
        mosquitto_reconnect_delay_set(mosq_, 1u, 5u, true);

        rc = mosquitto_connect_async(mosq_, host, port, 30);
        if (rc != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "[VISION_MQTT] connect_async failed: %s\n", mosquitto_strerror(rc));
            close();
            return false;
        }

        rc = mosquitto_loop_start(mosq_);
        if (rc != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "[VISION_MQTT] loop_start failed: %s\n", mosquitto_strerror(rc));
            close();
            return false;
        }

        loopStarted_ = true;
        return true;
    }

    void close() {
        if (mosq_ != nullptr) {
            if (loopStarted_) {
                (void)mosquitto_disconnect(mosq_);
                (void)mosquitto_loop_stop(mosq_, true);
            }
            mosquitto_destroy(mosq_);
            mosq_ = nullptr;
        }

        if (libInitialized_) {
            mosquitto_lib_cleanup();
            libInitialized_ = false;
        }

        loopStarted_ = false;
        connected_.store(false, std::memory_order_relaxed);
    }

    bool isConnected() const {
        return connected_.load(std::memory_order_relaxed);
    }

    bool publish(const char *topic, const QByteArray &payload, bool retain = false) {
        int rc = MOSQ_ERR_NO_CONN;

        if (mosq_ == nullptr || !isConnected()) {
            return false;
        }
        rc = mosquitto_publish(
            mosq_,
            nullptr,
            topic,
            payload.size(),
            payload.constData(),
            0,
            retain
        );
        if (rc != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "[VISION_MQTT] publish %s failed: %s\n", topic, mosquitto_strerror(rc));
            return false;
        }
        return true;
    }

private:
    static void handleConnect(struct mosquitto *mosq, void *userdata, int rc) {
        VisionMqttPublisher *publisher = static_cast<VisionMqttPublisher *>(userdata);
        (void)mosq;
        if (publisher == nullptr) {
            return;
        }
        publisher->connected_.store(rc == 0, std::memory_order_relaxed);
        if (rc != 0) {
            fprintf(stderr, "[VISION_MQTT] connect failed: %s\n", mosquitto_connack_string(rc));
        }
    }

    static void handleDisconnect(struct mosquitto *mosq, void *userdata, int rc) {
        VisionMqttPublisher *publisher = static_cast<VisionMqttPublisher *>(userdata);
        (void)mosq;
        if (publisher == nullptr) {
            return;
        }
        publisher->connected_.store(false, std::memory_order_relaxed);
        if (rc != 0) {
            fprintf(stderr, "[VISION_MQTT] disconnected unexpectedly: %s\n", mosquitto_strerror(rc));
        }
    }

    struct mosquitto *mosq_ = nullptr;
    bool loopStarted_ = false;
    bool libInitialized_ = false;
    std::atomic<bool> connected_{false};
};

QByteArray compactJson(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

void logPublishedMessage(DashboardBackend *backend, VisionMqttPublisher *mqtt, const QString &topic, const QJsonObject &payload, bool retain = false) {
    const QByteArray encoded = compactJson(payload);
    const bool published = mqtt != nullptr && mqtt->publish(topic.toUtf8().constData(), encoded, retain);

    if (backend != nullptr && published) {
        backend->addMqttPublishLog(topic, QString::fromUtf8(encoded));
    }
}

void publishStreamStatus(
    DashboardBackend *backend,
    VisionMqttPublisher *mqtt,
    const QString &state,
    const QString &reason = QString()
) {
    QJsonObject payload{
        {QStringLiteral("device_id"), QString::fromLatin1(kDeviceId)},
        {QStringLiteral("type"), QStringLiteral("stream_status")},
        {QStringLiteral("state"), state},
        {QStringLiteral("protocol"), QStringLiteral("rtsp")},
        {QStringLiteral("codec"), QStringLiteral("h264")},
        {QStringLiteral("url"), QString::fromLatin1(kRtspPushUrl)}
    };

    if (!reason.isEmpty()) {
        payload.insert(QStringLiteral("reason"), reason);
    }
    logPublishedMessage(backend, mqtt, QString::fromLatin1(kTopicStreamStatus), payload, true);
}

void publishDetection(
    DashboardBackend *backend,
    VisionMqttPublisher *mqtt,
    uint64_t frameId,
    long long captureTsUs,
    const detect_result_group_t &group
) {
    QJsonArray objects;
    for (int i = 0; i < group.count; ++i) {
        const detect_result_t &det = group.results[i];
        QJsonArray box;
        box.append(det.box.left);
        box.append(det.box.top);
        box.append(det.box.right);
        box.append(det.box.bottom);

        QJsonObject object{
            {QStringLiteral("class"), QString::fromLocal8Bit(det.name)},
            {QStringLiteral("conf"), det.prop},
            {QStringLiteral("box"), box}
        };
        objects.append(object);
    }

    QJsonObject payload{
        {QStringLiteral("device_id"), QString::fromLatin1(kDeviceId)},
        {QStringLiteral("type"), QStringLiteral("vision_detection")},
        {QStringLiteral("frame_id"), static_cast<qint64>(frameId)},
        {QStringLiteral("ts_us"), static_cast<qint64>(captureTsUs)},
        {QStringLiteral("objects"), objects},
        {QStringLiteral("stream_url"), QString::fromLatin1(kRtspPushUrl)}
    };

    logPublishedMessage(backend, mqtt, QString::fromLatin1(kTopicVisionDetection), payload, false);
}

int copyFrameToAiPool(
    const void *srcData,
    size_t srcSize,
    int width,
    int height,
    int format,
    uint64_t frameId,
    long long captureTsUs,
    rknnPool<rkYolov5s> *pool,
    FrameCopyPool *framePool
) {
    const size_t expectedSize = computeFrameSizeBytes(width, height, format);
    PutRejectReason rejectReason = PutRejectReason::kNone;
    unsigned char *ownedBuffer = nullptr;

    if (srcData == nullptr || pool == nullptr || framePool == nullptr || expectedSize == 0U || srcSize < expectedSize) {
        return -1;
    }
    if (expectedSize > framePool->bufferSize()) {
        return -1;
    }

    ownedBuffer = framePool->acquire();
    if (ownedBuffer == nullptr) {
        return 1;
    }

    memcpy(ownedBuffer, srcData, expectedSize);
    if (pool->put(
            ownedBuffer,
            width,
            height,
            format,
            expectedSize,
            frameId,
            captureTsUs,
            release_pooled_buffer,
            framePool,
            &rejectReason) != 0) {
        if (rejectReason == PutRejectReason::kInvalidReleaseContract) {
            release_frame_buffer(ownedBuffer, release_pooled_buffer, framePool);
            return -1;
        }
        return 1;
    }

    return 0;
}

int copyFrameToStreamPool(
    const void *srcData,
    size_t srcSize,
    int width,
    int height,
    int srcStride,
    int srcVerStride,
    int srcFormat,
    uint64_t frameId,
    long long captureTsUs,
    int streamWidth,
    int streamHeight,
    int streamStride,
    FrameCopyPool *streamBufferPool,
    StreamFramePool *streamFramePool
) {
    const size_t requiredSize = computeFrameSizeBytes(width, height, srcFormat);
    const size_t streamBytes = computeNv12Bytes(streamStride, streamHeight);
    unsigned char *streamBuffer = nullptr;
    StreamFrame frame;

    if (
        srcData == nullptr ||
        streamBufferPool == nullptr ||
        streamFramePool == nullptr ||
        width <= 0 ||
        height <= 0 ||
        streamWidth <= 0 ||
        streamHeight <= 0 ||
        srcStride < width ||
        srcVerStride < height ||
        streamStride < streamWidth ||
        requiredSize == 0U ||
        srcSize < requiredSize ||
        streamBytes == 0U ||
        streamBytes > streamBufferPool->bufferSize()
    ) {
        return -1;
    }

    streamBuffer = streamBufferPool->acquire();
    if (streamBuffer == nullptr) {
        return 1;
    }

    if (rga_resize_to_nv12_vaddr(
            const_cast<void *>(srcData),
            width,
            height,
            srcStride,
            srcVerStride,
            srcFormat,
            streamBuffer,
            streamWidth,
            streamHeight,
            streamStride,
            streamHeight) != 0) {
        release_frame_buffer(streamBuffer, release_pooled_buffer, streamBufferPool);
        return -1;
    }

    frame.data = streamBuffer;
    frame.data_size = streamBytes;
    frame.frame_id = frameId;
    frame.capture_ts_us = captureTsUs;
    frame.width = streamWidth;
    frame.height = streamHeight;
    frame.stride = streamStride;
    frame.format = RK_FORMAT_YCbCr_420_SP;
    frame.release_fn = release_pooled_buffer;
    frame.release_ctx = streamBufferPool;
    streamFramePool->enqueue(std::move(frame));
    return 0;
}

QColor colorForLabel(const QString &label) {
    static const QVector<QColor> palette = {
        QColor(255, 99, 71),
        QColor(50, 205, 50),
        QColor(30, 144, 255),
        QColor(255, 215, 0),
        QColor(218, 112, 214),
        QColor(0, 206, 209)
    };
    uint hash = 0;
    for (const QChar ch : label) {
        hash = (hash * 131U) + static_cast<uint>(ch.unicode());
    }
    return palette.at(static_cast<int>(hash % static_cast<uint>(palette.size())));
}

QStringList formatDetections(const detect_result_group_t &group) {
    QStringList detections;
    for (int i = 0; i < group.count; ++i) {
        const detect_result_t &det = group.results[i];
        detections.append(
            QStringLiteral("%1 %.1f%% [%2,%3]-[%4,%5]")
                .arg(QString::fromLocal8Bit(det.name))
                .arg(det.prop * 100.0, 0, 'f', 1)
                .arg(det.box.left)
                .arg(det.box.top)
                .arg(det.box.right)
                .arg(det.box.bottom)
        );
    }
    return detections;
}

QImage renderAnnotatedFrame(const QImage &baseFrame, const detect_result_group_t &group) {
    QImage frame = baseFrame.copy();
    QPainter painter(&frame);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QFont labelFont(QStringLiteral("Microsoft YaHei"), 11, QFont::Bold);
    painter.setFont(labelFont);

    for (int i = 0; i < group.count; ++i) {
        const detect_result_t &det = group.results[i];
        const QString name = QString::fromLocal8Bit(det.name);
        const QColor color = colorForLabel(name);
        const QRect box(
            det.box.left,
            det.box.top,
            qMax(1, det.box.right - det.box.left),
            qMax(1, det.box.bottom - det.box.top)
        );

        painter.setPen(QPen(color, 2));
        painter.drawRect(box);

        const QString label = QStringLiteral("%1 %.1f%%").arg(name).arg(det.prop * 100.0, 0, 'f', 1);
        const QRect labelRect = painter.fontMetrics().boundingRect(label).adjusted(-6, -3, 6, 3);
        QPoint origin(box.left(), qMax(labelRect.height(), box.top()));
        QRect drawRect(origin.x(), origin.y() - labelRect.height(), labelRect.width(), labelRect.height());
        painter.fillRect(drawRect, QColor(0, 0, 0, 160));
        painter.setPen(Qt::white);
        painter.drawText(drawRect.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
    }

    return frame;
}

#endif

}  // namespace

VisionRuntime::VisionRuntime(DashboardBackend *backend, const AppOptions &options)
    : backend_(backend),
      options_(options) {
}

VisionRuntime::~VisionRuntime() {
    stop();
}

void VisionRuntime::start() {
    if (backend_ == nullptr || running_.exchange(true)) {
        return;
    }
    worker_ = std::thread(&VisionRuntime::workerLoop, this);
}

void VisionRuntime::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void VisionRuntime::workerLoop() {
    if (backend_ == nullptr) {
        running_.store(false);
        return;
    }

#ifdef DASHBOARD_HAVE_LOCAL_VISION_RUNTIME
    const QString modelPath = resolveModelPath();
    const QString inputPath = options_.visionDevice.trimmed();
    const bool useV4L2 = isAllowedVisionDevicePath(inputPath);
    VisionMqttPublisher mqtt;
    QString terminalMessage;
    bool modelReady = false;
    bool inputReady = false;
    int srcWidth = 0;
    int srcHeight = 0;
    int srcFormat = RK_FORMAT_YCbCr_420_SP;
    int srcStride = 0;
    int srcVerStride = 0;
    int fpsNum = kDefaultFpsNum;
    int fpsDen = kDefaultFpsDen;
    bool prefetchedDecodedFrame = false;
    unsigned char *decodedFrameData = nullptr;
    size_t decodedFrameSize = 0U;

    backend_->addLog("INFO", "VISION", QStringLiteral("Starting local board-side VisionRuntime"));

    if (modelPath.isEmpty()) {
        backend_->updateVisionState(
            makeStatusSnapshot(
                QStringLiteral("RKNN model file was not found under the app model/ directory or project2_master third_party assets"),
                false,
                false
            )
        );
        backend_->addLog(
            "ERROR",
            "VISION",
            QStringLiteral("VisionRuntime could not find a usable RKNN model inside project2_master assets")
        );
        running_.store(false);
        return;
    }
    modelReady = true;

    rknnPool<rkYolov5s> aiPool(modelPath.toStdString(), kAiWorkerThreads, kAiQueueSize);
    if (aiPool.init() != 0) {
        backend_->updateVisionState(makeStatusSnapshot(QStringLiteral("RKNN model init failed"), false, false));
        backend_->addLog("ERROR", "VISION", QStringLiteral("VisionRuntime model init failed: %1").arg(modelPath));
        running_.store(false);
        return;
    }

    backend_->addLog("INFO", "VISION", QStringLiteral("VisionRuntime model loaded: %1").arg(modelPath));
    if (!mqtt.open("rk3568-001-vision-runtime", kMqttHost, kMqttPort)) {
        backend_->addLog("WARN", "VISION", QStringLiteral("Vision MQTT publisher is disabled; broker connection was not established"));
    }

    V4L2Capture capture(inputPath.toStdString(), kCaptureWidth, kCaptureHeight, kCaptureBuffers);
    MppDecoder decoder;
    bool captureOpened = false;
    bool captureStreaming = false;
    bool decoderOpened = false;

    if (useV4L2) {
        if (capture.open() != 0) {
            terminalMessage = QStringLiteral("Failed to open camera %1").arg(inputPath);
            backend_->updateVisionState(makeStatusSnapshot(terminalMessage, false, true));
            backend_->addLog("ERROR", "VISION", terminalMessage);
            running_.store(false);
            return;
        }
        captureOpened = true;

        if (capture.startStream() != 0) {
            terminalMessage = QStringLiteral("Failed to start camera stream %1").arg(inputPath);
            capture.close();
            backend_->updateVisionState(makeStatusSnapshot(terminalMessage, false, true));
            backend_->addLog("ERROR", "VISION", terminalMessage);
            running_.store(false);
            return;
        }
        captureStreaming = true;

        srcWidth = capture.getWidth();
        srcHeight = capture.getHeight();
        srcFormat = toRgaFormat(capture.getPixelFormat());
        srcStride = srcWidth;
        srcVerStride = srcHeight;
        fpsNum = capture.getFpsNum() > 0 ? capture.getFpsNum() : kDefaultFpsNum;
        fpsDen = capture.getFpsDen() > 0 ? capture.getFpsDen() : kDefaultFpsDen;
        inputReady = srcFormat >= 0;
        if (!inputReady) {
            terminalMessage = QStringLiteral("Unsupported camera pixel format: 0x%1")
                .arg(capture.getPixelFormat(), 8, 16, QLatin1Char('0'));
            backend_->updateVisionState(makeStatusSnapshot(terminalMessage, false, true));
            backend_->addLog("ERROR", "VISION", terminalMessage);
            capture.stopStream();
            capture.close();
            running_.store(false);
            return;
        }

        backend_->addLog(
            "INFO",
            "VISION",
            QStringLiteral("VisionRuntime connected camera %1 (%2x%3)")
                .arg(inputPath)
                .arg(srcWidth)
                .arg(srcHeight)
        );
    } else {
        const QString inputError = describeVisionInputError(inputPath);
        if (!inputError.isEmpty()) {
            backend_->updateVisionState(makeStatusSnapshot(inputError, false, true));
            backend_->addLog("ERROR", "VISION", inputError);
            running_.store(false);
            return;
        }
        if (decoder.open(inputPath.toStdString().c_str()) != 0) {
            terminalMessage = QStringLiteral("Failed to open video input %1").arg(inputPath);
            backend_->updateVisionState(makeStatusSnapshot(terminalMessage, false, true));
            backend_->addLog("ERROR", "VISION", terminalMessage);
            running_.store(false);
            return;
        }
        decoderOpened = true;
        srcFormat = RK_FORMAT_YCbCr_420_SP;
        fpsNum = decoder.getFpsNum() > 0 ? decoder.getFpsNum() : kDefaultFpsNum;
        fpsDen = decoder.getFpsDen() > 0 ? decoder.getFpsDen() : kDefaultFpsDen;
        if (decoder.readFrame(&decodedFrameData, &srcWidth, &srcHeight) != 0 || decodedFrameData == nullptr) {
            terminalMessage = QStringLiteral("Failed to decode the first frame from %1").arg(inputPath);
            backend_->updateVisionState(makeStatusSnapshot(terminalMessage, false, true));
            backend_->addLog("ERROR", "VISION", terminalMessage);
            decoder.close();
            running_.store(false);
            return;
        }
        srcStride = srcWidth;
        srcVerStride = srcHeight;
        decodedFrameSize = computeFrameSizeBytes(srcWidth, srcHeight, srcFormat);
        inputReady = srcWidth > 0 && srcHeight > 0 && decodedFrameSize > 0U;
        if (!inputReady) {
            terminalMessage = QStringLiteral("Decoded video dimensions are not available for %1").arg(inputPath);
            backend_->updateVisionState(makeStatusSnapshot(terminalMessage, false, true));
            backend_->addLog("ERROR", "VISION", terminalMessage);
            decoder.close();
            running_.store(false);
            return;
        }
        prefetchedDecodedFrame = true;

        backend_->addLog(
            "INFO",
            "VISION",
            QStringLiteral("VisionRuntime opened video input %1 (%2x%3)")
                .arg(inputPath)
                .arg(srcWidth)
                .arg(srcHeight)
        );
    }

    const size_t aiFrameBytes = computeFrameSizeBytes(srcWidth, srcHeight, srcFormat);
    const int streamWidth = kStreamWidth;
    const int streamHeight = kStreamHeight;
    const int streamStride = alignUp(streamWidth, 16);
    const size_t streamFrameBytes = computeNv12Bytes(streamStride, streamHeight);
    const size_t rgbBufferBytes = computeRgbBytes(srcWidth, srcHeight);
    if (aiFrameBytes == 0U || streamWidth <= 0 || streamHeight <= 0 || streamStride <= 0 ||
        streamFrameBytes == 0U || rgbBufferBytes == 0U ||
        rgbBufferBytes > static_cast<size_t>(std::numeric_limits<int>::max())) {
        terminalMessage = QStringLiteral("Unsupported vision frame geometry %1x%2").arg(srcWidth).arg(srcHeight);
        backend_->updateVisionState(makeStatusSnapshot(terminalMessage, false, modelReady));
        backend_->addLog("ERROR", "VISION", terminalMessage);
        if (captureStreaming) {
            capture.stopStream();
        }
        if (captureOpened) {
            capture.close();
        }
        if (decoderOpened) {
            decoder.close();
        }
        running_.store(false);
        return;
    }
    FrameCopyPool aiFramePool(aiFrameBytes, kPoolPreallocCount, kPoolCachedCount);
    FrameCopyPool streamBufferPool(streamFrameBytes, kPoolPreallocCount, kPoolCachedCount);
    StreamFramePool streamFramePool(kStreamQueueSize);
    QVector<unsigned char> rgbBuffer(static_cast<int>(rgbBufferBytes));
    std::atomic<uint64_t> frameIdGenerator{1U};
    QString sourceError;
    QString streamError;
    QString pipelineError;

    std::thread streamThread([&]() {
        MppRtspEncoder encoder;
        bool encoderOpen = false;

        while (true) {
            StreamFrame frame;
            if (!streamFramePool.waitAndPop(&frame)) {
                break;
            }

            if (!encoderOpen) {
                if (encoder.open(
                        kRtspPushUrl,
                        frame.width,
                        frame.height,
                        frame.stride,
                        frame.height,
                        fpsNum,
                        fpsDen,
                        kDefaultRtspBitrateBps) != 0) {
                    streamError = QStringLiteral("RTSP encoder open failed");
                    backend_->addLog("ERROR", "VISION", streamError);
                    publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), streamError);
                    release_frame_buffer(frame.data, frame.release_fn, frame.release_ctx);
                    running_.store(false);
                    streamFramePool.stop();
                    aiPool.notifyGetters();
                    break;
                }

                encoderOpen = true;
                backend_->addLog("INFO", "VISION", QStringLiteral("RTSP push online: %1").arg(QString::fromLatin1(kRtspPushUrl)));
                publishStreamStatus(backend_, &mqtt, QStringLiteral("online"));
            }

            if (encoder.encodeAndPush(frame) != 0) {
                streamError = QStringLiteral("RTSP push failed");
                backend_->addLog("ERROR", "VISION", streamError);
                publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), streamError);
                release_frame_buffer(frame.data, frame.release_fn, frame.release_ctx);
                running_.store(false);
                streamFramePool.stop();
                aiPool.notifyGetters();
                break;
            }

            release_frame_buffer(frame.data, frame.release_fn, frame.release_ctx);
        }

        if (encoderOpen && streamError.isEmpty()) {
            publishStreamStatus(backend_, &mqtt, QStringLiteral("offline"), QStringLiteral("stopped"));
        }
        encoder.close();
    });

    std::thread sourceThread([&]() {
        bool usePrefetchedDecodedFrame = prefetchedDecodedFrame;

        while (running_.load()) {
            const void *srcData = nullptr;
            size_t srcSize = 0U;
            int currentWidth = srcWidth;
            int currentHeight = srcHeight;
            int currentFormat = srcFormat;
            int currentStride = srcStride;
            int currentVerStride = srcVerStride;
            long long captureTsUs = nowWallTimeUs();
            V4L2Frame v4l2Frame;
            QString fatalCopyError;

            if (useV4L2) {
                if (capture.dequeueFrame(&v4l2Frame) != 0) {
                    sourceError = QStringLiteral("Camera frame capture failed");
                    backend_->addLog("ERROR", "VISION", sourceError);
                    running_.store(false);
                    break;
                }
                srcData = v4l2Frame.data;
                srcSize = static_cast<size_t>(v4l2Frame.size);
                if (v4l2Frame.capture_ts_us > 0) {
                    captureTsUs = v4l2Frame.capture_ts_us;
                }
            } else {
                if (usePrefetchedDecodedFrame) {
                    srcData = decodedFrameData;
                    srcSize = decodedFrameSize;
                    usePrefetchedDecodedFrame = false;
                } else {
                    unsigned char *decodedFrame = nullptr;
                    if (decoder.readFrame(&decodedFrame, &currentWidth, &currentHeight) != 0) {
                        break;
                    }
                    srcData = decodedFrame;
                }
                currentFormat = RK_FORMAT_YCbCr_420_SP;
                currentStride = currentWidth;
                currentVerStride = currentHeight;
                if (srcData == nullptr) {
                    sourceError = QStringLiteral("Decoder returned an empty frame");
                    backend_->addLog("ERROR", "VISION", sourceError);
                    running_.store(false);
                    break;
                }
                if (currentWidth != srcWidth || currentHeight != srcHeight) {
                    sourceError = describeGeometryChange(srcWidth, srcHeight, currentWidth, currentHeight);
                    backend_->addLog("ERROR", "VISION", sourceError);
                    running_.store(false);
                    break;
                }
                if (srcSize == 0U) {
                    srcSize = computeFrameSizeBytes(currentWidth, currentHeight, currentFormat);
                }
                if (srcSize == 0U) {
                    sourceError = QStringLiteral("Decoded frame geometry overflowed the configured buffers");
                    backend_->addLog("ERROR", "VISION", sourceError);
                    running_.store(false);
                    break;
                }
            }

            const uint64_t frameId = frameIdGenerator.fetch_add(1U, std::memory_order_relaxed);
            const int aiCopyRc = copyFrameToAiPool(
                srcData,
                srcSize,
                currentWidth,
                currentHeight,
                currentFormat,
                frameId,
                captureTsUs,
                &aiPool,
                &aiFramePool
            );
            if (aiCopyRc < 0) {
                fatalCopyError = QStringLiteral("AI frame fan-out failed for %1x%2 input")
                                     .arg(currentWidth)
                                     .arg(currentHeight);
            } else {
                const int streamCopyRc = copyFrameToStreamPool(
                    srcData,
                    srcSize,
                    currentWidth,
                    currentHeight,
                    currentStride,
                    currentVerStride,
                    currentFormat,
                    frameId,
                    captureTsUs,
                    streamWidth,
                    streamHeight,
                    streamStride,
                    &streamBufferPool,
                    &streamFramePool
                );
                if (streamCopyRc < 0) {
                    fatalCopyError = QStringLiteral("RTSP stream fan-out failed for %1x%2 input")
                                         .arg(currentWidth)
                                         .arg(currentHeight);
                }
            }

            if (useV4L2 && capture.queueFrame(v4l2Frame) != 0) {
                sourceError = QStringLiteral("Camera frame queue failed");
                backend_->addLog("ERROR", "VISION", sourceError);
                running_.store(false);
                break;
            }
            if (!fatalCopyError.isEmpty()) {
                sourceError = fatalCopyError;
                backend_->addLog("ERROR", "VISION", sourceError);
                running_.store(false);
                break;
            }
        }

        aiPool.notifyGetters();
        streamFramePool.stop();
    });

    int frameCount = 0;
    int fpsWindowFrames = 0;
    double fps = 0.0;
    auto fpsWindowStart = std::chrono::steady_clock::now();

    while (running_.load()) {
        detect_result_group_t detGroup;
        float scaleW = 0.0f;
        float scaleH = 0.0f;
        unsigned char *frameData = nullptr;
        int frameWidth = 0;
        int frameHeight = 0;
        int frameFormat = 0;
        uint64_t frameId = 0U;
        long long captureTsUs = 0LL;
        void (*releaseFn)(unsigned char *, void *) = nullptr;
        void *releaseCtx = nullptr;
        VisionSnapshot snapshot;

        memset(&detGroup, 0, sizeof(detGroup));
        if (aiPool.get(
                detGroup,
                scaleW,
                scaleH,
                &frameData,
                &frameWidth,
                &frameHeight,
                &frameFormat,
                &frameId,
                &captureTsUs,
                &releaseFn,
                &releaseCtx) != 0) {
            break;
        }

        snapshot.cameraOnline = inputReady;
        snapshot.modelLoaded = modelReady;
        snapshot.statusReported = true;
        snapshot.frameCount = ++frameCount;

        if (frameData == nullptr) {
            snapshot.errorMsg = QStringLiteral("AI pool returned an empty frame payload");
        } else if (computeRgbBytes(frameWidth, frameHeight) == 0U ||
                   computeRgbBytes(frameWidth, frameHeight) > static_cast<size_t>(rgbBuffer.size())) {
            snapshot.errorMsg = QStringLiteral("AI output geometry %1x%2 exceeds the configured RGB buffer")
                                    .arg(frameWidth)
                                    .arg(frameHeight);
            backend_->addLog("ERROR", "VISION", snapshot.errorMsg);
            pipelineError = snapshot.errorMsg;
            running_.store(false);
        } else if (rga_resize_convert_vaddr(
                       frameData,
                       frameWidth,
                       frameHeight,
                       frameFormat,
                       rgbBuffer.data(),
                       frameWidth,
                       frameHeight,
                       RK_FORMAT_RGB_888) != 0) {
            snapshot.errorMsg = QStringLiteral("RGA color conversion failed");
        } else {
            QImage rgbImage(
                rgbBuffer.constData(),
                frameWidth,
                frameHeight,
                frameWidth * 3,
                QImage::Format_RGB888
            );
            snapshot.detections = formatDetections(detGroup);
            snapshot.frame = renderAnnotatedFrame(rgbImage, detGroup);
            if (detGroup.count > 0) {
                publishDetection(backend_, &mqtt, frameId, captureTsUs, detGroup);
            }
        }

        if (frameData != nullptr) {
            release_frame_buffer(frameData, releaseFn, releaseCtx);
        }

        ++fpsWindowFrames;
        const auto now = std::chrono::steady_clock::now();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - fpsWindowStart).count();
        if (elapsedMs >= 1000) {
            fps = static_cast<double>(fpsWindowFrames) * 1000.0 / static_cast<double>(elapsedMs);
            fpsWindowFrames = 0;
            fpsWindowStart = now;
        }
        snapshot.fps = fps;
        backend_->updateVisionState(snapshot);
    }

    running_.store(false);
    aiPool.notifyGetters();
    streamFramePool.stop();
    if (sourceThread.joinable()) {
        sourceThread.join();
    }
    if (streamThread.joinable()) {
        streamThread.join();
    }

    if (captureStreaming) {
        capture.stopStream();
    }
    if (captureOpened) {
        capture.close();
    }
    if (decoderOpened) {
        decoder.close();
    }

    if (!sourceError.isEmpty()) {
        backend_->updateVisionState(makeStatusSnapshot(sourceError, false, modelReady));
    } else if (!streamError.isEmpty()) {
        backend_->updateVisionState(makeStatusSnapshot(streamError, inputReady, modelReady));
    } else if (!pipelineError.isEmpty()) {
        backend_->updateVisionState(makeStatusSnapshot(pipelineError, inputReady, modelReady));
    } else {
        backend_->updateVisionState(makeStatusSnapshot(QStringLiteral("Vision runtime stopped"), false, modelReady));
    }
    mqtt.close();
#else
    const QString configurationError = QStringLiteral(
        "VisionRuntime was built without DASHBOARD_HAVE_LOCAL_VISION_RUNTIME; this unsupported configuration no longer falls back to a fake-success runtime path"
    );
    backend_->updateVisionState(makeStatusSnapshot(configurationError, false, false));
    backend_->addLog("ERROR", "VISION", configurationError);
#endif

    running_.store(false);
}

}  // namespace dashboard
