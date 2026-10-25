// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QMLAGENTLAUNCHERCLEANUP_H
#define QMLAGENTLAUNCHERCLEANUP_H

#include <QtCore/qjsonobject.h>
#include <QtCore/qstringlist.h>

// Return a reason only for an observable non-launcher or absent process in
// the client's own PID namespace. Unknown identity is never proof of death.
QString verifiedStaleLauncherReason(const QJsonObject &metadata);

// Validate the recorded mailbox's runtime identity, including all existing
// descendants, without inferring any path from the client's TMPDIR.
bool verifiedLauncherRuntime(const QJsonObject &metadata, QString *runtimePath,
                             QString *error);

// Revalidate stale evidence and move only matching runtime/registry artifacts
// to Trash. The report includes complete, moved paths, and any explicit errors.
QJsonObject cleanupStaleLauncher(const QJsonObject &metadata, const QStringList &registryDirs);

#endif // QMLAGENTLAUNCHERCLEANUP_H
