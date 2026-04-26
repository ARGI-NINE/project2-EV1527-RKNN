#pragma once

#include <QString>

namespace dashboard {

struct AppOptions {
    QString rfInput;
    QString visionDevice;
};

inline QString defaultRFInputPath() {
    return QStringLiteral("/dev/rf433");
}

inline QString defaultVisionDevicePath() {
    return QStringLiteral("/dev/video9");
}

inline bool isAllowedVisionDevicePath(const QString &path) {
    return path.startsWith(QStringLiteral("/dev/video"));
}

}  // namespace dashboard
