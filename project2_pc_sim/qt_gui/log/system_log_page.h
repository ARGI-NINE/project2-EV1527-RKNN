#pragma once

#include "dashboard_backend.h"

#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

namespace dashboard {

class SystemLogPage : public QWidget {
public:
    explicit SystemLogPage(DashboardBackend *backend, QWidget *parent = nullptr);

private:
    void setupUi();
    void refresh();

    DashboardBackend *backend_ = nullptr;

    QLabel *mqttCountLabel_ = nullptr;
    QLabel *mqttLastLabel_ = nullptr;
    QLabel *crcLabel_ = nullptr;
    QLabel *dropLabel_ = nullptr;
    QLabel *frameCountLabel_ = nullptr;
    QLabel *modelStatusLabel_ = nullptr;
    QLabel *modelFpsLabel_ = nullptr;
    QLabel *modelErrorLabel_ = nullptr;
    QLabel *cpuLabel_ = nullptr;
    QLabel *memLabel_ = nullptr;
    QLabel *uptimeLabel_ = nullptr;
    QComboBox *filterCombo_ = nullptr;
    QPushButton *clearButton_ = nullptr;
    QPlainTextEdit *logText_ = nullptr;

    QTimer timer_;
};

}  // namespace dashboard
