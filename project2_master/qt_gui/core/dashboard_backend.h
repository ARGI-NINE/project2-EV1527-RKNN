#pragma once

#include "common_types.h"

#include <QElapsedTimer>
#include <QMutex>

namespace dashboard {

class DashboardBackend {
public:
    DashboardBackend();

    void updateSerialStatus(bool online, const QString &port = QString());
    void addRFEvent(const RFEvent &event);
    void updateWaveform(const QVector<int> &pulses);
    void incrementCrcError();
    void incrementParseError();
    void updateProtocolStats(int crcErrors, int parseErrors);
    void incrementDrop();

    void updateVisionState(const VisionSnapshot &snapshot);
    void setVisionOffline(const QString &message);

    void addLog(const QString &level, const QString &source, const QString &message);
    void addMqttPublishLog(const QString &topic, const QString &payload);
    void clearLogs();

    RFSnapshot snapshotRF() const;
    VisionSnapshot snapshotVisionState() const;
    QVector<LogEntry> queryLogs(const QString &sourceFilter, int limit) const;
    SystemStats snapshotSystemStats();

private:
    mutable QMutex mutex_;

    bool serialOnline_ = false;
    QString serialPort_;
    bool hasLastDecode_ = false;
    RFEvent lastDecode_;
    QVector<int> waveform_;
    QVector<RFEvent> eventHistory_;
    QVector<LogEntry> logs_;

    int crcErrors_ = 0;
    int parseErrors_ = 0;
    int frameCount_ = 0;
    int dropCount_ = 0;
    int mqttLogCount_ = 0;

    VisionSnapshot visionSnapshot_;

    QElapsedTimer startTimer_;
    qint64 lastStatsUpdateMs_ = 0;
    qint64 prevCpuTotal_ = 0;
    qint64 prevCpuIdle_ = 0;
    double cpuPercent_ = 0.0;
    double memoryPercent_ = 0.0;
    double memoryUsedMB_ = 0.0;
};

}  // namespace dashboard
