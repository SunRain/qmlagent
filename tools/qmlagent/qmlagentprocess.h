// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QMLAGENTPROCESS_H
#define QMLAGENTPROCESS_H

#include <QtCore/qjsonobject.h>
#include <QtCore/qstring.h>

// Linux-only /proc helpers shared by qmlagent-launcher and the clients. The
// functions are safe on every Unix platform: without /proc they report
// "unknown" instead of guessing a process identity.
bool qmlagentProcessProcAvailable();

// Returns true only for ESRCH in the client's PID namespace. Callers must
// first establish that a recorded PID belongs to that same namespace.
bool qmlagentLocalProcessDefinitelyAbsent(qint64 pid);

// Returns the NUL-separated /proc/<pid>/cmdline joined with spaces. An empty
// result means the command line could not be observed; it never means that
// the process does not exist.
QString qmlagentReadProcessCmdline(qint64 pid);

// Returns the symlink target of /proc/<pid>/ns/pid (for example
// "pid:[4026531836]"), or an empty string when it cannot be observed.
QString qmlagentReadPidNamespace(qint64 pid);

QString qmlagentClientPidNamespace();

// Normalize an unobservable namespace to JSON null, including in registry metadata.
QJsonValue qmlagentPidNamespaceValue(const QString &value);

// Add Linux-only namespace diagnostics. Unknown or unselected launchers have
// a null comparison, not a false cross-namespace diagnosis. Other platforms omit it.
void qmlagentInsertPidNamespace(QJsonObject &object, const QJsonValue &launcher,
                               const QString &client);

#endif // QMLAGENTPROCESS_H
