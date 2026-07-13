#pragma once

#include <QFileInfo>
#include <QString>

namespace dashboard {

struct AppOptions {
    QString rfInput;
    QString visionDevice;
    bool visionRtspEnabled = true;
    QString visionRtspUrl;
};

inline QString defaultRFInputPath() {
    return QStringLiteral("/dev/rf433");
}

inline QString defaultVisionDevicePath() {
    return QStringLiteral("/dev/video9");
}

inline QString defaultVisionRtspUrl() {
    return QStringLiteral("rtsp://192.168.30.26:8554/rk3568-001/cam0");
}

inline bool isAllowedVisionDevicePath(const QString &path) {
    return path.startsWith(QStringLiteral("/dev/video"));
}

inline bool isReadableVisionInputFile(const QString &path) {
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.isReadable();
}

inline bool isAllowedVisionInputPath(const QString &path) {
    if (isAllowedVisionDevicePath(path)) {
        return true;
    }
    return isReadableVisionInputFile(path);
}

}  // namespace dashboard
