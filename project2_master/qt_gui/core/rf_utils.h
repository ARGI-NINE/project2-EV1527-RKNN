#pragma once

#include <QString>
#include <QVector>

#include <cstdint>

namespace dashboard {

uint32_t parseRawCode(const QString &text);
QVector<int> buildWaveformFromRawCode(uint32_t rawCode);

}  // namespace dashboard
