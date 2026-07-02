// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qmlagentpaths.h"

#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qstandardpaths.h>

QString qmlAgentDataRoot()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    return QDir(base).filePath(QStringLiteral("QmlAgent"));
}

QString qmlAgentTempRoot()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (base.isEmpty())
        base = QDir::tempPath();
    const QString canonicalBase = QFileInfo(base).canonicalFilePath();
    if (!canonicalBase.isEmpty())
        base = canonicalBase;
    return QDir(base).filePath(QStringLiteral("QmlAgent"));
}

QString launcherExitDir()
{
    return QDir(qmlAgentTempRoot()).filePath(QStringLiteral("launcher-exits"));
}

QString mailboxRepliesRoot()
{
    return QDir(qmlAgentTempRoot()).filePath(QStringLiteral("mailbox-replies"));
}

QStringList globalLauncherRegistryDirs()
{
    return {
        QDir(qmlAgentTempRoot()).filePath(QStringLiteral("launcher-sessions")),
        QDir(qmlAgentDataRoot()).filePath(QStringLiteral("launcher-sessions")),
    };
}

QFileDevice::Permissions privateDirPermissions()
{
    return QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
}

QFileDevice::Permissions privateFilePermissions()
{
    return QFileDevice::ReadOwner | QFileDevice::WriteOwner;
}

bool ensurePrivateDirectory(const QString &path, QString *error)
{
    const QFileInfo before(path);
    if (before.exists() && (before.isSymLink() || !before.isDir())) {
        if (error)
            *error = QStringLiteral("Refusing non-directory or symlink path: %1").arg(path);
        return false;
    }

    if (!QDir().mkpath(path)) {
        if (error)
            *error = QStringLiteral("Could not create directory: %1").arg(path);
        return false;
    }

    const QFileInfo after(path);
    if (after.isSymLink() || !after.isDir()) {
        if (error)
            *error = QStringLiteral("Refusing non-directory or symlink path: %1").arg(path);
        return false;
    }

    QFile::setPermissions(path, privateDirPermissions());
    return true;
}

bool pathIsInsideDirectory(const QString &path, const QString &directory)
{
    const QString canonicalDirectory = QFileInfo(directory).canonicalFilePath();
    if (canonicalDirectory.isEmpty())
        return false;

    const QFileInfo info(path);
    const QString canonicalParent = info.dir().canonicalPath();
    if (canonicalParent.isEmpty())
        return false;

    return canonicalParent == canonicalDirectory
            || canonicalParent.startsWith(canonicalDirectory + QLatin1Char('/'));
}
