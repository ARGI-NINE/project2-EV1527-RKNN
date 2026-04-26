#include "vision_page.h"

#include <QDateTime>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QPainter>
#include <QVBoxLayout>

namespace dashboard {

static QString normalizedVisionBridgeHost(const QString &configured, QString *reason) {
    const QString host = configured.trimmed();
    if (host.isEmpty()) {
        if (reason != nullptr) {
            *reason = QStringLiteral("empty host -> 127.0.0.1");
        }
        return QStringLiteral("127.0.0.1");
    }
    if (host == QStringLiteral("localhost")) {
        if (reason != nullptr) {
            *reason = QStringLiteral("localhost -> 127.0.0.1");
        }
        return QStringLiteral("127.0.0.1");
    }
    if (host == QStringLiteral("0.0.0.0")) {
        if (reason != nullptr) {
            *reason = QStringLiteral("0.0.0.0 is bind-only -> 127.0.0.1");
        }
        return QStringLiteral("127.0.0.1");
    }
    if (reason != nullptr) {
        *reason = QStringLiteral("as configured");
    }
    return host;
}

class DetectionOverlayWidget : public QWidget {
public:
    struct OverlayBox {
        int x1 = 0;
        int y1 = 0;
        int x2 = 0;
        int y2 = 0;
        QString label;
        double score = 0.0;
    };

    explicit DetectionOverlayWidget(QWidget *parent = nullptr)
        : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

    void setDetections(const QVector<OverlayBox> &boxes, int frameWidth, int frameHeight) {
        boxes_ = boxes;
        frameWidth_ = frameWidth;
        frameHeight_ = frameHeight;
        update();
    }

    void setFrameImage(const QImage &image) {
        frameImage_ = image;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QWidget::paintEvent(event);
        const int baseW = frameWidth_ > 0 ? frameWidth_ : frameImage_.width();
        const int baseH = frameHeight_ > 0 ? frameHeight_ : frameImage_.height();
        if ((boxes_.isEmpty() && frameImage_.isNull()) || baseW <= 0 || baseH <= 0) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const double sx = static_cast<double>(width()) / static_cast<double>(baseW);
        const double sy = static_cast<double>(height()) / static_cast<double>(baseH);
        const double scale = qMin(sx, sy);
        if (scale <= 0.0) {
            return;
        }

        const double drawW = static_cast<double>(baseW) * scale;
        const double drawH = static_cast<double>(baseH) * scale;
        const double offsetX = (static_cast<double>(width()) - drawW) * 0.5;
        const double offsetY = (static_cast<double>(height()) - drawH) * 0.5;

        const QRectF targetRect(offsetX, offsetY, drawW, drawH);
        if (!frameImage_.isNull()) {
            painter.drawImage(targetRect, frameImage_);
        }

        if (boxes_.isEmpty()) {
            return;
        }

        QPen pen(QColor(52, 205, 50));
        pen.setWidth(2);
        painter.setPen(pen);

        for (const OverlayBox &box : boxes_) {
            const double x1 = offsetX + static_cast<double>(box.x1) * scale;
            const double y1 = offsetY + static_cast<double>(box.y1) * scale;
            const double x2 = offsetX + static_cast<double>(box.x2) * scale;
            const double y2 = offsetY + static_cast<double>(box.y2) * scale;

            QRectF rect(QPointF(x1, y1), QPointF(x2, y2));
            rect = rect.normalized();
            if (rect.width() < 2.0 || rect.height() < 2.0) {
                continue;
            }
            painter.drawRect(rect);

            const QString text = QString("%1 %2")
                                     .arg(box.label)
                                     .arg(QString::number(box.score, 'f', 2));
            const QFontMetrics fm(painter.font());
            QRect textRect = fm.boundingRect(text).adjusted(-4, -2, 4, 2);
            textRect.moveTo(static_cast<int>(rect.left()), static_cast<int>(rect.top()) - textRect.height() - 2);
            if (textRect.top() < 0) {
                textRect.moveTop(0);
            }

            painter.fillRect(textRect, QColor(0, 0, 0, 160));
            painter.setPen(QColor(230, 255, 230));
            painter.drawText(textRect.adjusted(4, 2, -4, -2), Qt::AlignLeft | Qt::AlignVCenter, text);
            painter.setPen(pen);
        }
    }

private:
    QVector<OverlayBox> boxes_;
    int frameWidth_ = 0;
    int frameHeight_ = 0;
    QImage frameImage_;
};

VisionPage::VisionPage(DashboardBackend *backend, const AppOptions &options, QWidget *parent)
    : QWidget(parent),
      backend_(backend),
      options_(options) {
    setupUi();
    setupVisionBridge();
    QObject::connect(&timer_, &QTimer::timeout, this, [this]() { refresh(); });
    timer_.start(33);
}

void VisionPage::setupUi() {
    auto *layout = new QHBoxLayout(this);

    auto *videoGroup = new QGroupBox(QStringLiteral("Video"), this);
    auto *videoLayout = new QVBoxLayout(videoGroup);
    overlayWidget_ = new DetectionOverlayWidget(videoGroup);
    overlayWidget_->setMinimumSize(640, 420);
    videoLayout->addWidget(overlayWidget_);
    layout->addWidget(videoGroup, 3);

    auto *rightLayout = new QVBoxLayout();

    auto *statusGroup = new QGroupBox(QStringLiteral("Status"), this);
    auto *statusLayout = new QVBoxLayout(statusGroup);

    fpsLabel_ = new QLabel(QStringLiteral("Infer FPS: 0.0"), statusGroup);
    fpsLabel_->setFont(QFont("Consolas", 24, QFont::Bold));
    fpsLabel_->setAlignment(Qt::AlignCenter);

    bridgeLabel_ = new QLabel(QStringLiteral("Bridge: disconnected"), statusGroup);
    cameraLabel_ = new QLabel(QStringLiteral("Camera: offline"), statusGroup);
    modelLabel_ = new QLabel(QStringLiteral("Model: unloaded"), statusGroup);
    frameCountLabel_ = new QLabel(QStringLiteral("Frames: 0"), statusGroup);
    detCountLabel_ = new QLabel(QStringLiteral("Detections: 0"), statusGroup);

    statusLayout->addWidget(fpsLabel_);
    statusLayout->addWidget(bridgeLabel_);
    statusLayout->addWidget(cameraLabel_);
    statusLayout->addWidget(modelLabel_);
    statusLayout->addWidget(frameCountLabel_);
    statusLayout->addWidget(detCountLabel_);
    rightLayout->addWidget(statusGroup);

    auto *detGroup = new QGroupBox(QStringLiteral("Detections"), this);
    auto *detLayout = new QVBoxLayout(detGroup);
    detList_ = new QListWidget(detGroup);
    detList_->setFont(QFont("Consolas", 10));
    detLayout->addWidget(detList_);
    rightLayout->addWidget(detGroup, 1);

    layout->addLayout(rightLayout, 1);
}

void VisionPage::setupVisionBridge() {
    if (options_.visionPort <= 0) {
        errorText_ = QStringLiteral("WSL vision bridge disabled");
        if (backend_ != nullptr) {
            backend_->setVisionOffline(errorText_);
            backend_->addLog(QStringLiteral("WARN"), QStringLiteral("VISION"), errorText_);
        }
        refresh();
        return;
    }

    QString normalizeReason;
    const QString configuredHost = options_.visionHost.trimmed();
    visionBridgeHost_ = normalizedVisionBridgeHost(configuredHost, &normalizeReason);
    Q_UNUSED(normalizeReason);
    visionBridgePort_ = static_cast<quint16>(options_.visionPort);

    visionSocket_ = new QTcpSocket(this);
    visionSocket_->setProxy(QNetworkProxy::NoProxy);
    QObject::connect(visionSocket_, &QTcpSocket::readyRead, this, [this]() { onVisionBridgeReadyRead(); });
    QObject::connect(
        visionSocket_,
        &QTcpSocket::stateChanged,
        this,
        [this](QAbstractSocket::SocketState state) { onVisionBridgeStateChanged(state); }
    );
    QObject::connect(
        visionSocket_,
        QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
        this,
        [this](QAbstractSocket::SocketError socketError) { onVisionBridgeError(socketError); }
    );

    visionReconnectTimer_.setInterval(2000);
    QObject::connect(&visionReconnectTimer_, &QTimer::timeout, this, [this]() { attemptVisionBridgeConnect(); });
    visionReconnectTimer_.start();
    if (backend_ != nullptr) {
        backend_->addLog(
            QStringLiteral("INFO"),
            QStringLiteral("VISION"),
            QString("WSL vision bridge enabled: %1:%2").arg(visionBridgeHost_).arg(visionBridgePort_)
        );
    }
    attemptVisionBridgeConnect();
}

void VisionPage::attemptVisionBridgeConnect() {
    if (visionSocket_ == nullptr) {
        return;
    }
    if (visionSocket_->state() == QAbstractSocket::ConnectedState ||
        visionSocket_->state() == QAbstractSocket::ConnectingState) {
        return;
    }

    visionSocket_->abort();
    visionSocket_->connectToHost(visionBridgeHost_, visionBridgePort_);
}

void VisionPage::onVisionBridgeReadyRead() {
    if (visionSocket_ == nullptr) {
        return;
    }

    visionBridgeBuffer_.append(visionSocket_->readAll());
    while (true) {
        const int newline = visionBridgeBuffer_.indexOf('\n');
        if (newline < 0) {
            break;
        }

        QByteArray line = visionBridgeBuffer_.left(newline);
        visionBridgeBuffer_.remove(0, newline + 1);
        if (!line.isEmpty() && line.endsWith('\r')) {
            line.chop(1);
        }
        handleVisionBridgeLine(QString::fromUtf8(line));
    }
}

void VisionPage::onVisionBridgeStateChanged(QAbstractSocket::SocketState state) {
    bool stateKnown = false;
    bool nowOnline = visionBridgeOnline_;
    if (state == QAbstractSocket::ConnectedState) {
        stateKnown = true;
        nowOnline = true;
    } else if (state == QAbstractSocket::UnconnectedState) {
        stateKnown = true;
        nowOnline = false;
    }
    if (!stateKnown) {
        return;
    }

    if (nowOnline != visionBridgeOnline_) {
        visionBridgeOnline_ = nowOnline;
        if (backend_ != nullptr) {
            if (visionBridgeOnline_) {
                backend_->addLog(
                    QStringLiteral("INFO"),
                    QStringLiteral("VISION"),
                    QString("WSL vision bridge connected: %1:%2").arg(visionBridgeHost_).arg(visionBridgePort_)
                );
            } else {
                backend_->addLog(QStringLiteral("WARN"), QStringLiteral("VISION"), QStringLiteral("WSL vision bridge disconnected; retrying"));
            }
        }
        if (!visionBridgeOnline_) {
            resetRemoteState();
        }
    }

    if (visionBridgeOnline_) {
        if (visionReconnectTimer_.isActive()) {
            visionReconnectTimer_.stop();
        }
    } else if (!visionReconnectTimer_.isActive()) {
        visionReconnectTimer_.start();
    }
    refresh();
}

void VisionPage::onVisionBridgeError(QAbstractSocket::SocketError socketError) {
    Q_UNUSED(socketError);
    if (visionSocket_ == nullptr) {
        return;
    }

    if (visionSocket_->state() != QAbstractSocket::ConnectedState) {
        if (backend_ != nullptr) {
            backend_->addLog(
                QStringLiteral("WARN"),
                QStringLiteral("VISION"),
                QString("WSL vision bridge connection failed: %1").arg(visionSocket_->errorString())
            );
        }
    }

    if (visionSocket_->state() != QAbstractSocket::ConnectedState && !visionReconnectTimer_.isActive()) {
        visionReconnectTimer_.start();
    }
    refresh();
}

void VisionPage::handleVisionBridgeLine(const QString &line) {
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (backend_ != nullptr) {
            backend_->addLog(
                QStringLiteral("WARN"),
                QStringLiteral("VISION"),
                QString("WSL vision bridge message parse failed: %1").arg(trimmed)
            );
        }
        return;
    }

    applyVisionBridgePayload(doc.object());
}

void VisionPage::applyVisionBridgePayload(const QJsonObject &obj) {
    if (obj.contains(QStringLiteral("fps"))) {
        remoteFps_ = obj.value(QStringLiteral("fps")).toDouble(remoteFps_);
    }
    if (obj.contains(QStringLiteral("camera_online"))) {
        remoteCameraOnline_ = obj.value(QStringLiteral("camera_online")).toBool(remoteCameraOnline_);
    }
    if (obj.contains(QStringLiteral("model_loaded"))) {
        remoteModelLoaded_ = obj.value(QStringLiteral("model_loaded")).toBool(remoteModelLoaded_);
    }
    if (obj.contains(QStringLiteral("frame_count"))) {
        remoteFrameCount_ = obj.value(QStringLiteral("frame_count")).toInt(remoteFrameCount_);
    }
    if (obj.contains(QStringLiteral("error"))) {
        remoteError_ = obj.value(QStringLiteral("error")).toString(remoteError_);
    }

    remoteFrameImage_ = QImage();
    if (obj.contains(QStringLiteral("frame_jpeg_b64"))) {
        const QString encoded = obj.value(QStringLiteral("frame_jpeg_b64")).toString();
        if (!encoded.isEmpty()) {
            const QByteArray jpegData = QByteArray::fromBase64(encoded.toLatin1());
            QImage decoded;
            if (decoded.loadFromData(jpegData, "JPG") || decoded.loadFromData(jpegData, "JPEG")) {
                remoteFrameImage_ = decoded;
            } else {
                remoteError_ = QStringLiteral("Failed to decode bridge frame");
            }
        }
    }

    remoteDetections_.clear();
    remoteBoxes_.clear();

    if (obj.contains(QStringLiteral("frame_width"))) {
        remoteFrameWidth_ = obj.value(QStringLiteral("frame_width")).toInt(remoteFrameWidth_);
    }
    if (obj.contains(QStringLiteral("frame_height"))) {
        remoteFrameHeight_ = obj.value(QStringLiteral("frame_height")).toInt(remoteFrameHeight_);
    }

    const QJsonArray dets = obj.value(QStringLiteral("detections")).toArray();
    int maxX = 0;
    int maxY = 0;
    for (const QJsonValue &v : dets) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject detObj = v.toObject();
        RemoteDetectionBox box;
        box.label = detObj.value(QStringLiteral("label")).toString(QStringLiteral("obj"));
        box.score = detObj.value(QStringLiteral("score")).toDouble(0.0);
        box.x1 = detObj.value(QStringLiteral("x1")).toInt(0);
        box.y1 = detObj.value(QStringLiteral("y1")).toInt(0);
        box.x2 = detObj.value(QStringLiteral("x2")).toInt(0);
        box.y2 = detObj.value(QStringLiteral("y2")).toInt(0);
        maxX = qMax(maxX, qMax(box.x1, box.x2));
        maxY = qMax(maxY, qMax(box.y1, box.y2));
        remoteBoxes_.append(box);
        remoteDetections_.append(
            QString("%1 %2 [%3,%4,%5,%6]")
                .arg(box.label)
                .arg(QString::number(box.score, 'f', 2))
                .arg(box.x1)
                .arg(box.y1)
                .arg(box.x2)
                .arg(box.y2)
        );
    }

    if (!remoteBoxes_.isEmpty()) {
        if (remoteFrameWidth_ <= 0) {
            remoteFrameWidth_ = maxX;
        }
        if (remoteFrameHeight_ <= 0) {
            remoteFrameHeight_ = maxY;
        }
    }

    hasRemoteVision_ = true;
    remoteVisionUpdateMs_ = QDateTime::currentMSecsSinceEpoch();
    refresh();
}

void VisionPage::resetRemoteState() {
    hasRemoteVision_ = false;
    remoteVisionUpdateMs_ = 0;
    remoteFps_ = 0.0;
    remoteCameraOnline_ = false;
    remoteModelLoaded_ = false;
    remoteFrameCount_ = 0;
    remoteFrameWidth_ = 0;
    remoteFrameHeight_ = 0;
    remoteError_.clear();
    remoteDetections_.clear();
    remoteBoxes_.clear();
    remoteFrameImage_ = QImage();
}

void VisionPage::refresh() {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool bridgeConfigured = visionBridgePort_ > 0;
    const bool remoteFresh = bridgeConfigured && visionBridgeOnline_ && hasRemoteVision_ && (nowMs - remoteVisionUpdateMs_ <= 3000);

    QString displayError = errorText_;
    if (bridgeConfigured && !visionBridgeOnline_) {
        displayError = QStringLiteral("WSL vision bridge not connected");
    } else if (bridgeConfigured && !hasRemoteVision_ && displayError.isEmpty()) {
        displayError = QStringLiteral("Waiting for vision bridge payload");
    } else if (bridgeConfigured && !remoteFresh && displayError.isEmpty()) {
        displayError = QStringLiteral("WSL vision payload stale");
    } else if (bridgeConfigured && !remoteError_.isEmpty()) {
        displayError = remoteError_;
    }

    const bool cameraOnline = remoteFresh && remoteCameraOnline_;
    const bool modelLoaded = remoteFresh && remoteModelLoaded_;
    const double inferFps = remoteFresh ? remoteFps_ : 0.0;
    const int shownFrameCount = remoteFresh ? remoteFrameCount_ : 0;
    const int detCount = remoteFresh ? remoteDetections_.size() : 0;

    if (overlayWidget_ != nullptr) {
        QVector<DetectionOverlayWidget::OverlayBox> drawBoxes;
        if (remoteFresh) {
            drawBoxes.reserve(remoteBoxes_.size());
            for (const RemoteDetectionBox &src : remoteBoxes_) {
                DetectionOverlayWidget::OverlayBox dst;
                dst.x1 = src.x1;
                dst.y1 = src.y1;
                dst.x2 = src.x2;
                dst.y2 = src.y2;
                dst.label = src.label;
                dst.score = src.score;
                drawBoxes.append(dst);
            }
        }
        overlayWidget_->setFrameImage(remoteFresh ? remoteFrameImage_ : QImage());
        overlayWidget_->setDetections(drawBoxes, remoteFresh ? remoteFrameWidth_ : 0, remoteFresh ? remoteFrameHeight_ : 0);
    }

    fpsLabel_->setText(QString("Infer FPS: %1").arg(QString::number(inferFps, 'f', 1)));
    bridgeLabel_->setText(QString("Bridge: %1").arg(visionBridgeOnline_ ? QStringLiteral("connected") : QStringLiteral("disconnected")));
    bridgeLabel_->setStyleSheet(visionBridgeOnline_ ? "color: #32cd32;" : "color: #ffb347;");

    cameraLabel_->setText(QString("Camera: %1").arg(cameraOnline ? QStringLiteral("online") : QStringLiteral("offline")));
    cameraLabel_->setStyleSheet(cameraOnline ? "color: #32cd32;" : "color: #ff5a5a;");

    modelLabel_->setText(QString("Model: %1").arg(modelLoaded ? QStringLiteral("loaded") : QStringLiteral("unloaded")));
    modelLabel_->setStyleSheet(modelLoaded ? "color: #32cd32;" : "color: #ff5a5a;");

    frameCountLabel_->setText(QString("Frames: %1").arg(shownFrameCount));
    detCountLabel_->setText(QString("Detections: %1").arg(detCount));

    detList_->clear();
    if (!displayError.isEmpty()) {
        detList_->addItem(displayError);
    } else if (remoteFresh && !remoteDetections_.isEmpty()) {
        for (const QString &item : remoteDetections_) {
            detList_->addItem(item);
        }
    } else {
        detList_->addItem(QStringLiteral("No detections"));
    }

    if (backend_ != nullptr) {
        VisionSnapshot snapshot;
        snapshot.frame = remoteFresh ? remoteFrameImage_ : QImage();
        snapshot.detections = remoteFresh ? remoteDetections_ : QStringList();
        snapshot.fps = inferFps;
        snapshot.cameraOnline = cameraOnline;
        snapshot.modelLoaded = modelLoaded;
        snapshot.frameCount = shownFrameCount;
        snapshot.errorMsg = displayError;
        backend_->updateVisionState(snapshot);
    }
}

}  // namespace dashboard
