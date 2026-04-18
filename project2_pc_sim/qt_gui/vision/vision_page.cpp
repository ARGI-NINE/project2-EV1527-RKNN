#include "vision_page.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QPainter>
#include <QStackedLayout>
#include <QUrl>
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

    QObject::connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        if (pos != lastPositionMs_) {
            lastPositionMs_ = pos;
            ++frameCount_;
            ++fpsWindowFrames_;
        }
    });
    QObject::connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            player_->setPosition(0);
            player_->play();
        }
    });
    QObject::connect(
        player_,
        QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error),
        this,
        [this](QMediaPlayer::Error) {
            errorText_ = player_->errorString();
            if (backend_ != nullptr && !errorText_.isEmpty()) {
                backend_->addLog("ERROR", "VISION", errorText_);
            }
        }
    );

    setupVideoInput();
    setupVisionBridge();
    fpsTimer_.start();
    QObject::connect(&timer_, &QTimer::timeout, this, [this]() { refresh(); });
    timer_.start(33);
}

void VisionPage::setupUi() {
    auto *layout = new QHBoxLayout(this);

    auto *videoGroup = new QGroupBox(QStringLiteral("Video"), this);
    auto *videoLayout = new QVBoxLayout(videoGroup);
    auto *videoContainer = new QWidget(videoGroup);
    videoStack_ = new QStackedLayout(videoContainer);

    videoWidget_ = new QVideoWidget(videoContainer);
    videoWidget_->setMinimumSize(640, 420);
    overlayWidget_ = new DetectionOverlayWidget(videoContainer);
    overlayWidget_->setMinimumSize(640, 420);

    videoStack_->addWidget(videoWidget_);
    videoStack_->addWidget(overlayWidget_);
    videoStack_->setStackingMode(QStackedLayout::StackAll);
    videoStack_->setCurrentWidget(videoWidget_);

    videoLayout->addWidget(videoContainer);
    layout->addWidget(videoGroup, 3);

    player_ = new QMediaPlayer(this);
    player_->setVideoOutput(videoWidget_);
    player_->setNotifyInterval(60);

    auto *rightLayout = new QVBoxLayout();

    auto *statusGroup = new QGroupBox(QStringLiteral("Status"), this);
    auto *statusLayout = new QVBoxLayout(statusGroup);

    fpsLabel_ = new QLabel(QStringLiteral("FPS: 0.0"), statusGroup);
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

void VisionPage::setupVideoInput() {
    activeVideoPath_ = resolveVideoPath();
    if (options_.visionPort > 0) {
        if (backend_ != nullptr) {
            if (!activeVideoPath_.isEmpty()) {
                backend_->addLog("INFO", "VISION", QString("Video input resolved: %1").arg(activeVideoPath_));
            }
            backend_->addLog(
                "INFO",
                "VISION",
                QStringLiteral("Bridge frame display mode enabled: Qt will display WSL boxed frames only")
            );
        }
        return;
    }

    if (activeVideoPath_.isEmpty()) {
        errorText_ = QStringLiteral("Video input file not found (pass --video-input).");
        if (backend_ != nullptr) {
            backend_->setVisionOffline(errorText_);
            backend_->addLog("ERROR", "VISION", errorText_);
        }
        return;
    }

    player_->setMedia(QUrl::fromLocalFile(activeVideoPath_));
    player_->play();
    if (backend_ != nullptr) {
        backend_->addLog("INFO", "VISION", QString("Video input loaded: %1").arg(activeVideoPath_));
    }
}

void VisionPage::setupVisionBridge() {
    if (options_.visionPort <= 0) {
        return;
    }

    QString normalizeReason;
    const QString configuredHost = options_.visionHost.trimmed();
    visionBridgeHost_ = normalizedVisionBridgeHost(configuredHost, &normalizeReason);
    visionBridgePort_ = static_cast<quint16>(options_.visionPort);

    visionSocket_ = new QTcpSocket(this);
    // Force direct TCP path for local/WSL bridge; system HTTP proxies can break raw socket connects.
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

    if (backend_ != nullptr) {
        backend_->addLog(
            "INFO",
            "VISION",
            QString("WSL vision bridge enabled: %1:%2 (configured=%3, normalize=%4)")
                .arg(visionBridgeHost_)
                .arg(visionBridgePort_)
                .arg(configuredHost.isEmpty() ? QStringLiteral("<empty>") : configuredHost)
                .arg(normalizeReason)
        );
    }
    if (!visionReconnectTimer_.isActive()) {
        visionReconnectTimer_.start();
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
        qInfo().noquote() << QString("VISION_BRIDGE_STATE online=%1 host=%2 port=%3")
                                 .arg(visionBridgeOnline_ ? "true" : "false")
                                 .arg(visionBridgeHost_)
                                 .arg(visionBridgePort_);
        if (backend_ != nullptr) {
            if (visionBridgeOnline_) {
                backend_->addLog(
                    "INFO",
                    "VISION",
                    QString("WSL vision bridge connected: %1:%2").arg(visionBridgeHost_).arg(visionBridgePort_)
                );
            } else {
                backend_->addLog("WARN", "VISION", QStringLiteral("WSL vision bridge disconnected; retrying"));
            }
        }

        if (!visionBridgeOnline_) {
            hasRemoteVision_ = false;
            remoteVisionUpdateMs_ = 0;
            remoteFps_ = 0.0;
            remoteDisplayFps_ = 0.0;
            remoteCameraOnline_ = false;
            remoteModelLoaded_ = false;
            remoteFrameCount_ = 0;
            remoteFrameWidth_ = 0;
            remoteFrameHeight_ = 0;
            remoteDetections_.clear();
            remoteBoxes_.clear();
            remoteFrameImage_ = QImage();
            lastRemoteFrameCount_ = -1;
            lastRemoteFrameTickMs_ = 0;
            lastRemoteFrameDelta_ = 0;
        }
    }

    if (visionBridgeOnline_) {
        if (visionReconnectTimer_.isActive()) {
            visionReconnectTimer_.stop();
        }
    } else if (!visionReconnectTimer_.isActive()) {
        visionReconnectTimer_.start();
    }
}

void VisionPage::onVisionBridgeError(QAbstractSocket::SocketError socketError) {
    (void)socketError;
    if (backend_ == nullptr || visionSocket_ == nullptr) {
        return;
    }

    if (visionSocket_->state() != QAbstractSocket::ConnectedState) {
        backend_->addLog(
            "WARN",
            "VISION",
            QString("WSL vision bridge connection failed: %1").arg(visionSocket_->errorString())
        );
        if (!visionReconnectTimer_.isActive()) {
            visionReconnectTimer_.start();
        }
    }
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
            backend_->addLog("WARN", "VISION", QString("WSL vision bridge message parse failed: %1").arg(trimmed));
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
        const int newFrameCount = obj.value(QStringLiteral("frame_count")).toInt(remoteFrameCount_);
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (lastRemoteFrameCount_ >= 0 && newFrameCount >= lastRemoteFrameCount_ && lastRemoteFrameTickMs_ > 0) {
            const qint64 deltaFrames = static_cast<qint64>(newFrameCount - lastRemoteFrameCount_);
            const qint64 deltaMs = nowMs - lastRemoteFrameTickMs_;
            lastRemoteFrameDelta_ = deltaFrames;
            if (deltaFrames > 0 && deltaMs > 0) {
                remoteDisplayFps_ = (static_cast<double>(deltaFrames) * 1000.0) / static_cast<double>(deltaMs);
            }
        }
        lastRemoteFrameCount_ = newFrameCount;
        lastRemoteFrameTickMs_ = nowMs;
        remoteFrameCount_ = newFrameCount;
    }
    if (obj.contains(QStringLiteral("error"))) {
        remoteError_ = obj.value(QStringLiteral("error")).toString(remoteError_);
    }

    bool frameDecoded = false;
    QString frameDecodeError;

    const bool needRemoteFrameDecode = visionBridgePort_ > 0;

    if (needRemoteFrameDecode && obj.contains(QStringLiteral("frame_jpeg_b64"))) {
        const QString encoded = obj.value(QStringLiteral("frame_jpeg_b64")).toString();
        if (!encoded.isEmpty()) {
            const QByteArray jpegData = QByteArray::fromBase64(encoded.toLatin1());
            QImage decoded;
            if (decoded.loadFromData(jpegData, "JPG") || decoded.loadFromData(jpegData, "JPEG")) {
                remoteFrameImage_ = decoded;
                frameDecoded = true;
            } else {
                frameDecodeError = QString("JPEG decode failed: %1 bytes").arg(jpegData.size());
            }
        }
    }

    if (backend_ != nullptr) {
        if (frameDecoded && !remoteFrameDecodeLogged_) {
            remoteFrameDecodeLogged_ = true;
            remoteFrameDecodeWarned_ = false;
            backend_->addLog(
                "INFO",
                "VISION",
                QString("WSL vision frame decoded: %1x%2")
                    .arg(remoteFrameImage_.width())
                    .arg(remoteFrameImage_.height()));
        } else if (!frameDecoded && !frameDecodeError.isEmpty() && !remoteFrameDecodeWarned_) {
            remoteFrameDecodeWarned_ = true;
            backend_->addLog("WARN", "VISION", QString("WSL vision frame decode error: %1").arg(frameDecodeError));
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
                .arg(box.y2));
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
    if (remoteVisionUpdateMs_ - lastPayloadLogMs_ >= 1000) {
        const int detCount = !remoteBoxes_.isEmpty() ? remoteBoxes_.size() : remoteDetections_.size();
        qInfo().noquote() << QString("VISION_PAYLOAD frame=%1 fps=%2 det=%3 camera_online=%4 model_loaded=%5")
                                 .arg(remoteFrameCount_)
                                 .arg(QString::number(remoteFps_, 'f', 2))
                                 .arg(detCount)
                                 .arg(remoteCameraOnline_ ? "true" : "false")
                                 .arg(remoteModelLoaded_ ? "true" : "false");
        lastPayloadLogMs_ = remoteVisionUpdateMs_;
    }
    refresh();
}

QString VisionPage::resolveVideoPath() const {
    const QString cwd = QDir::currentPath();
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates;

    if (options_.videoPath.trimmed().isEmpty()) {
        return QString();
    }
    candidates << options_.videoPath.trimmed();

    for (const QString &candidate : candidates) {
        if (candidate.isEmpty()) {
            continue;
        }

        const QFileInfo asGiven(candidate);
        if (asGiven.exists() && asGiven.isFile()) {
            return asGiven.absoluteFilePath();
        }

        const QFileInfo inCwd(QDir(cwd).filePath(candidate));
        if (inCwd.exists() && inCwd.isFile()) {
            return inCwd.absoluteFilePath();
        }

        const QFileInfo inApp(QDir(appDir).filePath(candidate));
        if (inApp.exists() && inApp.isFile()) {
            return inApp.absoluteFilePath();
        }

        const QFileInfo inParent(QDir(appDir).filePath(QStringLiteral("../") + candidate));
        if (inParent.exists() && inParent.isFile()) {
            return inParent.absoluteFilePath();
        }

        const QFileInfo inGrandParent(QDir(appDir).filePath(QStringLiteral("../../") + candidate));
        if (inGrandParent.exists() && inGrandParent.isFile()) {
            return inGrandParent.absoluteFilePath();
        }
    }

    return QString();
}

void VisionPage::refresh() {
    if (backend_ == nullptr) {
        return;
    }

    const qint64 elapsedMs = fpsTimer_.elapsed();
    if (elapsedMs >= 1000) {
        currentFps_ = (static_cast<double>(fpsWindowFrames_) * 1000.0) / static_cast<double>(elapsedMs);
        fpsWindowFrames_ = 0;
        fpsTimer_.restart();
    }

    const bool localPlaying = player_ != nullptr && player_->state() == QMediaPlayer::PlayingState;
    const bool localLoaded = player_ != nullptr &&
        (player_->mediaStatus() == QMediaPlayer::LoadedMedia ||
         player_->mediaStatus() == QMediaPlayer::BufferingMedia ||
         player_->mediaStatus() == QMediaPlayer::BufferedMedia ||
         player_->mediaStatus() == QMediaPlayer::EndOfMedia);

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool bridgeConfigured = visionBridgePort_ > 0;
    const bool remoteConnected = hasRemoteVision_ && visionBridgeOnline_;
    const bool remoteFresh = remoteConnected && (nowMs - remoteVisionUpdateMs_ <= 3000);
    const bool useRemote = remoteConnected;
    const bool preferLocalVideo = !bridgeConfigured && !activeVideoPath_.isEmpty() && localLoaded;
    const bool useRemoteFrame = useRemote && !remoteFrameImage_.isNull();

    const double inferFps = bridgeConfigured ? remoteFps_ : currentFps_;
    const double remoteDisplayFps = (remoteDisplayFps_ > 0.05) ? remoteDisplayFps_ : remoteFps_;
    double displayFps = 0.0;
    if (bridgeConfigured) {
        displayFps = remoteDisplayFps;
    } else {
        displayFps = currentFps_;
    }
    const bool playing = bridgeConfigured ? (visionBridgeOnline_ && remoteCameraOnline_) : localPlaying;
    const bool loaded = bridgeConfigured ? (visionBridgeOnline_ && remoteModelLoaded_) : localLoaded;
    const int shownFrameCount = bridgeConfigured ? remoteFrameCount_ : frameCount_;
    QString displayError = useRemote ? remoteError_ : errorText_;
    if (bridgeConfigured && !visionBridgeOnline_) {
        displayError = QStringLiteral("WSL vision bridge not connected");
    } else if (useRemote && !remoteFresh && displayError.isEmpty()) {
        displayError = QStringLiteral("WSL vision payload stale");
    }
    const bool hasError = !displayError.isEmpty();

    if (overlayWidget_ != nullptr) {
        QVector<DetectionOverlayWidget::OverlayBox> drawBoxes;
        int drawFrameWidth = 0;
        int drawFrameHeight = 0;
        QImage drawImage;
        if (useRemote) {
            if (preferLocalVideo) {
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
            drawFrameWidth = remoteFrameWidth_;
            drawFrameHeight = remoteFrameHeight_;
        }
        if (useRemoteFrame) {
            drawImage = remoteFrameImage_;
            if (drawFrameWidth <= 0) {
                drawFrameWidth = remoteFrameImage_.width();
            }
            if (drawFrameHeight <= 0) {
                drawFrameHeight = remoteFrameImage_.height();
            }
        }

        if (videoStack_ != nullptr && videoWidget_ != nullptr) {
            if (bridgeConfigured) {
                videoStack_->setCurrentWidget(overlayWidget_);
            } else if (preferLocalVideo) {
                videoStack_->setCurrentWidget(videoWidget_);
                overlayWidget_->raise();
            } else if (useRemoteFrame) {
                videoStack_->setCurrentWidget(overlayWidget_);
            } else {
                videoStack_->setCurrentWidget(videoWidget_);
            }
        }

        overlayWidget_->setFrameImage(drawImage);
        overlayWidget_->setDetections(drawBoxes, drawFrameWidth, drawFrameHeight);
    }

    if (bridgeConfigured) {
        fpsLabel_->setText(
            QString("Infer FPS: %1 | Display FPS: %2")
                .arg(QString::number(inferFps, 'f', 1))
                .arg(QString::number(displayFps, 'f', 1))
        );
    } else {
        fpsLabel_->setText(QString("FPS: %1").arg(QString::number(displayFps, 'f', 1)));
    }
    if (bridgeLabel_ != nullptr) {
        bridgeLabel_->setText(QString("Bridge: %1").arg(visionBridgeOnline_ ? QStringLiteral("connected") : QStringLiteral("disconnected")));
        bridgeLabel_->setStyleSheet(visionBridgeOnline_ ? "color: #32cd32;" : "color: #ffb347;");
    }

    cameraLabel_->setText(QString("Camera: %1").arg(playing ? QStringLiteral("online") : QStringLiteral("offline")));
    cameraLabel_->setStyleSheet(playing ? "color: #32cd32;" : "color: #ff5a5a;");

    modelLabel_->setText(QString("Model: %1").arg(loaded ? QStringLiteral("loaded") : QStringLiteral("unloaded")));
    modelLabel_->setStyleSheet(loaded ? "color: #32cd32;" : "color: #ff5a5a;");

    frameCountLabel_->setText(QString("Frames: %1").arg(shownFrameCount));
    if (detCountLabel_ != nullptr) {
        const int detCount = useRemote ? remoteDetections_.size() : 0;
        detCountLabel_->setText(QString("Detections: %1").arg(detCount));
    }

    detList_->clear();
    if (hasError) {
        detList_->addItem(displayError);
        if (useRemote && lastRemoteFrameDelta_ > 1 && lastRemoteFrameTickMs_ > 0) {
            detList_->addItem(
                QString("Remote frame jump detected: +%1 frames")
                    .arg(lastRemoteFrameDelta_));
        }
    } else if (useRemote && !remoteDetections_.isEmpty()) {
        for (const QString &item : remoteDetections_) {
            detList_->addItem(item);
        }
        if (lastRemoteFrameDelta_ > 1) {
            detList_->addItem(
                QString("Remote frame jump detected: +%1 frames")
                    .arg(lastRemoteFrameDelta_));
        }
    } else if (visionBridgePort_ > 0 && !visionBridgeOnline_) {
        detList_->addItem(QStringLiteral("WSL vision bridge not connected (auto reconnecting)"));
    } else if (!bridgeConfigured && activeVideoPath_.isEmpty()) {
        detList_->addItem(QStringLiteral("Video input not configured (pass --video-input)"));
    } else if (bridgeConfigured && visionBridgeOnline_ && remoteFrameImage_.isNull()) {
        detList_->addItem(QStringLiteral("Waiting for boxed frame from WSL vision bridge"));
    } else {
        detList_->addItem(QFileInfo(activeVideoPath_).fileName());
    }

    VisionSnapshot snapshot;
    snapshot.fps = inferFps;
    snapshot.cameraOnline = playing;
    snapshot.modelLoaded = loaded;
    snapshot.frameCount = shownFrameCount;
    snapshot.errorMsg = displayError;
    if (useRemote) {
        snapshot.detections = remoteDetections_;
    }
    backend_->updateVisionState(snapshot);

    if (nowMs - lastUiLogMs_ >= 1000) {
        qInfo().noquote() << QString("VISION_UI bridge=%1 camera=%2 model=%3 use_remote=%4 infer_fps=%5 display_fps=%6 frames=%7 det=%8 error=%9")
                                 .arg(visionBridgeOnline_ ? "connected" : "disconnected")
                                 .arg(playing ? "online" : "offline")
                                 .arg(loaded ? "loaded" : "unloaded")
                                 .arg(useRemote ? "true" : "false")
                                 .arg(QString::number(inferFps, 'f', 2))
                                 .arg(QString::number(displayFps, 'f', 2))
                                 .arg(shownFrameCount)
                                 .arg(remoteDetections_.size())
                                 .arg(displayError.isEmpty() ? "none" : displayError);
        lastUiLogMs_ = nowMs;
    }
}

}  // namespace dashboard


