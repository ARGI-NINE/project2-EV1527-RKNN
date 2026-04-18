#pragma once

#include <QString>

namespace dashboard {

struct AppOptions {
    QString rfInput;
};

inline QString defaultRFInputPath() {
    return QStringLiteral("/dev/ttyS9");
}

}  // namespace dashboard
