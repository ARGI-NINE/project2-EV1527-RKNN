#include "vision_runtime.h"

#include "dashboard_backend.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <chrono>
#include <cstring>
#include <utility>

#ifdef DASHBOARD_HAVE_LOCAL_VISION_RUNTIME
#include "preprocess.h"
#include "rkYolov5s.hpp"
#include "v4l2_capture.h"

#include <linux/videodev2.h>
#include <rga.h>
#endif

namespace dashboard {

namespace {

constexpr int kCaptureWidth = 640;
constexpr int kCaptureHeight = 480;
constexpr int kCaptureBuffers = 4;

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
    backend_->addLog("INFO", "VISION", QStringLiteral("Starting local board-side VisionRuntime"));

    while (running_.load()) {
        const QString modelPath = resolveModelPath();
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
            sleepShort();
            continue;
        }

        rkYolov5s detector(modelPath.toStdString());
        if (detector.init() != 0) {
            backend_->updateVisionState(
                makeStatusSnapshot(QStringLiteral("RKNN model init failed"), false, false)
            );
            backend_->addLog(
                "ERROR",
                "VISION",
                QStringLiteral("VisionRuntime model init failed: %1").arg(modelPath)
            );
            sleepShort();
            continue;
        }

        backend_->addLog("INFO", "VISION", QStringLiteral("VisionRuntime model loaded: %1").arg(modelPath));

        V4L2Capture capture(options_.visionDevice.toStdString(), kCaptureWidth, kCaptureHeight, kCaptureBuffers);
        if (capture.open() != 0) {
            backend_->updateVisionState(
                makeStatusSnapshot(
                    QStringLiteral("Failed to open camera %1").arg(options_.visionDevice),
                    false,
                    true
                )
            );
            backend_->addLog(
                "ERROR",
                "VISION",
                QStringLiteral("VisionRuntime failed to open camera: %1").arg(options_.visionDevice)
            );
            sleepShort();
            continue;
        }

        if (capture.startStream() != 0) {
            capture.close();
            backend_->updateVisionState(
                makeStatusSnapshot(
                    QStringLiteral("Failed to start camera stream %1").arg(options_.visionDevice),
                    false,
                    true
                )
            );
            backend_->addLog(
                "ERROR",
                "VISION",
                QStringLiteral("VisionRuntime failed to start camera stream: %1").arg(options_.visionDevice)
            );
            sleepShort();
            continue;
        }

        backend_->addLog(
            "INFO",
            "VISION",
            QStringLiteral("VisionRuntime connected camera %1 (%2x%3)")
                .arg(options_.visionDevice)
                .arg(capture.getWidth())
                .arg(capture.getHeight())
        );

        const int srcFormat = toRgaFormat(capture.getPixelFormat());
        if (srcFormat < 0) {
            capture.stopStream();
            capture.close();
            backend_->updateVisionState(
                makeStatusSnapshot(
                    QStringLiteral("Unsupported camera pixel format: 0x%1")
                        .arg(capture.getPixelFormat(), 8, 16, QLatin1Char('0')),
                    false,
                    true
                )
            );
            backend_->addLog(
                "ERROR",
                "VISION",
                QStringLiteral("VisionRuntime unsupported pixel format: 0x%1")
                    .arg(capture.getPixelFormat(), 8, 16, QLatin1Char('0'))
            );
            sleepShort();
            continue;
        }

        QVector<unsigned char> rgbBuffer(capture.getWidth() * capture.getHeight() * 3);
        int frameCount = 0;
        int fpsWindowFrames = 0;
        double fps = 0.0;
        auto fpsWindowStart = std::chrono::steady_clock::now();

        while (running_.load()) {
            void *rawData = nullptr;
            int rawSize = 0;
            if (capture.captureFrame(&rawData, &rawSize) != 0) {
                backend_->updateVisionState(
                    makeStatusSnapshot(QStringLiteral("Camera frame capture failed"), false, true)
                );
                backend_->addLog("ERROR", "VISION", QStringLiteral("VisionRuntime captureFrame failed"));
                break;
            }

            detect_result_group_t detGroup;
            memset(&detGroup, 0, sizeof(detGroup));
            float scaleW = 0.0f;
            float scaleH = 0.0f;

            VisionSnapshot snapshot;
            snapshot.cameraOnline = true;
            snapshot.modelLoaded = true;
            snapshot.frameCount = ++frameCount;

            if (rga_resize_convert_vaddr(
                    rawData,
                    capture.getWidth(),
                    capture.getHeight(),
                    srcFormat,
                    rgbBuffer.data(),
                    capture.getWidth(),
                    capture.getHeight(),
                    RK_FORMAT_RGB_888) != 0) {
                snapshot.errorMsg = QStringLiteral("RGA color conversion failed");
            } else {
                QImage rgbImage(
                    rgbBuffer.constData(),
                    capture.getWidth(),
                    capture.getHeight(),
                    capture.getWidth() * 3,
                    QImage::Format_RGB888
                );

                if (detector.infer(
                        rawData,
                        capture.getWidth(),
                        capture.getHeight(),
                        srcFormat,
                        &detGroup,
                        &scaleW,
                        &scaleH) != 0) {
                    snapshot.errorMsg = QStringLiteral("RKNN inference failed");
                    snapshot.frame = rgbImage.copy();
                    snapshot.detections.clear();
                } else {
                    snapshot.detections = formatDetections(detGroup);
                    snapshot.frame = renderAnnotatedFrame(rgbImage, detGroup);
                }
            }

            (void)rawSize;
            (void)scaleW;
            (void)scaleH;

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
            capture.releaseFrame();
        }

        capture.stopStream();
        capture.close();
        if (running_.load()) {
            sleepShort();
        }
    }
#else
    backend_->updateVisionState(
        makeStatusSnapshot(
            QStringLiteral("This host build does not enable the RK3568 local VisionRuntime"),
            false,
            false
        )
    );
    backend_->addLog("WARN", "VISION", QStringLiteral("This host build does not enable the board-side VisionRuntime"));
#endif

    running_.store(false);
}

}  // namespace dashboard
