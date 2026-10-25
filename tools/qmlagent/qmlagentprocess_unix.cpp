// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qmlagentprocess.h"

#include <QtCore/qbytearray.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qstring.h>

#include <limits>

#if defined(Q_OS_LINUX)
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

namespace {

#if defined(Q_OS_LINUX)
QString procFilePath(qint64 pid, const char *leaf)
{
    return QStringLiteral("/proc/%1/%2").arg(pid).arg(QLatin1String(leaf));
}
#endif

} // namespace

bool qmlagentProcessProcAvailable()
{
#if defined(Q_OS_LINUX)
    return QFileInfo::exists(QStringLiteral("/proc/self/status"));
#else
    return false;
#endif
}

bool qmlagentLocalProcessDefinitelyAbsent(qint64 pid)
{
#if defined(Q_OS_LINUX)
    if (pid <= 0 || pid > std::numeric_limits<pid_t>::max())
        return false;
    return ::kill(pid_t(pid), 0) == -1 && errno == ESRCH;
#else
    Q_UNUSED(pid);
    return false;
#endif
}

QString qmlagentReadProcessCmdline(qint64 pid)
{
#if defined(Q_OS_LINUX)
    if (pid <= 0)
        return {};
    QFile file(procFilePath(pid, "cmdline"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QByteArray cmdline = file.readAll();
    const qsizetype last = cmdline.size() - 1;
    if (last >= 0 && cmdline.at(last) == '\0')
        cmdline.truncate(last);
    cmdline.replace('\0', ' ');
    return QString::fromUtf8(cmdline).trimmed();
#else
    Q_UNUSED(pid);
    return {};
#endif
}

QString qmlagentReadPidNamespace(qint64 pid)
{
#if defined(Q_OS_LINUX)
    if (pid <= 0)
        return {};
    const QByteArray path = procFilePath(pid, "ns/pid").toLocal8Bit();
    char buffer[256];
    const ssize_t length = ::readlink(path.constData(), buffer, sizeof(buffer) - 1);
    if (length <= 0)
        return {};
    buffer[length] = '\0';
    return QString::fromLocal8Bit(buffer, int(length));
#else
    Q_UNUSED(pid);
    return {};
#endif
}

QString qmlagentClientPidNamespace()
{
    return qmlagentReadPidNamespace(qint64(QCoreApplication::applicationPid()));
}

QJsonValue qmlagentPidNamespaceValue(const QString &value)
{
    return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
}

void qmlagentInsertPidNamespace(QJsonObject &object, const QJsonValue &launcher,
                               const QString &client)
{
#if defined(Q_OS_LINUX)
    const QString launcherNamespace = launcher.toString();
    const bool known = !launcherNamespace.isEmpty() && !client.isEmpty();
    object.insert(QStringLiteral("pidNamespace"), QJsonObject{
        { QStringLiteral("launcher"), qmlagentPidNamespaceValue(launcherNamespace) },
        { QStringLiteral("client"), qmlagentPidNamespaceValue(client) },
        { QStringLiteral("sameNamespace"), known
                  ? QJsonValue(launcherNamespace == client) : QJsonValue(QJsonValue::Null) },
    });
    if (known && launcherNamespace != client) {
        object.insert(QStringLiteral("pidNamespaceHint"), QStringLiteral(
                "PIDs belong to the launcher PID namespace; do not validate them with ps "
                "from a sandboxed shell. Use mailbox RPC status instead."));
    }
#else
    Q_UNUSED(object);
    Q_UNUSED(launcher);
    Q_UNUSED(client);
#endif
}
