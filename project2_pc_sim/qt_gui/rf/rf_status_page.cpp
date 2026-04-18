#include "rf_status_page.h"

#include "rf_utils.h"
#include "waveform_widget.h"

#include <QDateTime>
#include <QFont>
#include <QGroupBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace dashboard {

namespace {

QString formatDecodeTime(qint64 decodeUs) {
    if (decodeUs < 0) {
        return QStringLiteral("--");
    }
    if (decodeUs < 1000) {
        return QStringLiteral("%1 us").arg(decodeUs);
    }
    return QStringLiteral("%1 ms").arg(QString::number(decodeUs / 1000.0, 'f', 2));
}

QString formatCandidateWavSec(double wavSec) {
    if (wavSec < 0.0) {
        return QStringLiteral("--");
    }
    return QString::number(wavSec, 'f', 6);
}

QString formatConfidence(double confidence) {
    if (confidence < 0.0) {
        return QStringLiteral("--");
    }
    return QString::number(confidence, 'f', 2);
}

}  // namespace

RFStatusPage::RFStatusPage(DashboardBackend *backend, QWidget *parent)
    : QWidget(parent),
      backend_(backend) {
    setupUi();
    QObject::connect(&refreshTimer_, &QTimer::timeout, this, [this]() { refresh(); });
    historySelectionTimer_.setSingleShot(true);
    QObject::connect(&historySelectionTimer_, &QTimer::timeout, this, [this]() {
        historyPinned_ = false;
        clearHistorySelectionState();
        refresh();
    });
    refreshTimer_.start(100);
}

void RFStatusPage::setupUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);
    layout->setContentsMargins(10, 6, 10, 6);

    auto *serialGroup = new QGroupBox(QStringLiteral("RF Link"), this);
    auto *serialLayout = new QHBoxLayout(serialGroup);
    serialLayout->setContentsMargins(12, 8, 12, 8);
    statusDot_ = new QLabel(QStringLiteral("\u25CF"), serialGroup);
    statusDot_->setStyleSheet("color: #d9534f; font-size: 20px;");
    serialLabel_ = new QLabel(QStringLiteral("Offline"), serialGroup);
    serialLabel_->setFont(QFont("Consolas", 11));
    frameLabel_ = new QLabel(QStringLiteral("Frames: 0"), serialGroup);
    frameLabel_->setFont(QFont("Consolas", 10));
    frameLabel_->setStyleSheet("color: #888;");
    serialLayout->addWidget(statusDot_);
    serialLayout->addSpacing(4);
    serialLayout->addWidget(serialLabel_);
    serialLayout->addStretch();
    serialLayout->addWidget(frameLabel_);
    layout->addWidget(serialGroup);

    auto *waveGroup = new QGroupBox(QStringLiteral("Waveform"), this);
    auto *waveLayout = new QVBoxLayout(waveGroup);
    waveLayout->setContentsMargins(4, 4, 4, 4);
    waveformWidget_ = new WaveformWidget(waveGroup);
    waveLayout->addWidget(waveformWidget_);
    layout->addWidget(waveGroup);

    auto *decodeGroup = new QGroupBox(QStringLiteral("Latest EV1527 Decode"), this);
    auto *decodeLayout = new QHBoxLayout(decodeGroup);
    decodeLayout->setContentsMargins(12, 8, 12, 8);
    addrLabel_ = new QLabel(QStringLiteral("Address: --"), decodeGroup);
    candidateWavLabel_ = new QLabel(QStringLiteral("WAV Sec: --"), decodeGroup);
    confidenceLabel_ = new QLabel(QStringLiteral("Confidence: --"), decodeGroup);
    decodeTimeLabel_ = new QLabel(QStringLiteral("Decode Time: --"), decodeGroup);

    const QFont mono("Consolas", 11);
    addrLabel_->setFont(mono);
    addrLabel_->setStyleSheet("color: #4EC9B0;");
    candidateWavLabel_->setFont(mono);
    confidenceLabel_->setFont(mono);
    decodeTimeLabel_->setFont(mono);
    decodeTimeLabel_->setStyleSheet("color: #888;");

    decodeLayout->addWidget(addrLabel_);
    decodeLayout->addWidget(candidateWavLabel_);
    decodeLayout->addWidget(confidenceLabel_);
    decodeLayout->addStretch();
    decodeLayout->addWidget(decodeTimeLabel_);
    layout->addWidget(decodeGroup);

    auto *historyGroup = new QGroupBox(QStringLiteral("RF Event History"), this);
    auto *historyLayout = new QVBoxLayout(historyGroup);
    historyLayout->setContentsMargins(4, 4, 4, 4);
    historyTable_ = new QTableWidget(0, 4, historyGroup);
    historyTable_->setHorizontalHeaderLabels({
        QStringLiteral("Address"),
        QStringLiteral("WAV Sec"),
        QStringLiteral("Confidence"),
        QStringLiteral("Decode Time")
    });
    historyTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    historyTable_->setEditTriggers(QTableWidget::NoEditTriggers);
    historyTable_->setSelectionBehavior(QTableWidget::SelectRows);
    historyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    historyTable_->setAlternatingRowColors(true);
    historyTable_->verticalHeader()->setVisible(false);
    historyTable_->setShowGrid(false);
    QObject::connect(historyTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        onHistoryRowClicked(row);
    });
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
                                   ? QString("Online (%1)").arg(
                                         snapshot.serialPort.isEmpty()
                                             ? QStringLiteral("unknown")
                                             : snapshot.serialPort
                                     )
                                   : QStringLiteral("Offline");
    serialLabel_->setText(serialText);
    frameLabel_->setText(QString("Frames: %1 | CRC Errors: %2")
                             .arg(snapshot.frameCount)
                             .arg(snapshot.crcErrors));

    displayedEvents_ = snapshot.events;
    historyTable_->setRowCount(snapshot.events.size());
    for (int row = 0; row < snapshot.events.size(); ++row) {
        const RFEvent &event = snapshot.events.at(row);
        historyTable_->setItem(row, 0, new QTableWidgetItem(event.address));
        historyTable_->setItem(row, 1, new QTableWidgetItem(formatCandidateWavSec(event.candidateWavSec)));
        historyTable_->setItem(row, 2, new QTableWidgetItem(formatConfidence(event.confidence)));
        historyTable_->setItem(row, 3, new QTableWidgetItem(formatDecodeTime(event.decodeUs)));
    }

    if (historyPinned_) {
        int pinnedRow = -1;
        for (int row = 0; row < displayedEvents_.size(); ++row) {
            if (sameEvent(displayedEvents_.at(row), pinnedEvent_)) {
                pinnedRow = row;
                break;
            }
        }

        if (pinnedRow >= 0) {
            const RFEvent selected = displayedEvents_.at(pinnedRow);
            const QVector<int> pulses = buildWaveformFromRawCode(parseRawCode(selected.address));
            waveformWidget_->setPulses(pulses.isEmpty() ? snapshot.waveform : pulses);
            showEventDetails(selected);
            selectHistoryRow(pinnedRow);
            return;
        }

        historyPinned_ = false;
        clearHistorySelectionState();
    }

    clearHistorySelectionState();
    waveformWidget_->setPulses(snapshot.waveform);
    if (snapshot.hasLastDecode) {
        showEventDetails(snapshot.lastDecode);
    } else {
        addrLabel_->setText(QStringLiteral("Address: --"));
        candidateWavLabel_->setText(QStringLiteral("WAV Sec: --"));
        confidenceLabel_->setText(QStringLiteral("Confidence: --"));
        decodeTimeLabel_->setText(QStringLiteral("Decode Time: --"));
    }
}

void RFStatusPage::showEventDetails(const RFEvent &event) {
    addrLabel_->setText(QString("Address: %1").arg(event.address));
    candidateWavLabel_->setText(QString("WAV Sec: %1").arg(formatCandidateWavSec(event.candidateWavSec)));
    confidenceLabel_->setText(QString("Confidence: %1").arg(formatConfidence(event.confidence)));
    decodeTimeLabel_->setText(QString("Decode Time: %1").arg(formatDecodeTime(event.decodeUs)));
}

void RFStatusPage::onHistoryRowClicked(int row) {
    if (row < 0 || row >= displayedEvents_.size()) {
        return;
    }
    pinnedEvent_ = displayedEvents_.at(row);
    historyPinned_ = true;
    historySelectionTimer_.start(5000);
    refresh();
}

void RFStatusPage::clearHistorySelectionState() {
    if (historyTable_ == nullptr) {
        return;
    }

    const QSignalBlocker tableBlocker(historyTable_);
    QItemSelectionModel *selectionModel = historyTable_->selectionModel();
    const QSignalBlocker selectionBlocker(selectionModel);

    historyTable_->clearSelection();
    if (selectionModel != nullptr) {
        selectionModel->clear();
        selectionModel->clearSelection();
        selectionModel->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
    }
    historyTable_->setCurrentItem(nullptr, QItemSelectionModel::NoUpdate);
    historyTable_->viewport()->update();
}

void RFStatusPage::selectHistoryRow(int row) {
    if (historyTable_ == nullptr || row < 0 || row >= historyTable_->rowCount()) {
        return;
    }

    const QSignalBlocker tableBlocker(historyTable_);
    QItemSelectionModel *selectionModel = historyTable_->selectionModel();
    const QModelIndex firstColumnIndex = historyTable_->model()->index(row, 0);
    if (selectionModel != nullptr) {
        const QSignalBlocker selectionBlocker(selectionModel);
        selectionModel->setCurrentIndex(firstColumnIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        selectionModel->select(firstColumnIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    historyTable_->setCurrentCell(row, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    historyTable_->viewport()->update();
}

bool RFStatusPage::sameEvent(const RFEvent &lhs, const RFEvent &rhs) {
    if (lhs.frameSeq > 0 && rhs.frameSeq > 0) {
        return lhs.frameSeq == rhs.frameSeq;
    }
    return lhs.timestamp == rhs.timestamp &&
           lhs.address == rhs.address &&
           lhs.decodeUs == rhs.decodeUs;
}

}  // namespace dashboard
