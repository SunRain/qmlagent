// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QMLAGENTPATHS_H
#define QMLAGENTPATHS_H

#include <QtCore/qfiledevice.h>
#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>

// Security-sensitive path handling shared by qmlagent-launcher and the
// qmlagentctl/qmlagent-mcp client binary. All private-directory creation,
// permission, containment, and well-known-location logic lives here so a fix
// cannot land in one binary and silently miss the other.

QString qmlAgentDataRoot();
QString qmlAgentTempRoot();
QString launcherExitDir();
QString mailboxRepliesRoot();
QStringList globalLauncherRegistryDirs();
QFileDevice::Permissions privateDirPermissions();
QFileDevice::Permissions privateFilePermissions();
bool ensurePrivateDirectory(const QString &path, QString *error = nullptr);
bool pathIsInsideDirectory(const QString &path, const QString &directory);

#endif // QMLAGENTPATHS_H
