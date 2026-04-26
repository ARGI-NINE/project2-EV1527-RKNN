#include "rf_status_page.h"

#include "waveform_widget.h"

#include <QFont>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace dashboard {

RFStatusPage::RFStatusPage(DashboardBackend *backend, QWidget *parent)
    : QWidget(parent),
      backend_(backend) {
    setupUi();
    QObject::connect(&refreshTimer_, &QTimer::timeout, this, [this]() { refresh(); });
    refreshTimer_.start(500);
}

void RFStatusPage::setupUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);
    layout->setContentsMargins(10, 6, 10, 6);

    auto *serialGroup = new QGroupBox(QStringLiteral("串口状态"), this);
    auto *serialLayout = new QHBoxLayout(serialGroup);
    serialLayout->setContentsMargins(12, 8, 12, 8);
    statusDot_ = new QLabel(QStringLiteral("●"), serialGroup);
    statusDot_->setStyleSheet("color: #d9534f; font-size: 20px;");
    serialLabel_ = new QLabel(QStringLiteral("离线"), serialGroup);
    serialLabel_->setFont(QFont("Consolas", 11));
    frameLabel_ = new QLabel(QStringLiteral("帧数: 0"), serialGroup);
    frameLabel_->setFont(QFont("Consolas", 10));
    frameLabel_->setStyleSheet("color: #888;");
    serialLayout->addWidget(statusDot_);
    serialLayout->addSpacing(4);
    serialLayout->addWidget(serialLabel_);
    serialLayout->addStretch();
    serialLayout->addWidget(frameLabel_);
    layout->addWidget(serialGroup);

    auto *waveGroup = new QGroupBox(QStringLiteral("实时波形预览"), this);
    auto *waveLayout = new QVBoxLayout(waveGroup);
    waveLayout->setContentsMargins(4, 4, 4, 4);
    waveformWidget_ = new WaveformWidget(waveGroup);
    waveLayout->addWidget(waveformWidget_);
    layout->addWidget(waveGroup);

    auto *decodeGroup = new QGroupBox(QStringLiteral("最近一次 EV1527 解码结果"), this);
    auto *decodeLayout = new QHBoxLayout(decodeGroup);
    decodeLayout->setContentsMargins(12, 8, 12, 8);
    addrLabel_ = new QLabel(QStringLiteral("地址: --"), decodeGroup);
    keyLabel_ = new QLabel(QStringLiteral("按键: --"), decodeGroup);
    confLabel_ = new QLabel(QStringLiteral("置信度: --"), decodeGroup);
    srcLabel_ = new QLabel(QStringLiteral("来源: --"), decodeGroup);
    seqLabel_ = new QLabel(QStringLiteral("序列号: --"), decodeGroup);
    decodeUsLabel_ = new QLabel(QStringLiteral("解码耗时: --"), decodeGroup);

    const QFont mono("Consolas", 11);
    addrLabel_->setFont(mono);
    addrLabel_->setStyleSheet("color: #4EC9B0;");
    keyLabel_->setFont(mono);
    confLabel_->setFont(mono);
    srcLabel_->setFont(mono);
    seqLabel_->setFont(mono);
    decodeUsLabel_->setFont(mono);
    decodeUsLabel_->setStyleSheet("color: #888;");

    decodeLayout->addWidget(addrLabel_);
    decodeLayout->addWidget(keyLabel_);
    decodeLayout->addWidget(confLabel_);
    decodeLayout->addWidget(srcLabel_);
    decodeLayout->addWidget(seqLabel_);
    decodeLayout->addWidget(decodeUsLabel_);
    layout->addWidget(decodeGroup);

    auto *historyGroup = new QGroupBox(QStringLiteral("RF 历史事件表"), this);
    auto *historyLayout = new QVBoxLayout(historyGroup);
    historyLayout->setContentsMargins(4, 4, 4, 4);
    historyTable_ = new QTableWidget(0, 7, historyGroup);
    historyTable_->setHorizontalHeaderLabels({
        QStringLiteral("时间"), QStringLiteral("地址"), QStringLiteral("按键"),
        QStringLiteral("置信度"), QStringLiteral("来源"), QStringLiteral("序列号"),
        QStringLiteral("解码耗时(μs)")
    });
    historyTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    historyTable_->setEditTriggers(QTableWidget::NoEditTriggers);
    historyTable_->setSelectionBehavior(QTableWidget::SelectRows);
    historyTable_->setAlternatingRowColors(true);
    historyTable_->verticalHeader()->setVisible(false);
    historyTable_->setShowGrid(false);
    historyLayout->addWidget(historyTable_);
    layout->addWidget(historyGroup, 1);
}

void RFStatusPage::refresh() {
    if (backend_ == nullptr) {
        return;
    }

    const RFSnapshot snapshot = backend_->snapshotRF();
    statusDot_->setStyleSheet(
        snapshot.serialOnline
            ? "color: #4EC9B0; font-size: 20px;"
            : "color: #d9534f; font-size: 20px;"
    );

    const QString serialText = snapshot.serialOnline
                                   ? QString("在线 (%1)").arg(
                                         snapshot.serialPort.isEmpty()
                                             ? QStringLiteral("unknown")
                                             : snapshot.serialPort
                                     )
                                   : QStringLiteral("离线");
    serialLabel_->setText(serialText);
    frameLabel_->setText(QString("帧数: %1 | CRC错误: %2 | 解析失败: %3 | 驱动丢帧: %4")
                             .arg(snapshot.frameCount)
                             .arg(snapshot.crcErrors)
                             .arg(snapshot.parseErrors)
                             .arg(snapshot.driverDropFrames));

    waveformWidget_->setPulses(snapshot.waveform);

    if (snapshot.hasLastDecode) {
        addrLabel_->setText(QString("地址: %1").arg(snapshot.lastDecode.address));
        keyLabel_->setText(QString("按键: %1").arg(snapshot.lastDecode.key));
        confLabel_->setText(QString("置信度: %1").arg(QString::number(snapshot.lastDecode.confidence, 'f', 2)));
        srcLabel_->setText(QString("来源: %1").arg(snapshot.lastDecode.source));
        seqLabel_->setText(QString("序列号: %1").arg(
            snapshot.lastDecode.frameSeq >= 0 ? QString::number(snapshot.lastDecode.frameSeq) : QStringLiteral("--")));
        decodeUsLabel_->setText(QString("解码耗时: %1 μs").arg(
            snapshot.lastDecode.decodeUs >= 0 ? QString::number(snapshot.lastDecode.decodeUs) : QStringLiteral("--")));
    }

    historyTable_->setRowCount(snapshot.events.size());
    for (int row = 0; row < snapshot.events.size(); ++row) {
        const RFEvent &event = snapshot.events.at(row);
        historyTable_->setItem(row, 0, new QTableWidgetItem(event.timestamp.toString("HH:mm:ss")));
        historyTable_->setItem(row, 1, new QTableWidgetItem(event.address));
        historyTable_->setItem(row, 2, new QTableWidgetItem(event.key));
        historyTable_->setItem(row, 3, new QTableWidgetItem(QString::number(event.confidence, 'f', 2)));
        historyTable_->setItem(row, 4, new QTableWidgetItem(event.source));
        historyTable_->setItem(row, 5, new QTableWidgetItem(
            event.frameSeq >= 0 ? QString::number(event.frameSeq) : QStringLiteral("--")));
        historyTable_->setItem(row, 6, new QTableWidgetItem(
            event.decodeUs >= 0 ? QString::number(event.decodeUs) : QStringLiteral("--")));
    }
}

}  // namespace dashboard
