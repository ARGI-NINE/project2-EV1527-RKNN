#include "dashboard_backend.h"

#include <QDateTime>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace {
#ifdef Q_OS_WIN
quint64 fileTimeToUint64(const FILETIME &ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<quint64>(u.QuadPart);
}
#endif
}

namespace dashboard {

DashboardBackend::DashboardBackend() {
    startTimer_.start();
}

void DashboardBackend::updateSerialStatus(bool online, const QString &port) {
    QMutexLocker locker(&mutex_);
    serialOnline_ = online;
    serialPort_ = port;
}

void DashboardBackend::addRFEvent(const RFEvent &event) {
    QMutexLocker locker(&mutex_);
    hasLastDecode_ = true;
    lastDecode_ = event;
    eventHistory_.prepend(event);
    if (eventHistory_.size() > kMaxHistory) {
        eventHistory_.resize(kMaxHistory);
    }
    ++frameCount_;
}

void DashboardBackend::updateWaveform(const QVector<int> &pulses) {
    QMutexLocker locker(&mutex_);
    waveform_ = pulses;
    if (waveform_.size() > kMaxWaveform) {
        waveform_.resize(kMaxWaveform);
    }
}

void DashboardBackend::incrementCrcError() {
    QMutexLocker locker(&mutex_);
    ++crcErrors_;
}

void DashboardBackend::incrementParseError() {
    QMutexLocker locker(&mutex_);
    ++parseErrors_;
}

void DashboardBackend::updateProtocolStats(int crcErrors, int parseErrors, int driverDropFrames) {
    QMutexLocker locker(&mutex_);
    if (crcErrors >= 0) {
        crcErrors_ = crcErrors;
    }
    if (parseErrors >= 0) {
        parseErrors_ = parseErrors;
    }
    if (driverDropFrames >= 0) {
        driverDropFrames_ = driverDropFrames;
    }
}

void DashboardBackend::incrementDrop() {
    QMutexLocker locker(&mutex_);
    ++dropCount_;
}

void DashboardBackend::updateVisionState(const VisionSnapshot &snapshot) {
    QMutexLocker locker(&mutex_);
    visionSnapshot_ = snapshot;
    visionSnapshot_.statusReported = true;
}

void DashboardBackend::setVisionOffline(const QString &message) {
    QMutexLocker locker(&mutex_);
    VisionSnapshot snapshot;
    snapshot.statusReported = false;
    snapshot.cameraOnline = false;
    snapshot.modelLoaded = false;
    snapshot.errorMsg = message;
    visionSnapshot_ = snapshot;
}

qint64 DashboardBackend::uptimeSec() const {
    return startTimer_.elapsed() / 1000;
}

void DashboardBackend::addLog(const QString &level, const QString &source, const QString &message) {
    QMutexLocker locker(&mutex_);
    LogEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.level = level;
    entry.source = source;
    entry.message = message;
    logs_.append(entry);

    if (logs_.size() > kMaxLogs) {
        logs_.remove(0, logs_.size() - kMaxLogs);
    }
    if (source == QStringLiteral("MQTT")) {
        ++mqttLogCount_;
    }
}

void DashboardBackend::addMqttPublishLog(const QString &topic, const QString &payload) {
    addLog(QStringLiteral("INFO"), QStringLiteral("MQTT"), QString("PUB %1: %2").arg(topic, payload));
}

void DashboardBackend::clearLogs() {
    QMutexLocker locker(&mutex_);
    logs_.clear();
    mqttLogCount_ = 0;
}

RFSnapshot DashboardBackend::snapshotRF() const {
    QMutexLocker locker(&mutex_);

    RFSnapshot snapshot;
    snapshot.serialOnline = serialOnline_;
    snapshot.serialPort = serialPort_;
    snapshot.hasLastDecode = hasLastDecode_;
    snapshot.lastDecode = lastDecode_;
    snapshot.waveform = waveform_;
    snapshot.crcErrors = crcErrors_;
    snapshot.parseErrors = parseErrors_;
    snapshot.driverDropFrames = driverDropFrames_;
    snapshot.frameCount = frameCount_;
    snapshot.dropCount = dropCount_;

    const int count = qMin(eventHistory_.size(), 50);
    snapshot.events.reserve(count);
    for (int i = 0; i < count; ++i) {
        snapshot.events.append(eventHistory_.at(i));
    }
    return snapshot;
}

VisionSnapshot DashboardBackend::snapshotVisionState() const {
    QMutexLocker locker(&mutex_);
    return visionSnapshot_;
}

QVector<LogEntry> DashboardBackend::queryLogs(const QString &sourceFilter, int limit) const {
    QMutexLocker locker(&mutex_);

    QVector<LogEntry> out;
    out.reserve(limit);

    for (int i = logs_.size() - 1; i >= 0 && out.size() < limit; --i) {
        const LogEntry &entry = logs_.at(i);
        if (sourceFilter.isEmpty() || sourceFilter == entry.source) {
            out.append(entry);
        }
    }
    return out;
}

SystemStats DashboardBackend::snapshotSystemStats() {
    QMutexLocker locker(&mutex_);

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (lastStatsUpdateMs_ == 0 || (nowMs - lastStatsUpdateMs_) >= 500) {
#ifdef Q_OS_WIN
        FILETIME idleTime;
        FILETIME kernelTime;
        FILETIME userTime;
        if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
            const quint64 idle100ns = fileTimeToUint64(idleTime);
            const quint64 kernel100ns = fileTimeToUint64(kernelTime);
            const quint64 user100ns = fileTimeToUint64(userTime);

            if (lastCpuSampleMs_ > 0 &&
                idle100ns >= lastSystemIdle100ns_ &&
                kernel100ns >= lastSystemKernel100ns_ &&
                user100ns >= lastSystemUser100ns_) {
                const quint64 deltaIdle = idle100ns - lastSystemIdle100ns_;
                const quint64 deltaKernel = kernel100ns - lastSystemKernel100ns_;
                const quint64 deltaUser = user100ns - lastSystemUser100ns_;
                const quint64 deltaTotal = deltaKernel + deltaUser;
                if (deltaTotal > 0 && deltaTotal >= deltaIdle) {
                    const double busy = static_cast<double>(deltaTotal - deltaIdle);
                    const double cpu = (busy * 100.0) / static_cast<double>(deltaTotal);
                    cpuPercent_ = qBound(0.0, cpu, 100.0);
                }
            }

            lastSystemIdle100ns_ = idle100ns;
            lastSystemKernel100ns_ = kernel100ns;
            lastSystemUser100ns_ = user100ns;
            lastCpuSampleMs_ = nowMs;
        }

        PROCESS_MEMORY_COUNTERS_EX memCounters;
        if (GetProcessMemoryInfo(
                GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memCounters),
                sizeof(memCounters))) {
            const double workingSet = static_cast<double>(memCounters.WorkingSetSize);
            memoryUsedMB_ = workingSet / (1024.0 * 1024.0);

            MEMORYSTATUSEX memStatus;
            memStatus.dwLength = sizeof(memStatus);
            if (GlobalMemoryStatusEx(&memStatus)) {
                memoryPercent_ = qBound(0.0, static_cast<double>(memStatus.dwMemoryLoad), 100.0);
            }
        }
#endif
        lastStatsUpdateMs_ = nowMs;
    }

    SystemStats stats;
    stats.cpuPercent = cpuPercent_;
    stats.memoryPercent = memoryPercent_;
    stats.memoryUsedMB = memoryUsedMB_;
    stats.uptimeSec = startTimer_.elapsed() / 1000;
    stats.mqttCount = mqttLogCount_;
    return stats;
}

}  // namespace dashboard
