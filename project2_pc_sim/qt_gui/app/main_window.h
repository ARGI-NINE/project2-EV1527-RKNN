#pragma once

#include "app_options.h"
#include "dashboard_backend.h"
#include "rf_gateway_client.h"

#include <QLabel>
#include <QMainWindow>
#include <QTimer>

namespace dashboard {

class RFStatusPage;
class VisionPage;
class SystemLogPage;

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(const AppOptions &options, QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void setupUi();
    void setupRuntime();
    void updateStatusBar();

    AppOptions options_;
    DashboardBackend backend_;
    RFGatewayClient rfClient_;

    RFStatusPage *rfPage_ = nullptr;
    VisionPage *visionPage_ = nullptr;
    SystemLogPage *logPage_ = nullptr;

    QLabel *statusLabel_ = nullptr;
    QTimer statusTimer_;
};

}  // namespace dashboard
