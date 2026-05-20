#pragma once

#include "app_options.h"
#include "dashboard_backend.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

namespace dashboard {

class RFGatewayClient {
public:
    RFGatewayClient(DashboardBackend *backend, const AppOptions &options, QObject *context);
    ~RFGatewayClient();

    void start();
    void stop();

private:
    void startGatewayProcess();
    void stopGatewayProcess();
    bool prepareRealtimeTimeline();
    void startGatewayWithRealtimeInput(const QString &gatewayPath);

    void onGatewayStdout();
    void onGatewayStderr();
    void onGatewayFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onReplayStdout();
    void onReplayStderr();
    void onReplayFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onPrepStdout();
    void onPrepStderr();
    void onPrepFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onPrepTimeout();
    void onFirstRfTimeout();

    void drainBuffer(QByteArray *buffer, const QByteArray &chunk, const QString &source);
    void handleGatewayLine(const QString &line, const QString &source);
    bool parseGatewayEventLine(const QString &line, RFEvent *event) const;
    qint64 computeFirstRfTimeoutMs(double replaySpeed) const;

    QString resolvePythonBin() const;
    QString resolveGatewayPath() const;
    QString resolveWavInputPath() const;
    QString resolveWavToPulsesPath() const;
    QString resolveTimelineReplayPath() const;

    DashboardBackend *backend_ = nullptr;
    AppOptions options_;
    QObject *context_ = nullptr;

    QProcess gatewayProcess_;
    bool gatewayConnected_ = false;
    QByteArray gatewayStdoutBuffer_;
    QByteArray gatewayStderrBuffer_;

    QProcess replayProcess_;
    bool replayConnected_ = false;
    QByteArray replayStderrBuffer_;

    QProcess prepProcess_;
    bool prepConnected_ = false;
    QByteArray prepStdoutBuffer_;
    QByteArray prepStderrBuffer_;
    QTimer prepTimeoutTimer_;
    QString pendingGatewayPath_;

    QTimer firstRfTimer_;
    bool awaitingFirstRf_ = false;

    QString lastStartError_;

    QString runtimePulseTxtPath_;
    QString runtimePulseJsonPath_;
    int timelineFrameCount_ = 0;
    qint64 firstRfTimeoutMs_ = 15000;
    QHash<int, double> replayWavSecByIdx_;

};

}  // namespace dashboard
