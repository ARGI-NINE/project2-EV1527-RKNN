#pragma once

#include <QString>

namespace dashboard {

QString normalizeExistingFilePath(const QString &path);
QString projectRootPath();
QString resolveProjectFilePath(const QString &relativePath);
QString resolveInputFilePath(const QString &path);

}  // namespace dashboard
