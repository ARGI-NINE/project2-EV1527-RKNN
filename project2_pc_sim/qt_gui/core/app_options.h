#pragma once

#include <QString>

namespace dashboard {

struct AppOptions {
    QString gatewayPath;
    QString wavPath;
    bool wavLoop = false;
    double wavSpeed = 1.0;
    QString pythonBin;
    QString visionHost;
    int visionPort = 0;
};

}  // namespace dashboard
