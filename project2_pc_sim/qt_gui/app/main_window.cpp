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
    visionPage_ = new VisionPage(&backend_, options_, tabs);
    logPage_ = new SystemLogPage(&backend_, tabs);

    tabs->addTab(rfPage_, QStringLiteral("RF Status"));
    tabs->addTab(visionPage_, QStringLiteral("Vision"));
    tabs->addTab(logPage_, QStringLiteral("System Log"));

    setCentralWidget(tabs);

    statusLabel_ = new QLabel(QStringLiteral("System starting..."), this);
    statusBar()->addPermanentWidget(statusLabel_);

    QObject::connect(&statusTimer_, &QTimer::timeout, this, [this]() { updateStatusBar(); });
    statusTimer_.start(2000);
}

void MainWindow::setupRuntime() {
    backend_.addLog(QStringLiteral("INFO"), QStringLiteral("SYSTEM"), QStringLiteral("Qt5 frontend started"));
    rfClient_.start();
    updateStatusBar();
}

void MainWindow::updateStatusBar() {
    const SystemStats stats = backend_.snapshotSystemStats();
    const RFSnapshot rf = backend_.snapshotRF();
    const VisionSnapshot vision = backend_.snapshotVisionState();

    statusLabel_->setText(
        QString("Uptime %1s | RF Frames %2 | Vision FPS %3 | CPU %4%")
            .arg(stats.uptimeSec)
            .arg(rf.frameCount)
            .arg(QString::number(vision.fps, 'f', 1))
            .arg(QString::number(stats.cpuPercent, 'f', 0))
    );
}

}  // namespace dashboard
