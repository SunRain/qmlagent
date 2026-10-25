// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qmlagentlauncher.h"
#include "qmlagentlaunchercleanup.h"
#include "qmlagentpaths.h"
#include "qmlagentprocess.h"

#include <QtCore/qcoreapplication.h>
#include <QtCore/qdeadlinetimer.h>
#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qlockfile.h>
#include <QtCore/qsavefile.h>
#include <QtCore/qset.h>
#include <QtCore/qthread.h>
#include <QtCore/quuid.h>

static QStringList launcherRegistryDirs()
{
    QStringList dirs;
    const QString workspaceStateDir = QDir::current().filePath(QStringLiteral(".qmlagent"));
    if (QFileInfo::exists(workspaceStateDir))
        dirs.append(QDir(workspaceStateDir).filePath(QStringLiteral("launcher-sessions")));

    for (const QString &globalDir : globalLauncherRegistryDirs()) {
        if (!dirs.contains(globalDir))
            dirs.append(globalDir);
    }
    return dirs;
}

static QJsonObject readLauncherMailboxResponse(const QString &responsePath, int timeoutMs,
                                               QString *error)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired()) {
        if (QFileInfo(responsePath).isSymLink()) {
            *error = QStringLiteral("Refusing symlinked qmlagent-launcher mailbox response: %1")
                             .arg(responsePath);
            return {};
        }
        QFile responseFile(responsePath);
        if (responseFile.open(QIODevice::ReadOnly)) {
            const QByteArray payload = responseFile.readAll().trimmed();
            responseFile.close();
            QFile::remove(responsePath);

            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                *error = QStringLiteral("Invalid qmlagent-launcher mailbox response.");
                return {};
            }
            return document.object();
        }

        QThread::msleep(10);
    }

    *error = QStringLiteral("Timed out waiting for qmlagent-launcher mailbox response.");
    return {};
}

static bool publishLauncherRequest(const QString &mailboxDir, const QString &method,
                                   const QJsonObject &params, QString *responsePath, QString *error)
{
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString requestPath = QDir(mailboxDir).filePath(
            QStringLiteral("request-%1.json").arg(token));
    const QString replyDir = QDir(mailboxRepliesRoot())
            .filePath(QString::number(qint64(QCoreApplication::applicationPid())));
    if (!ensurePrivateDirectory(qmlAgentTempRoot(), error))
        return false;
    if (!ensurePrivateDirectory(mailboxRepliesRoot(), error))
        return false;
    if (!ensurePrivateDirectory(replyDir, error))
        return false;
    *responsePath = QDir(replyDir).filePath(
            QStringLiteral("response-%1.json").arg(token));

    if (!pathIsInsideDirectory(requestPath, mailboxDir)
            || !pathIsInsideDirectory(*responsePath, replyDir)) {
        *error = QStringLiteral("Refusing qmlagent-launcher mailbox path outside private directory.");
        return false;
    }

    QSaveFile requestFile(requestPath);
    if (!requestFile.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("Could not write qmlagent-launcher mailbox request: %1")
                         .arg(requestPath);
        return false;
    }
    QJsonObject request{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), 1 },
        { QStringLiteral("method"), method },
        { QStringLiteral("params"), params },
    };
    request.insert(QStringLiteral("replyTo"), *responsePath);
    requestFile.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
    requestFile.write("\n");
    if (!requestFile.commit()) {
        *error = QStringLiteral("Could not publish qmlagent-launcher mailbox request: %1")
                         .arg(requestPath);
        return false;
    }
    QFile::setPermissions(requestPath, privateFilePermissions());
    return true;
}

static QJsonObject sendLauncherMailboxControlRequest(const QString &mailboxDir, int timeoutMs,
                                                     const QString &method,
                                                     const QJsonObject &params, QString *error)
{
    // Neither an invisible PID nor an absent mailbox proves that a launcher
    // in another namespace exited. Only probe existing control directories.
    const QFileInfo mailboxInfo(mailboxDir);
    if (!mailboxInfo.exists() || mailboxInfo.isSymLink() || !mailboxInfo.isDir()) {
        *error = QStringLiteral("qmlagent-launcher control mailbox is unavailable: %1")
                         .arg(mailboxDir);
        return {};
    }

    QLockFile controlLock(QDir(mailboxDir).filePath(QStringLiteral("control.lock")));
    controlLock.setStaleLockTime(5000);
    const QDeadlineTimer deadline(timeoutMs);
    if (!controlLock.tryLock(qMax(0, timeoutMs))) {
        *error = QStringLiteral("Timed out acquiring qmlagent-launcher control lease for %1. "
                                "Another agent process may be driving this launcher session.")
                         .arg(mailboxDir);
        return {};
    }

    QString responsePath;
    if (!publishLauncherRequest(mailboxDir, method, params, &responsePath, error))
        return {};

    if (method == QLatin1String("Session.stop"))
        controlLock.unlock();

    return readLauncherMailboxResponse(responsePath, int(deadline.remainingTime()), error);
}

QJsonObject sendLauncherControlRequest(const QJsonObject &metadata, int timeoutMs,
                                      const QString &method, const QJsonObject &params,
                                      QString *error)
{
    error->clear();
    const QString mailboxDir = metadata.value(QStringLiteral("controlMailbox")).toString();
    if (!mailboxDir.isEmpty())
        return sendLauncherMailboxControlRequest(mailboxDir, timeoutMs, method, params, error);

    *error = QStringLiteral("qmlagent-launcher session has no supported control mailbox. "
                            "Restart the session with the current qmlagent-launcher.");
    return {};
}

static bool hasLauncherControlEndpoint(const QJsonObject &metadata)
{
    return !metadata.value(QStringLiteral("controlMailbox")).toString().isEmpty();
}

static QJsonObject launcherControlEndpointSummary(const QJsonObject &metadata)
{
    const QString mailboxDir = metadata.value(QStringLiteral("controlMailbox")).toString();
    if (!mailboxDir.isEmpty()) {
        return {
            { QStringLiteral("kind"), QStringLiteral("fileMailbox") },
            { QStringLiteral("path"), mailboxDir },
        };
    }

    return {
        { QStringLiteral("kind"), QStringLiteral("none") },
    };
}

static QJsonObject launcherControlEndpointSummary(const LauncherSession &session)
{
    QJsonObject endpoint = launcherControlEndpointSummary(session.metadata);
    const QJsonObject statusEndpoint = session.status.value(QStringLiteral("controlEndpoint")).toObject();
    if (!statusEndpoint.isEmpty())
        endpoint = statusEndpoint;
    return endpoint;
}

static LauncherSession probeLauncherSession(const QJsonObject &metadata, int timeoutMs)
{
    const QString staleReason = verifiedStaleLauncherReason(metadata);
    QString runtime;
    QString runtimeError;
    // Only a same-namespace stale PID with a structurally owned runtime can
    // skip the normal one-second wait. Still allow an already responsive
    // mailbox to overrule registry evidence; malformed/foreign records always
    // get the normal probe (including reachable mailboxes with rewritten PIDs).
    const bool fastProbe = !staleReason.isEmpty()
            && verifiedLauncherRuntime(metadata, &runtime, &runtimeError);
    QString error;
    const QJsonObject response = sendLauncherControlRequest(
            metadata, qMin(timeoutMs, fastProbe ? 20 : 1000),
            QStringLiteral("Session.status"), {}, &error);
    QJsonObject status;
    if (error.isEmpty()) {
        status = response.value(QStringLiteral("result")).toObject();
        status.insert(QStringLiteral("controlReachable"), true);
    } else {
        status = {
            { QStringLiteral("controlReachable"), false },
            { QStringLiteral("controlError"), error },
            { QStringLiteral("sessionType"), metadata.value(QStringLiteral("sessionType")) },
            { QStringLiteral("launchCommand"), metadata.value(QStringLiteral("launchCommand")) },
            { QStringLiteral("reloadPreviewSupported"),
              metadata.value(QStringLiteral("reloadPreviewSupported")) },
        };
    }
    return { metadata.value(QStringLiteral("launcherSession")).toString(), metadata, status };
}

LauncherSessionDiscovery discoverLauncherSessions(int timeoutMs)
{
    LauncherSessionDiscovery discovery;
    QSet<QString> seenSessionIds;
    const QStringList registryDirs = launcherRegistryDirs();
    for (const QString &registryDir : registryDirs) {
        const QDir dir(registryDir);
        for (const QString &entry : dir.entryList({ QStringLiteral("*.json") }, QDir::Files)) {
            const QString path = dir.filePath(entry);
            if (QFileInfo(path).isSymLink())
                continue;
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                continue;
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
            file.close();
            if (parseError.error != QJsonParseError::NoError || !document.isObject())
                continue;
            const QJsonObject metadata = document.object();
            const QString id = metadata.value(QStringLiteral("launcherSession")).toString();
            if (id.isEmpty() || entry != id + QStringLiteral(".json")
                    || !hasLauncherControlEndpoint(metadata) || seenSessionIds.contains(id))
                continue;
            seenSessionIds.insert(id);
            LauncherSession session = probeLauncherSession(metadata, timeoutMs);
            if (!session.status.value(QStringLiteral("controlReachable")).toBool()
                    && !verifiedStaleLauncherReason(metadata).isEmpty()) {
                const QJsonObject cleanup = cleanupStaleLauncher(metadata, registryDirs);
                if (cleanup.value(QStringLiteral("complete")).toBool()) {
                    discovery.staleRemovals.append(cleanup);
                    continue;
                }
                discovery.cleanupFailures.append(cleanup);
                session.status.insert(QStringLiteral("cleanupError"), cleanup);
            }
            discovery.sessions.append(session);
        }
    }
    return discovery;
}

QJsonArray launcherSessionSummaries(const QList<LauncherSession> &sessions)
{
    QJsonArray summaries;
    for (const LauncherSession &session : sessions) {
        QJsonObject summary{
            { QStringLiteral("launcherSession"), session.id },
            { QStringLiteral("controlReachable"), session.status.value(QStringLiteral("controlReachable")).toBool(true) },
            { QStringLiteral("controlError"), session.status.value(QStringLiteral("controlError")) },
            { QStringLiteral("cleanupError"), session.status.value(QStringLiteral("cleanupError")) },
            { QStringLiteral("controlEndpoint"), launcherControlEndpointSummary(session) },
            { QStringLiteral("sessionType"), session.status.value(QStringLiteral("sessionType")) },
            { QStringLiteral("launchCommand"), session.status.value(QStringLiteral("launchCommand")) },
            { QStringLiteral("reloadPreviewSupported"), session.status.value(QStringLiteral("reloadPreviewSupported")) },
            { QStringLiteral("previewRoot"), session.status.value(QStringLiteral("previewRoot")) },
            { QStringLiteral("targetPid"), session.status.value(QStringLiteral("targetPid")) },
            { QStringLiteral("sessionReady"), session.status.contains(QStringLiteral("sessionReady"))
                    ? session.status.value(QStringLiteral("sessionReady"))
                    : QJsonValue(QJsonValue::Null) },
            { QStringLiteral("targetRunning"), session.status.contains(QStringLiteral("targetRunning"))
                    ? session.status.value(QStringLiteral("targetRunning"))
                    : QJsonValue(QJsonValue::Null) },
            { QStringLiteral("launcherCwd"),
              session.status.contains(QStringLiteral("launcherCwd"))
                      ? session.status.value(QStringLiteral("launcherCwd"))
                      : session.metadata.value(QStringLiteral("launcherCwd")) },
            { QStringLiteral("services"), session.status.value(QStringLiteral("services")) },
        };
        qmlagentInsertPidNamespace(summary,
                                  session.metadata.value(QStringLiteral("pidNamespace")),
                                  qmlagentClientPidNamespace());
        summaries.append(summary);
    }
    return summaries;
}

bool resolveLauncherSession(int timeoutMs, LauncherSession *selected, QString *error,
                            const QString &wantedSessionId)
{
    error->clear();
    QList<LauncherSession> matching;
    QList<LauncherSession> reachable;
    for (const LauncherSession &session : discoverLauncherSessions(timeoutMs).sessions) {
        if (!wantedSessionId.isEmpty() && !session.id.startsWith(wantedSessionId))
            continue;
        matching.append(session);
        if (session.status.value(QStringLiteral("controlReachable")).toBool(false))
            reachable.append(session);
    }
    if (reachable.isEmpty()) {
        if (!matching.isEmpty()) {
            *error = QStringLiteral("No reachable qmlagent-launcher control channel. "
                                    "Registry entries were retained; launcher PIDs may belong "
                                    "to another PID namespace. Sessions: %1")
                             .arg(QString::fromUtf8(QJsonDocument(launcherSessionSummaries(matching))
                                                            .toJson(QJsonDocument::Compact)));
            return false;
        }
        if (!wantedSessionId.isEmpty()) {
            *error = QStringLiteral("No live qmlagent-launcher session matches --session %1. "
                                    "Run qmlagentctl sessions to list live sessions.")
                             .arg(wantedSessionId);
            return false;
        }
        *error = QStringLiteral("No live qmlagent-launcher session found.\n\n"
                                "Start one of these first:\n"
                                "  qmlagent-launcher preview <Main.qml>\n"
                                "  qmlagent-launcher app <executable> [-- args...]");
        return false;
    }
    if (reachable.size() > 1) {
        *error = QStringLiteral("Multiple live qmlagent-launcher sessions found. "
                                "Pin one by its launcherSession id (qmlagentctl --session <id>, "
                                "or the MCP tools' session argument). Sessions: %1")
                         .arg(QString::fromUtf8(QJsonDocument(launcherSessionSummaries(reachable))
                                                    .toJson(QJsonDocument::Compact)));
        return false;
    }
    *selected = reachable.first();
    return true;
}
