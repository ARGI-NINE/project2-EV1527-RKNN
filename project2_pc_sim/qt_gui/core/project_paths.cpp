#include "project_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace {

bool looksLikeProjectRoot(const QDir &dir) {
    return dir.exists(QStringLiteral("README.md")) &&
        dir.exists(QStringLiteral("qt_gui")) &&
        dir.exists(QStringLiteral("python")) &&
        dir.exists(QStringLiteral("docs"));
}

QString findProjectRootFrom(const QString &startPath) {
    QFileInfo startInfo(startPath);
    QDir dir = startInfo.isDir() ? QDir(startInfo.absoluteFilePath()) : startInfo.absoluteDir();
    while (dir.exists()) {
        if (looksLikeProjectRoot(dir)) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QString();
}

}  // namespace

namespace dashboard {

QString normalizeExistingFilePath(const QString &path) {
    if (path.trimmed().isEmpty()) {
        return QString();
    }

    const QFileInfo info(path);
    if (info.exists() && info.isFile()) {
        return info.absoluteFilePath();
    }
    return QString();
}

QString projectRootPath() {
    static const QString root = []() {
#ifdef PROJECT2_PC_SIM_ROOT
        const QString configuredRoot = findProjectRootFrom(QString::fromUtf8(PROJECT2_PC_SIM_ROOT));
        if (!configuredRoot.isEmpty()) {
            return configuredRoot;
        }
#endif
        const QString appRoot = findProjectRootFrom(QCoreApplication::applicationDirPath());
        if (!appRoot.isEmpty()) {
            return appRoot;
        }
        return findProjectRootFrom(QDir::currentPath());
    }();
    return root;
}

QString resolveProjectFilePath(const QString &relativePath) {
    const QString root = projectRootPath();
    if (root.isEmpty()) {
        return QString();
    }
    return normalizeExistingFilePath(QDir(root).filePath(relativePath));
}

QString resolveInputFilePath(const QString &path) {
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const QString directPath = normalizeExistingFilePath(trimmed);
    if (!directPath.isEmpty()) {
        return directPath;
    }

    const QString cwdPath = normalizeExistingFilePath(QDir(QDir::currentPath()).filePath(trimmed));
    if (!cwdPath.isEmpty()) {
        return cwdPath;
    }

    const QString projectPath = resolveProjectFilePath(trimmed);
    if (!projectPath.isEmpty()) {
        return projectPath;
    }

    return normalizeExistingFilePath(QDir(QCoreApplication::applicationDirPath()).filePath(trimmed));
}

}  // namespace dashboard
