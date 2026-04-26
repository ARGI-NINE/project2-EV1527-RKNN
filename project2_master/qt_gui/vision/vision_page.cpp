#include "vision_page.h"

#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPixmap>
#include <QVBoxLayout>

namespace dashboard {

VisionPage::VisionPage(DashboardBackend *backend, QWidget *parent)
    : QWidget(parent),
      backend_(backend) {
    setupUi();
    QObject::connect(&timer_, &QTimer::timeout, this, [this]() { refresh(); });
    timer_.start(33);
}

void VisionPage::setupUi() {
    auto *layout = new QHBoxLayout(this);

    auto *videoGroup = new QGroupBox(QStringLiteral("摄像头视频"), this);
    auto *videoLayout = new QVBoxLayout(videoGroup);
    videoLabel_ = new QLabel(QStringLiteral("等待摄像头..."), videoGroup);
    videoLabel_->setAlignment(Qt::AlignCenter);
    videoLabel_->setMinimumSize(640, 420);
    videoLabel_->setStyleSheet("background-color: #1a1a1a; color: #999;");
    videoLayout->addWidget(videoLabel_);
    layout->addWidget(videoGroup, 3);

    auto *rightLayout = new QVBoxLayout();

    auto *statusGroup = new QGroupBox(QStringLiteral("状态"), this);
    auto *statusLayout = new QVBoxLayout(statusGroup);

    fpsLabel_ = new QLabel(QStringLiteral("FPS: 0.0"), statusGroup);
    fpsLabel_->setFont(QFont("Consolas", 24, QFont::Bold));
    fpsLabel_->setAlignment(Qt::AlignCenter);

    cameraLabel_ = new QLabel(QStringLiteral("摄像头: 离线"), statusGroup);
    modelLabel_ = new QLabel(QStringLiteral("模型: 未加载"), statusGroup);
    frameCountLabel_ = new QLabel(QStringLiteral("总帧数: 0"), statusGroup);

    statusLayout->addWidget(fpsLabel_);
    statusLayout->addWidget(cameraLabel_);
    statusLayout->addWidget(modelLabel_);
    statusLayout->addWidget(frameCountLabel_);
    rightLayout->addWidget(statusGroup);

    auto *detGroup = new QGroupBox(QStringLiteral("检测结果"), this);
    auto *detLayout = new QVBoxLayout(detGroup);
    detList_ = new QListWidget(detGroup);
    detList_->setFont(QFont("Consolas", 10));
    detLayout->addWidget(detList_);
    rightLayout->addWidget(detGroup, 1);

    layout->addLayout(rightLayout, 1);
}

void VisionPage::refresh() {
    if (backend_ == nullptr) {
        return;
    }

    const VisionSnapshot snapshot = backend_->snapshotVisionState();
    const bool statusReported = snapshot.statusReported;

    fpsLabel_->setText(
        statusReported
            ? QString("FPS: %1").arg(QString::number(snapshot.fps, 'f', 1))
            : QStringLiteral("FPS: --")
    );

    if (!statusReported) {
        cameraLabel_->setText(QStringLiteral("摄像头: 未接入"));
        cameraLabel_->setStyleSheet("color: #999;");
        modelLabel_->setText(QStringLiteral("模型: 状态未上报"));
        modelLabel_->setStyleSheet("color: #999;");
        frameCountLabel_->setText(QStringLiteral("总帧数: --"));
    } else {
        cameraLabel_->setText(
            QString("摄像头: %1")
                .arg(snapshot.cameraOnline ? QStringLiteral("在线")
                                           : QStringLiteral("离线"))
        );
        cameraLabel_->setStyleSheet(snapshot.cameraOnline ? "color: #32cd32;" : "color: #ff5a5a;");

        modelLabel_->setText(
            QString("模型: %1")
                .arg(snapshot.modelLoaded ? QStringLiteral("已加载")
                                          : QStringLiteral("未加载"))
        );
        modelLabel_->setStyleSheet(snapshot.modelLoaded ? "color: #32cd32;" : "color: #ff5a5a;");
        frameCountLabel_->setText(QString("总帧数: %1").arg(snapshot.frameCount));
    }

    if (!snapshot.frame.isNull()) {
        const QPixmap pixmap = QPixmap::fromImage(snapshot.frame);
        videoLabel_->setText(QString());
        videoLabel_->setPixmap(pixmap.scaled(videoLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        videoLabel_->setPixmap(QPixmap());
        videoLabel_->setText(
            snapshot.errorMsg.isEmpty()
                ? (statusReported
                       ? QStringLiteral("等待摄像头...")
                       : QStringLiteral("视觉链路未接入"))
                : snapshot.errorMsg
        );
    }

    detList_->clear();
    if (!statusReported) {
        detList_->addItem(QStringLiteral("等待板侧视觉状态接入"));
    } else {
        for (const QString &det : snapshot.detections) {
            detList_->addItem(det);
        }
        if (detList_->count() == 0) {
            detList_->addItem(QStringLiteral("无检测目标"));
        }
    }
}

}  // namespace dashboard
