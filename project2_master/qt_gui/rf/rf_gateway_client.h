#pragma once

#include "app_options.h"
#include "dashboard_backend.h"

#include <QProcess>
#include <QString>

namespace dashboard {

class RFGatewayClient {
public:
    RFGatewayClient(DashboardBackend *backend, const AppOptions &options, QObject *context);
    ~RFGatewayClient();

    void start();
    void stop();

private:
    QString resolveGatewayPath() const;
    QString resolvedRfInputPath() const;
    QStringList buildGatewayArgs() const;

    void startGateway();
    void handleGatewayLine(const QString &line);

    DashboardBackend *backend_ = nullptr;
    AppOptions options_;
    QObject *context_ = nullptr;

    QProcess *gateway_ = nullptr;
    QString gatewayBuffer_;
};

}  // namespace dashboard
