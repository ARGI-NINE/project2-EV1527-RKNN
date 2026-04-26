#include "main_window.h"

#include "rf_status_page.h"
#include "system_log_page.h"
#include "vision_page.h"

#include <QFont>
#include <QStatusBar>
#include <QTabWidget>

namespace dashboard {

MainWindow::MainWindow(const AppOptions &options, QWidget *parent)
    : QMainWindow(parent),
      options_(options),
      rfClient_(&backend_, options_, this),
      visionRuntime_(&backend_, options_) {
    setupUi();
    setupRuntime();
}

MainWindow::~MainWindow() {
    visionRuntime_.stop();
    rfClient_.stop();
}

void MainWindow::setupUi() {
    setWindowTitle(QStringLiteral("RK3568 AIoT Dashboard"));
    resize(1360, 860);

    auto *tabs = new QTabWidget(this);
    tabs->setFont(QFont("Microsoft YaHei", 10));

    rfPage_ = new RFStatusPage(&backend_, tabs);
    visionPage_ = new VisionPage(&backend_, tabs);
    logPage_ = new SystemLogPage(&backend_, tabs);

    tabs->addTab(rfPage_, QStringLiteral("📡 RF 状态"));
    tabs->addTab(visionPage_, QStringLiteral("📷 视觉检测"));
    tabs->addTab(logPage_, QStringLiteral("📋 系统日志"));

    setCentralWidget(tabs);

    statusLabel_ = new QLabel(QStringLiteral("系统启动中..."), this);
    statusBar()->addPermanentWidget(statusLabel_);

    QObject::connect(&statusTimer_, &QTimer::timeout, this, [this]() { updateStatusBar(); });
    statusTimer_.start(2000);
}

void MainWindow::setupRuntime() {
    backend_.addLog("INFO", "SYSTEM", QStringLiteral("Qt5 前端已启动"));
    backend_.addLog(
        "INFO",
        "VISION",
        QStringLiteral("板侧本地视觉链路已接入，默认使用 %1").arg(options_.visionDevice)
    );

    rfClient_.start();
    visionRuntime_.start();
    updateStatusBar();
}

void MainWindow::updateStatusBar() {
    const SystemStats stats = backend_.snapshotSystemStats();
    const RFSnapshot rf = backend_.snapshotRF();
    const VisionSnapshot vision = backend_.snapshotVisionState();

    statusLabel_->setText(
        vision.statusReported
            ? QString("运行 %1s | RF帧 %2 | Vision FPS %3 | CPU %4%")
                  .arg(stats.uptimeSec)
                  .arg(rf.frameCount)
                  .arg(QString::number(vision.fps, 'f', 1))
                  .arg(QString::number(stats.cpuPercent, 'f', 0))
            : QString("运行 %1s | RF帧 %2 | Vision 未接入 | CPU %3%")
                  .arg(stats.uptimeSec)
                  .arg(rf.frameCount)
                  .arg(QString::number(stats.cpuPercent, 'f', 0))
    );
}

}  // namespace dashboard
