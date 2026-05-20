#pragma once

#include "app_options.h"
#include "dashboard_backend.h"

#include <QByteArray>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QStringList>

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
    void drainProtocolBuffer(const QByteArray &chunk);
    void flushProtocolBuffer();
    void appendDiagnosticChunk(const QByteArray &chunk);
    QString takeBufferedDiagnostics();
    void handleProtocolLine(const QString &line);
    bool parseRFEventPayload(const QJsonObject &payload, RFEvent *event, QVector<int> *pulses) const;
    bool parseProtocolEnvelope(
        const QString &line,
        QString *type,
        QString *topic,
        bool *mqttPublished,
        QJsonObject *payload
    ) const;

    DashboardBackend *backend_ = nullptr;
    AppOptions options_;
    QObject *context_ = nullptr;

    QProcess *gateway_ = nullptr;
    QByteArray gatewayStdoutBuffer_;
    QByteArray gatewayStderrBuffer_;
};

}  // namespace dashboard
