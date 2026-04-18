#include "system_log_page.h"

#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
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
    mqttCountLabel_ = new QLabel(QStringLiteral("上报次数: 0"), mqttGroup);
    mqttCountLabel_->setFont(QFont("Consolas", 13));
    mqttCountLabel_->setStyleSheet("color: #4EC9B0;");
    mqttLastLabel_ = new QLabel(QStringLiteral("最近: --"), mqttGroup);
    mqttLastLabel_->setStyleSheet("color: #888;");
    mqttLayout->addWidget(mqttCountLabel_);
    mqttLayout->addWidget(mqttLastLabel_);
    topLayout->addWidget(mqttGroup);

    auto *serialGroup = new QGroupBox(QStringLiteral("串口错误"), this);
    auto *serialLayout = new QVBoxLayout(serialGroup);
    crcLabel_ = new QLabel(QStringLiteral("CRC 错误: 0"), serialGroup);
    crcLabel_->setFont(QFont("Consolas", 13));
    parseLabel_ = new QLabel(QStringLiteral("解析失败: 0"), serialGroup);
    parseLabel_->setFont(QFont("Consolas", 13));
    dropLabel_ = new QLabel(QStringLiteral("丢包: 0"), serialGroup);
    dropLabel_->setFont(QFont("Consolas", 13));
    frameCountLabel_ = new QLabel(QStringLiteral("总帧数: 0"), serialGroup);
    frameCountLabel_->setStyleSheet("color: #888;");
    serialLayout->addWidget(crcLabel_);
    serialLayout->addWidget(parseLabel_);
    serialLayout->addWidget(dropLabel_);
    serialLayout->addWidget(frameCountLabel_);
    topLayout->addWidget(serialGroup);

    auto *modelGroup = new QGroupBox(QStringLiteral("视觉模型"), this);
    auto *modelLayout = new QVBoxLayout(modelGroup);
    modelStatusLabel_ = new QLabel(QStringLiteral("状态: 未加载"), modelGroup);
    modelStatusLabel_->setFont(QFont("Consolas", 12));
    modelFpsLabel_ = new QLabel(QStringLiteral("推理 FPS: 0.0"), modelGroup);
    modelFpsLabel_->setStyleSheet("color: #888;");
    modelErrorLabel_ = new QLabel(QStringLiteral("错误: 无"), modelGroup);
    modelErrorLabel_->setStyleSheet("color: #888;");
    modelLayout->addWidget(modelStatusLabel_);
    modelLayout->addWidget(modelFpsLabel_);
    modelLayout->addWidget(modelErrorLabel_);
    topLayout->addWidget(modelGroup);

    auto *sysGroup = new QGroupBox(QStringLiteral("系统资源"), this);
    auto *sysLayout = new QVBoxLayout(sysGroup);
    cpuLabel_ = new QLabel(QStringLiteral("CPU: 0%"), sysGroup);
    cpuLabel_->setFont(QFont("Consolas", 13));
    cpuLabel_->setStyleSheet("color: #569CD6;");
    memLabel_ = new QLabel(QStringLiteral("内存: 0%"), sysGroup);
    memLabel_->setFont(QFont("Consolas", 13));
    memLabel_->setStyleSheet("color: #569CD6;");
    uptimeLabel_ = new QLabel(QStringLiteral("运行: 00:00:00"), sysGroup);
    uptimeLabel_->setStyleSheet("color: #888;");
    sysLayout->addWidget(cpuLabel_);
    sysLayout->addWidget(memLabel_);
    sysLayout->addWidget(uptimeLabel_);
    topLayout->addWidget(sysGroup);

    layout->addLayout(topLayout);

    auto *filterLayout = new QHBoxLayout();
    filterLayout->setContentsMargins(4, 4, 4, 4);
    auto *filterLabel = new QLabel(QStringLiteral("日志过滤:"), this);
    filterLabel->setStyleSheet("color: #888; font-size: 11px;");
    filterLayout->addWidget(filterLabel);
    filterCombo_ = new QComboBox(this);
    filterCombo_->addItems({
        QStringLiteral("全部"),
        QStringLiteral("RF"),
        QStringLiteral("VISION"),
        QStringLiteral("MQTT"),
        QStringLiteral("SYSTEM")
    });
    filterLayout->addWidget(filterCombo_);

    clearButton_ = new QPushButton(QStringLiteral("清空日志"), this);
    QObject::connect(clearButton_, &QPushButton::clicked, this, [this]() {
        if (backend_ != nullptr) {
            backend_->clearLogs();
        }
    });
    filterLayout->addWidget(clearButton_);
    filterLayout->addStretch();
    layout->addLayout(filterLayout);

    auto *logGroup = new QGroupBox(QStringLiteral("系统日志"), this);
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
    const QString sourceFilter = (selected == QStringLiteral("全部")) ? QString() : selected;

    const SystemStats stats = backend_->snapshotSystemStats();
    const RFSnapshot rf = backend_->snapshotRF();
    const VisionSnapshot vision = backend_->snapshotVisionState();

    cpuLabel_->setText(QString("CPU: %1%").arg(QString::number(stats.cpuPercent, 'f', 1)));
    memLabel_->setText(QString("内存: %1% (%2 MB)")
                           .arg(QString::number(stats.memoryPercent, 'f', 1),
                                QString::number(stats.memoryUsedMB, 'f', 0)));

    const qint64 uptime = stats.uptimeSec;
    const qint64 hour = uptime / 3600;
    const qint64 minute = (uptime % 3600) / 60;
    const qint64 second = uptime % 60;
    uptimeLabel_->setText(QString("运行: %1:%2:%3")
                              .arg(hour, 2, 10, QLatin1Char('0'))
                              .arg(minute, 2, 10, QLatin1Char('0'))
                              .arg(second, 2, 10, QLatin1Char('0')));

    crcLabel_->setText(QString("CRC 错误: %1").arg(rf.crcErrors));
    crcLabel_->setStyleSheet(rf.crcErrors > 0 ? "color: #d9534f; font-size: 13px;" : "font-size: 13px;");
    parseLabel_->setText(QString("解析失败: %1").arg(rf.parseErrors));
    parseLabel_->setStyleSheet(rf.parseErrors > 0 ? "color: #d9534f; font-size: 13px;" : "font-size: 13px;");
    dropLabel_->setText(QString("丢包: %1").arg(rf.dropCount));
    frameCountLabel_->setText(QString("总帧数: %1").arg(rf.frameCount));

    modelStatusLabel_->setText(QString("状态: %1").arg(vision.modelLoaded ? QStringLiteral("已加载 ✓") : QStringLiteral("未加载 ✗")));
    modelStatusLabel_->setStyleSheet(vision.modelLoaded ? "color: #4EC9B0;" : "color: #d9534f;");
    modelFpsLabel_->setText(QString("推理 FPS: %1").arg(QString::number(vision.fps, 'f', 1)));
    modelErrorLabel_->setText(QString("错误: %1").arg(vision.errorMsg.isEmpty() ? QStringLiteral("无") : vision.errorMsg));

    mqttCountLabel_->setText(QString("上报次数: %1").arg(stats.mqttCount));
    const QVector<LogEntry> mqttLogs = backend_->queryLogs(QStringLiteral("MQTT"), 1);
    if (!mqttLogs.isEmpty()) {
        mqttLastLabel_->setText(QString("最近: %1").arg(mqttLogs.first().message.left(70)));
    } else {
        mqttLastLabel_->setText(QStringLiteral("最近: --"));
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
    logText_->setPlainText(lines.join('\n'));
}

}  // namespace dashboard
