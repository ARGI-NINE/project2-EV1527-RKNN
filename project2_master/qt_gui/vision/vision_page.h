#pragma once

#include "dashboard_backend.h"

#include <QLabel>
#include <QListWidget>
#include <QTimer>
#include <QWidget>

namespace dashboard {

class VisionPage : public QWidget {
public:
    explicit VisionPage(DashboardBackend *backend, QWidget *parent = nullptr);

private:
    void setupUi();
    void refresh();

    DashboardBackend *backend_ = nullptr;
    QLabel *videoLabel_ = nullptr;
    QLabel *fpsLabel_ = nullptr;
    QLabel *cameraLabel_ = nullptr;
    QLabel *modelLabel_ = nullptr;
    QLabel *frameCountLabel_ = nullptr;
    QListWidget *detList_ = nullptr;
    QTimer timer_;
};

}  // namespace dashboard
