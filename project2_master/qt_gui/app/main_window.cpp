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
            rfClient_(&backend_, options_, this) {
    setupUi();
    setupRuntime();
}

MainWindow::~MainWindow() {
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
    backend_.setVisionOffline(QStringLiteral("视觉输入未接入（板侧版本不提供内置视觉mock）"));
    backend_.addLog("INFO", "VISION", QStringLiteral("等待外部视觉状态接入"));

    rfClient_.start();
    updateStatusBar();
}

void MainWindow::updateStatusBar() {
    const SystemStats stats = backend_.snapshotSystemStats();
    const RFSnapshot rf = backend_.snapshotRF();
    const VisionSnapshot vision = backend_.snapshotVisionState();

    statusLabel_->setText(
        QString("运行 %1s | RF帧 %2 | Vision FPS %3 | CPU %4%")
            .arg(stats.uptimeSec)
            .arg(rf.frameCount)
            .arg(QString::number(vision.fps, 'f', 1))
            .arg(QString::number(stats.cpuPercent, 'f', 0))
    );
}

}  // namespace dashboard
