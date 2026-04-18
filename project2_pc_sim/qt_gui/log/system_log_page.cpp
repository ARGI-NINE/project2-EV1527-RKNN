#include "system_log_page.h"

#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QVBoxLayout>

namespace dashboard {

SystemLogPage::SystemLogPage(DashboardBackend *backend, QWidget *parent)
    : QWidget(parent),
      backend_(backend) {
    setupUi();
    QObject::connect(&timer_, &QTimer::timeout, this, [this]() { refresh(); });
    timer_.start(1000);
}

void SystemLogPage::setupUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);
    layout->setContentsMargins(10, 6, 10, 6);

    auto *topLayout = new QHBoxLayout();
    topLayout->setSpacing(8);

    auto *mqttGroup = new QGroupBox(QStringLiteral("MQTT"), this);
    auto *mqttLayout = new QVBoxLayout(mqttGroup);
    mqttCountLabel_ = new QLabel(QStringLiteral("Publish Count: 0"), mqttGroup);
    mqttCountLabel_->setFont(QFont("Consolas", 13));
    mqttCountLabel_->setStyleSheet("color: #4EC9B0;");
    mqttLastLabel_ = new QLabel(QStringLiteral("Latest: --"), mqttGroup);
    mqttLastLabel_->setStyleSheet("color: #888;");
    mqttLayout->addWidget(mqttCountLabel_);
    mqttLayout->addWidget(mqttLastLabel_);
    topLayout->addWidget(mqttGroup);

    auto *serialGroup = new QGroupBox(QStringLiteral("Serial Errors"), this);
    auto *serialLayout = new QVBoxLayout(serialGroup);
    crcLabel_ = new QLabel(QStringLiteral("CRC Errors: 0"), serialGroup);
    crcLabel_->setFont(QFont("Consolas", 13));
    dropLabel_ = new QLabel(QStringLiteral("Drops: 0"), serialGroup);
    dropLabel_->setFont(QFont("Consolas", 13));
    frameCountLabel_ = new QLabel(QStringLiteral("Total Frames: 0"), serialGroup);
    frameCountLabel_->setStyleSheet("color: #888;");
    serialLayout->addWidget(crcLabel_);
    serialLayout->addWidget(dropLabel_);
    serialLayout->addWidget(frameCountLabel_);
    topLayout->addWidget(serialGroup);

    auto *modelGroup = new QGroupBox(QStringLiteral("Vision Model"), this);
    auto *modelLayout = new QVBoxLayout(modelGroup);
    modelStatusLabel_ = new QLabel(QStringLiteral("Status: Unloaded"), modelGroup);
    modelStatusLabel_->setFont(QFont("Consolas", 12));
    modelFpsLabel_ = new QLabel(QStringLiteral("Inference FPS: 0.0"), modelGroup);
    modelFpsLabel_->setStyleSheet("color: #888;");
    modelErrorLabel_ = new QLabel(QStringLiteral("Error: None"), modelGroup);
    modelErrorLabel_->setStyleSheet("color: #888;");
    modelLayout->addWidget(modelStatusLabel_);
    modelLayout->addWidget(modelFpsLabel_);
    modelLayout->addWidget(modelErrorLabel_);
    topLayout->addWidget(modelGroup);

    auto *sysGroup = new QGroupBox(QStringLiteral("System Resources"), this);
    auto *sysLayout = new QVBoxLayout(sysGroup);
    cpuLabel_ = new QLabel(QStringLiteral("CPU: 0%"), sysGroup);
    cpuLabel_->setFont(QFont("Consolas", 13));
    cpuLabel_->setStyleSheet("color: #569CD6;");
    memLabel_ = new QLabel(QStringLiteral("Memory: 0%"), sysGroup);
    memLabel_->setFont(QFont("Consolas", 13));
    memLabel_->setStyleSheet("color: #569CD6;");
    uptimeLabel_ = new QLabel(QStringLiteral("Uptime: 00:00:00"), sysGroup);
    uptimeLabel_->setStyleSheet("color: #888;");
    sysLayout->addWidget(cpuLabel_);
    sysLayout->addWidget(memLabel_);
    sysLayout->addWidget(uptimeLabel_);
    topLayout->addWidget(sysGroup);

    layout->addLayout(topLayout);

    auto *filterLayout = new QHBoxLayout();
    filterLayout->setContentsMargins(4, 4, 4, 4);
    auto *filterLabel = new QLabel(QStringLiteral("Log Filter:"), this);
    filterLabel->setStyleSheet("color: #888; font-size: 11px;");
    filterLayout->addWidget(filterLabel);
    filterCombo_ = new QComboBox(this);
    filterCombo_->addItems({
        QStringLiteral("ALL"),
        QStringLiteral("RF"),
        QStringLiteral("VISION"),
        QStringLiteral("MQTT"),
        QStringLiteral("SYSTEM")
    });
    filterLayout->addWidget(filterCombo_);

    clearButton_ = new QPushButton(QStringLiteral("Clear Logs"), this);
    QObject::connect(clearButton_, &QPushButton::clicked, this, [this]() {
        if (backend_ != nullptr) {
            backend_->clearLogs();
        }
    });
    filterLayout->addWidget(clearButton_);
    filterLayout->addStretch();
    layout->addLayout(filterLayout);

    auto *logGroup = new QGroupBox(QStringLiteral("System Log"), this);
    auto *logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(4, 4, 4, 4);
    logText_ = new QPlainTextEdit(logGroup);
    logText_->setReadOnly(true);
    logText_->setFont(QFont("Consolas", 9));
    logText_->setMaximumBlockCount(kMaxLogs);
    logLayout->addWidget(logText_);
    layout->addWidget(logGroup, 1);
}

void SystemLogPage::refresh() {
    if (backend_ == nullptr) {
        return;
    }

    const QString selected = filterCombo_->currentText();
    const QString sourceFilter = (selected == QStringLiteral("ALL")) ? QString() : selected;

    const SystemStats stats = backend_->snapshotSystemStats();
    const RFSnapshot rf = backend_->snapshotRF();
    const VisionSnapshot vision = backend_->snapshotVisionState();

        cpuLabel_->setText(QString("CPU (System): %1%").arg(QString::number(stats.cpuPercent, 'f', 1)));
        memLabel_->setText(QString("Memory (System): %1% | Process: %2 MB")
                          .arg(QString::number(stats.memoryPercent, 'f', 1),
                              QString::number(stats.memoryUsedMB, 'f', 0)));

    const qint64 uptime = stats.uptimeSec;
    const qint64 hour = uptime / 3600;
    const qint64 minute = (uptime % 3600) / 60;
    const qint64 second = uptime % 60;
    uptimeLabel_->setText(QString("Uptime: %1:%2:%3")
                              .arg(hour, 2, 10, QLatin1Char('0'))
                              .arg(minute, 2, 10, QLatin1Char('0'))
                              .arg(second, 2, 10, QLatin1Char('0')));

    crcLabel_->setText(QString("CRC Errors: %1").arg(rf.crcErrors));
    crcLabel_->setStyleSheet(rf.crcErrors > 0 ? "color: #d9534f; font-size: 13px;" : "font-size: 13px;");
    dropLabel_->setText(QString("Drops: %1").arg(rf.dropCount));
    frameCountLabel_->setText(QString("Total Frames: %1").arg(rf.frameCount));

    modelStatusLabel_->setText(QString("Status: %1").arg(vision.modelLoaded ? QStringLiteral("Loaded") : QStringLiteral("Unloaded")));
    modelStatusLabel_->setStyleSheet(vision.modelLoaded ? "color: #4EC9B0;" : "color: #d9534f;");
    modelFpsLabel_->setText(QString("Inference FPS: %1").arg(QString::number(vision.fps, 'f', 1)));
    modelErrorLabel_->setText(QString("Error: %1").arg(vision.errorMsg.isEmpty() ? QStringLiteral("None") : vision.errorMsg));

    mqttCountLabel_->setText(QString("Publish Count: %1").arg(stats.mqttCount));
    const QVector<LogEntry> mqttLogs = backend_->queryLogs(QStringLiteral("MQTT"), 1);
    if (!mqttLogs.isEmpty()) {
        mqttLastLabel_->setText(QString("Latest: %1").arg(mqttLogs.first().message.left(70)));
    } else {
        mqttLastLabel_->setText(QStringLiteral("Latest: --"));
    }

    const QVector<LogEntry> logs = backend_->queryLogs(sourceFilter, 200);
    QStringList lines;
    lines.reserve(logs.size());
    for (int i = logs.size() - 1; i >= 0; --i) {
        const LogEntry &entry = logs.at(i);
        lines << QString("[%1] [%2] [%3] %4")
                     .arg(entry.timestamp.toString("HH:mm:ss"),
                          entry.source.leftJustified(6, ' ', true),
                          entry.level.leftJustified(5, ' ', true),
                          entry.message);
    }

    const QString newText = lines.join('\n');
    QScrollBar *bar = logText_->verticalScrollBar();
    const int oldValue = (bar == nullptr) ? 0 : bar->value();
    const int oldMax = (bar == nullptr) ? 0 : bar->maximum();
    const bool followTail = (bar == nullptr) ? true : ((oldMax - oldValue) <= 2);

    if (logText_->toPlainText() != newText) {
        logText_->setPlainText(newText);
        if (bar != nullptr) {
            if (followTail) {
                bar->setValue(bar->maximum());
            } else if (oldMax > 0) {
                const double ratio = static_cast<double>(oldValue) / static_cast<double>(oldMax);
                bar->setValue(static_cast<int>(ratio * static_cast<double>(bar->maximum())));
            } else {
                bar->setValue(oldValue);
            }
        }
    }
}

}  // namespace dashboard
