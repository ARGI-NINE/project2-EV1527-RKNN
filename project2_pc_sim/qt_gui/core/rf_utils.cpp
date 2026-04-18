#include "rf_utils.h"

namespace dashboard {

uint32_t parseRawCode(const QString &text) {
    QString value = text.trimmed();
    if (value.startsWith("0x", Qt::CaseInsensitive)) {
        value = value.mid(2);
    }

    bool ok = false;
    const uint32_t code = value.toUInt(&ok, 16);
    if (!ok) {
        return 0;
    }
    return code & 0x00FFFFFFu;
}

QVector<int> buildWaveformFromRawCode(uint32_t rawCode) {
    QVector<int> pulses;
    pulses.reserve(2 + 24 * 2);

    const int tUs = 300;
    pulses.append(4 * tUs);
    pulses.append(124 * tUs);

    for (int bit = 23; bit >= 0; --bit) {
        const bool one = ((rawCode >> bit) & 0x1u) != 0u;
        if (one) {
            pulses.append(12 * tUs);
            pulses.append(4 * tUs);
        } else {
            pulses.append(4 * tUs);
            pulses.append(12 * tUs);
        }
    }

    return pulses;
}

}  // namespace dashboard
