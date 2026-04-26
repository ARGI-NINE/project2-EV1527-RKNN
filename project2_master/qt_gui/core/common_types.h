#pragma once

#include <QDateTime>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

namespace dashboard {

constexpr int kMaxHistory = 200;
constexpr int kMaxLogs = 500;
constexpr int kMaxWaveform = 1024;

struct RFEvent {
    QDateTime timestamp;
    QString address;
    QString key;
    double confidence = 0.0;
    QString source;
    qint64 frameSeq = -1;
    qint64 decodeUs = -1;
};

struct LogEntry {
    QDateTime timestamp;
    QString level;
    QString source;
    QString message;
};

struct RFSnapshot {
    bool serialOnline = false;
    QString serialPort;
    bool hasLastDecode = false;
    RFEvent lastDecode;
    QVector<int> waveform;
    QVector<RFEvent> events;
    int crcErrors = 0;
    int parseErrors = 0;
    int driverDropFrames = 0;
    int frameCount = 0;
    int dropCount = 0;
};

struct VisionSnapshot {
    QImage frame;
    QStringList detections;
    double fps = 0.0;
    bool statusReported = false;
    bool modelLoaded = false;
    bool cameraOnline = false;
    int frameCount = 0;
    QString errorMsg;
};

struct SystemStats {
    double cpuPercent = 0.0;
    double memoryPercent = 0.0;
    double memoryUsedMB = 0.0;
    qint64 uptimeSec = 0;
    int mqttCount = 0;
};

}  // namespace dashboard
