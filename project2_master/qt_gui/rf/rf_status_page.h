#pragma once

#include "dashboard_backend.h"

#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

namespace dashboard {

class WaveformWidget;

class RFStatusPage : public QWidget {
public:
    explicit RFStatusPage(DashboardBackend *backend, QWidget *parent = nullptr);

private:
    void setupUi();
    void refresh();

    DashboardBackend *backend_ = nullptr;
    QLabel *statusDot_ = nullptr;
    QLabel *serialLabel_ = nullptr;
    QLabel *frameLabel_ = nullptr;
    WaveformWidget *waveformWidget_ = nullptr;
    QLabel *addrLabel_ = nullptr;
    QLabel *keyLabel_ = nullptr;
    QLabel *confLabel_ = nullptr;
    QLabel *srcLabel_ = nullptr;
    QLabel *seqLabel_ = nullptr;
    QLabel *decodeUsLabel_ = nullptr;
    QTableWidget *historyTable_ = nullptr;
    QTimer refreshTimer_;
};

}  // namespace dashboard
