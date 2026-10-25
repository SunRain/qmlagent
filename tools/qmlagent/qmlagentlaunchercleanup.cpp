// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qmlagentlaunchercleanup.h"
#include "qmlagentprocess.h"

#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qregularexpression.h>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

bool canonicalWithoutLinks(const QString &path, QString *error)
{
    // Check missing leaves too: canonicalFilePath() alone hides a dangling link.
    QString ancestor = path;
    while (!ancestor.isEmpty()) {
        const QFileInfo info(ancestor);
#if defined(Q_OS_UNIX)
        struct stat attributes;
        if (::lstat(QFile::encodeName(ancestor).constData(), &attributes) == -1) {
            const int errorCode = errno;
            if (errorCode != ENOENT) {
                *error = QStringLiteral("Cannot inspect cleanup path %1: %2")
                                 .arg(ancestor, QString::fromLocal8Bit(std::strerror(errorCode)));
                return false;
            }
        } else if (S_ISLNK(attributes.st_mode)) {
#else
        if (info.isSymLink()) {
#endif
            *error = QStringLiteral("Refusing symlink in cleanup path: %1").arg(ancestor);
            return false;
        }
        const QString parent = info.absolutePath();
        if (parent == ancestor)
            break;
        ancestor = parent;
    }
    return true;
}

bool runtimeContentsBelongToSession(const QString &runtime, const QString &shortId,
                                    QString *error)
{
    const QString mailboxName = QStringLiteral("control-") + shortId;
    const QDir dir(runtime);
    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::Hidden
                                                   | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        if (entry.isSymLink() || (name != mailboxName && name != QStringLiteral("d-") + shortId
                                 && name != QStringLiteral("p-") + shortId)
                || (name == mailboxName ? !entry.isDir() : entry.isDir())) {
            *error = QStringLiteral("Runtime contains an unowned entry: %1").arg(entry.filePath());
            return false;
        }
        if (name == mailboxName && !entry.isReadable()) {
            *error = QStringLiteral("Cannot inspect mailbox contents: %1").arg(entry.filePath());
            return false;
        }
    }
    if (entries.isEmpty()) {
        *error = QStringLiteral("Cannot establish session ownership of empty runtime: %1").arg(runtime);
        return false;
    }
    const QDir mailbox(dir.filePath(mailboxName));
    static const QRegularExpression messageName(QStringLiteral(
            "^(request|response)-[a-fA-F0-9-]+\\.json$|^control\\.lock$"));
    for (const QFileInfo &entry : mailbox.entryInfoList(QDir::AllEntries | QDir::Hidden
                                                       | QDir::System | QDir::NoDotAndDotDot)) {
        if (entry.isSymLink() || !entry.isFile() || !messageName.match(entry.fileName()).hasMatch()) {
            *error = QStringLiteral("Mailbox contains an unowned entry: %1").arg(entry.filePath());
            return false;
        }
    }
    return true;
}

void appendError(QJsonArray *errors, const QString &path, const QString &message)
{
    errors->append(QJsonObject{
        { QStringLiteral("path"), path },
        { QStringLiteral("error"), message },
    });
}

QStringList matchingRegistryPaths(const QJsonObject &metadata, const QStringList &registryDirs,
                                  const QString &runtime, QJsonArray *errors)
{
    QStringList dirs = registryDirs;
    dirs.append(QFileInfo(runtime).dir().filePath(QStringLiteral("launcher-sessions")));
    const QString cwd = metadata.value(QStringLiteral("launcherCwd")).toString();
    if (QDir::isAbsolutePath(cwd))
        dirs.append(QDir(cwd).filePath(QStringLiteral(".qmlagent/launcher-sessions")));
    dirs.removeDuplicates();
    QStringList paths;
    for (const QString &dir : dirs) {
        const QString path = QDir(dir).filePath(
                metadata.value(QStringLiteral("launcherSession")).toString() + QStringLiteral(".json"));
        QString error;
        if (!canonicalWithoutLinks(path, &error)) {
            appendError(errors, path, error);
            continue;
        }
        if (!QFileInfo::exists(path))
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            appendError(errors, path, file.errorString());
        } else if (QJsonDocument::fromJson(file.readAll()).object() != metadata) {
            appendError(errors, path, QStringLiteral("Registry changed or belongs to another session"));
        } else {
            paths.append(path);
        }
    }
    return paths;
}

bool moveToTrash(const QString &path, QJsonArray *moved, QJsonArray *errors)
{
    QFile file(path);
    if (!file.moveToTrash()) {
        appendError(errors, path, QStringLiteral("Could not move to Trash: %1").arg(file.errorString()));
        return false;
    }
    if (QFileInfo::exists(path) || QFileInfo(path).isSymLink()) {
        appendError(errors, path, QStringLiteral("Trash operation left the original path in place; "
                                                 "a recoverable copy may exist at %1").arg(file.fileName()));
        return false;
    }
    moved->append(QJsonObject{
        { QStringLiteral("path"), path },
        { QStringLiteral("trashPath"), file.fileName() },
    });
    return true;
}

bool runtimeOwnedByCurrentUser(const QFileInfo &runtime, QString *error)
{
#if defined(Q_OS_UNIX)
    if (runtime.ownerId() != ::geteuid()
            || QFileInfo(runtime.absolutePath()).ownerId() != ::geteuid()) {
        *error = QStringLiteral("Runtime or its QmlAgent root is not owned by this user: %1")
                         .arg(runtime.absoluteFilePath());
        return false;
    }
#else
    Q_UNUSED(runtime);
    Q_UNUSED(error);
#endif
    return true;
}

} // namespace

QString verifiedStaleLauncherReason(const QJsonObject &metadata)
{
#if defined(Q_OS_LINUX)
    const QString recordedNamespace = metadata.value(QStringLiteral("pidNamespace")).toString();
    if (recordedNamespace.isEmpty() || recordedNamespace != qmlagentClientPidNamespace()
            || !qmlagentProcessProcAvailable())
        return {};
    const qint64 pid = metadata.value(QStringLiteral("launcherPid")).toInteger(-1);
    const QString cmdline = qmlagentReadProcessCmdline(pid);
    if (!cmdline.isEmpty() && !cmdline.contains(QStringLiteral("qmlagent-launcher"))
            && qmlagentReadPidNamespace(pid) == recordedNamespace)
        return QStringLiteral("pid_reuse_verified");
    if (qmlagentLocalProcessDefinitelyAbsent(pid))
        return QStringLiteral("pid_absent_verified");
#else
    Q_UNUSED(metadata);
#endif
    return {};
}

bool verifiedLauncherRuntime(const QJsonObject &metadata, QString *runtimePath, QString *error)
{
    const QString id = metadata.value(QStringLiteral("launcherSession")).toString();
    static const QRegularExpression sessionId(QStringLiteral("^[a-zA-Z0-9-]{8,}$"));
    const QString mailbox = metadata.value(QStringLiteral("controlMailbox")).toString();
    const QFileInfo mailboxInfo(mailbox);
    const QFileInfo runtimeInfo(mailboxInfo.absolutePath());
    *runtimePath = runtimeInfo.absoluteFilePath();
    const qint64 pid = metadata.value(QStringLiteral("launcherPid")).toInteger(-1);
    if (!sessionId.match(id).hasMatch() || pid <= 0 || !QDir::isAbsolutePath(mailbox)
            || QDir::cleanPath(mailbox) != mailbox
            || mailboxInfo.fileName() != QStringLiteral("control-") + id.left(8)
            || runtimeInfo.fileName() != QStringLiteral("qmlagent-%1").arg(pid)
            || runtimeInfo.dir().dirName() != QLatin1String("QmlAgent")) {
        *error = QStringLiteral("Recorded control mailbox does not identify this session's runtime: %1")
                         .arg(mailbox);
        return false;
    }
    const QJsonObject endpoint = metadata.value(QStringLiteral("controlEndpoint")).toObject();
    if (!endpoint.isEmpty() && (endpoint.value(QStringLiteral("kind")) != QLatin1String("fileMailbox")
                               || endpoint.value(QStringLiteral("path")) != mailbox)) {
        *error = QStringLiteral("Control endpoint and mailbox disagree: %1").arg(mailbox);
        return false;
    }
    if (!canonicalWithoutLinks(mailbox, error))
        return false;
    if (!runtimeInfo.exists())
        return true;
    if (!runtimeOwnedByCurrentUser(runtimeInfo, error))
        return false;
    if (!runtimeInfo.isDir() || !runtimeInfo.isReadable()
            || !runtimeContentsBelongToSession(*runtimePath, id.left(8), error)) {
        if (error->isEmpty())
            *error = QStringLiteral("Runtime is not an observable directory: %1").arg(*runtimePath);
        return false;
    }
    return true;
}

QJsonObject cleanupStaleLauncher(const QJsonObject &metadata, const QStringList &registryDirs)
{
    QJsonArray errors;
    QJsonArray moved;
    QString runtime;
    QString error;
    const QString reason = verifiedStaleLauncherReason(metadata);
    if (reason.isEmpty())
        appendError(&errors, {}, QStringLiteral("Stale process evidence is no longer available"));
    else if (!verifiedLauncherRuntime(metadata, &runtime, &error))
        appendError(&errors, runtime, error);
    const QStringList paths = errors.isEmpty()
            ? matchingRegistryPaths(metadata, registryDirs, runtime, &errors) : QStringList{};
    // Validate the entire deletion set before touching anything. Keep registry
    // records discoverable if runtime ownership or trash support is uncertain.
    if (errors.isEmpty() && QFileInfo::exists(runtime))
        moveToTrash(runtime, &moved, &errors);
    if (errors.isEmpty()) {
        for (const QString &path : paths)
            moveToTrash(path, &moved, &errors);
    }
    return {
        { QStringLiteral("launcherSession"), metadata.value(QStringLiteral("launcherSession")) },
        { QStringLiteral("launcherPid"), metadata.value(QStringLiteral("launcherPid")) },
        { QStringLiteral("reason"), reason },
        { QStringLiteral("complete"), errors.isEmpty() },
        { QStringLiteral("moved"), moved },
        { QStringLiteral("errors"), errors },
    };
}
