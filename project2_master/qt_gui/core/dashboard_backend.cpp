#include "dashboard_backend.h"

#include <QDateTime>
#include <QFile>
#include <QTextStream>

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

void DashboardBackend::updateProtocolStats(int crcErrors, int parseErrors) {
    QMutexLocker locker(&mutex_);
    if (crcErrors >= 0) {
        crcErrors_ = crcErrors;
    }
    if (parseErrors >= 0) {
        parseErrors_ = parseErrors;
    }
}

void DashboardBackend::incrementDrop() {
    QMutexLocker locker(&mutex_);
    ++dropCount_;
}

void DashboardBackend::updateVisionState(const VisionSnapshot &snapshot) {
    QMutexLocker locker(&mutex_);
    visionSnapshot_ = snapshot;
}

void DashboardBackend::setVisionOffline(const QString &message) {
    QMutexLocker locker(&mutex_);
    VisionSnapshot snapshot;
    snapshot.cameraOnline = false;
    snapshot.modelLoaded = false;
    snapshot.errorMsg = message;
    visionSnapshot_ = snapshot;
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
    if (source == "MQTT") {
        ++mqttLogCount_;
    }
}

void DashboardBackend::addMqttPublishLog(const QString &topic, const QString &payload) {
    addLog("INFO", "MQTT", QString("PUB %1: %2").arg(topic, payload));
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
    if (lastStatsUpdateMs_ == 0 || (nowMs - lastStatsUpdateMs_) >= 1500) {
        /* ---- Read real CPU usage from /proc/stat ---- */
        {
            QFile procStat("/proc/stat");
            if (procStat.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QString line = QTextStream(&procStat).readLine();   /* "cpu  user nice system idle ..." */
                const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 5 && parts[0] == "cpu") {
                    qint64 user   = parts[1].toLongLong();
                    qint64 nice   = parts[2].toLongLong();
                    qint64 system = parts[3].toLongLong();
                    qint64 idle   = parts[4].toLongLong();
                    qint64 total  = user + nice + system + idle;
                    for (int k = 5; k < parts.size(); ++k)
                        total += parts[k].toLongLong();

                    if (prevCpuTotal_ > 0) {
                        qint64 dTotal = total - prevCpuTotal_;
                        qint64 dIdle  = idle  - prevCpuIdle_;
                        if (dTotal > 0) {
                            cpuPercent_ = 100.0 * (1.0 - static_cast<double>(dIdle) / static_cast<double>(dTotal));
                        }
                    }
                    prevCpuTotal_ = total;
                    prevCpuIdle_  = idle;
                }
                procStat.close();
            }
        }

        /* ---- Read real memory usage from /proc/meminfo ---- */
        {
            QFile procMem("/proc/meminfo");
            if (procMem.open(QIODevice::ReadOnly | QIODevice::Text)) {
                qint64 memTotal = 0, memAvailable = 0;
                QTextStream stream(&procMem);
                while (!stream.atEnd()) {
                    const QString line = stream.readLine();
                    if (line.startsWith("MemTotal:")) {
                        memTotal = line.split(' ', Qt::SkipEmptyParts).at(1).toLongLong();
                    } else if (line.startsWith("MemAvailable:")) {
                        memAvailable = line.split(' ', Qt::SkipEmptyParts).at(1).toLongLong();
                    }
                    if (memTotal > 0 && memAvailable > 0)
                        break;
                }
                procMem.close();

                if (memTotal > 0) {
                    qint64 used = memTotal - memAvailable;
                    memoryPercent_ = 100.0 * static_cast<double>(used) / static_cast<double>(memTotal);
                    memoryUsedMB_  = static_cast<double>(used) / 1024.0;   /* kB → MB */
                }
            }
        }

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
