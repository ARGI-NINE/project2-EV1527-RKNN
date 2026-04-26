#pragma once

#include "app_options.h"
#include "dashboard_backend.h"

#include <QImage>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QTcpSocket>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace dashboard {

class DetectionOverlayWidget;

class VisionPage : public QWidget {
public:
    explicit VisionPage(DashboardBackend *backend, const AppOptions &options, QWidget *parent = nullptr);

private:
    void setupUi();
    void setupVisionBridge();
    void attemptVisionBridgeConnect();
    void onVisionBridgeReadyRead();
    void onVisionBridgeStateChanged(QAbstractSocket::SocketState state);
    void onVisionBridgeError(QAbstractSocket::SocketError socketError);
    void handleVisionBridgeLine(const QString &line);
    void applyVisionBridgePayload(const QJsonObject &obj);
    void resetRemoteState();
    void refresh();

    DashboardBackend *backend_ = nullptr;
    AppOptions options_;

    QString errorText_;
    QString visionBridgeHost_;
    quint16 visionBridgePort_ = 0u;
    QTcpSocket *visionSocket_ = nullptr;
    QByteArray visionBridgeBuffer_;
    QTimer visionReconnectTimer_;
    bool visionBridgeOnline_ = false;

    bool hasRemoteVision_ = false;
    qint64 remoteVisionUpdateMs_ = 0;
    double remoteFps_ = 0.0;
    bool remoteCameraOnline_ = false;
    bool remoteModelLoaded_ = false;
    int remoteFrameCount_ = 0;
    int remoteFrameWidth_ = 0;
    int remoteFrameHeight_ = 0;
    QString remoteError_;
    QStringList remoteDetections_;
    QImage remoteFrameImage_;

    struct RemoteDetectionBox {
        int x1 = 0;
        int y1 = 0;
        int x2 = 0;
        int y2 = 0;
        QString label;
        double score = 0.0;
    };
    QVector<RemoteDetectionBox> remoteBoxes_;

    QLabel *fpsLabel_ = nullptr;
    QLabel *bridgeLabel_ = nullptr;
    QLabel *cameraLabel_ = nullptr;
    QLabel *modelLabel_ = nullptr;
    QLabel *frameCountLabel_ = nullptr;
    QLabel *detCountLabel_ = nullptr;
    QListWidget *detList_ = nullptr;
    DetectionOverlayWidget *overlayWidget_ = nullptr;

    QTimer timer_;
};

}  // namespace dashboard
