#pragma once

#include "dashboard_backend.h"

#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace dashboard {

class WaveformWidget;

class RFStatusPage : public QWidget {
public:
    explicit RFStatusPage(DashboardBackend *backend, QWidget *parent = nullptr);

private:
    void setupUi();
    void refresh();
    void showEventDetails(const RFEvent &event);
    void onHistoryRowClicked(int row);
    void clearHistorySelectionState();
    void selectHistoryRow(int row);
    static bool sameEvent(const RFEvent &lhs, const RFEvent &rhs);

    DashboardBackend *backend_ = nullptr;
    QLabel *statusDot_ = nullptr;
    QLabel *serialLabel_ = nullptr;
    QLabel *frameLabel_ = nullptr;
    WaveformWidget *waveformWidget_ = nullptr;
    QLabel *addrLabel_ = nullptr;
    QLabel *candidateWavLabel_ = nullptr;
    QLabel *confidenceLabel_ = nullptr;
    QTableWidget *historyTable_ = nullptr;
    QTimer refreshTimer_;
    QTimer historySelectionTimer_;
    QVector<RFEvent> displayedEvents_;
    bool historyPinned_ = false;
    RFEvent pinnedEvent_;
};

}  // namespace dashboard
