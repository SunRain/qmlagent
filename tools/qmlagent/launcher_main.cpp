// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include <private/qqmldebugclient_p.h>
#include <private/qqmldebugconnection_p.h>

#include <QtCore/qcommandlineparser.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qdatetime.h>
#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qfilesystemwatcher.h>
#include <QtCore/qhash.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qpointer.h>
#include <QtCore/qprocess.h>
#include <QtCore/qregularexpression.h>
#include <QtCore/qsavefile.h>
#include <QtCore/qstandardpaths.h>
#include <QtCore/qtextstream.h>
#include <QtCore/qtimer.h>
#include <QtCore/qurl.h>
#include <QtCore/quuid.h>
#include <QtGui/qguiapplication.h>
#include <QtNetwork/qlocalserver.h>
#include <QtNetwork/qlocalsocket.h>
#include <QtQml/qqmlapplicationengine.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlerror.h>
#include <QtQuick/qquickitem.h>
#include <QtQuick/qquickwindow.h>

#include <atomic>
#include <csignal>

#if defined(Q_OS_UNIX)
#include <signal.h>
#include <unistd.h>
#endif

static std::atomic_int s_pendingSignal = 0;

static void handleLauncherSignal(int signalNumber)
{
    s_pendingSignal.store(signalNumber);
}

static void terminateTargetProcess(QProcess *target)
{
    if (!target || target->state() == QProcess::NotRunning)
        return;

#if defined(Q_OS_UNIX)
    const qint64 pid = target->processId();
    if (pid > 0) {
        ::kill(-pid, SIGTERM);
        QTimer::singleShot(1500, target, [target, pid]() {
            if (target->state() != QProcess::NotRunning)
                ::kill(-pid, SIGKILL);
        });
        return;
    }
#endif

    target->terminate();
    QTimer::singleShot(1500, target, [target]() {
        if (target->state() != QProcess::NotRunning)
            target->kill();
    });
}

class QmlAgentClient : public QQmlDebugClient
{
    Q_OBJECT

public:
    explicit QmlAgentClient(QQmlDebugConnection *connection)
        : QQmlDebugClient(QStringLiteral("QmlAgent"), connection)
    {
    }

signals:
    void received(const QByteArray &message);

private:
    void messageReceived(const QByteArray &message) override { emit received(message); }
};

static QByteArray compactJson(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

static void printJsonLine(const QJsonObject &object)
{
    QTextStream(stdout) << QString::fromUtf8(compactJson(object)) << Qt::endl;
}

static int fail(const QString &message)
{
    QTextStream(stderr) << message << Qt::endl;
    return 1;
}

static QString qmlAgentDataRoot()
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

static QString qmlAgentTempRoot()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (base.isEmpty())
        base = QDir::tempPath();
    const QString canonicalBase = QFileInfo(base).canonicalFilePath();
    if (!canonicalBase.isEmpty())
        base = canonicalBase;
    return QDir(base).filePath(QStringLiteral("QmlAgent"));
}

static QFileDevice::Permissions privateDirPermissions()
{
    return QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
}

static QFileDevice::Permissions privateFilePermissions()
{
    return QFileDevice::ReadOwner | QFileDevice::WriteOwner;
}

static bool ensurePrivateDirectory(const QString &path, QString *error = nullptr)
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

static bool pathIsInsideDirectory(const QString &path, const QString &directory)
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

static bool responsePathAllowed(const QString &path, const QString &mailboxDir)
{
    if (QFileInfo(path).isSymLink())
        return false;

    const QString replyRoot = QDir(qmlAgentTempRoot()).filePath(QStringLiteral("mailbox-replies"));
    return pathIsInsideDirectory(path, replyRoot) || pathIsInsideDirectory(path, mailboxDir);
}

static QStringList globalLauncherRegistryDirs()
{
    return {
        QDir(qmlAgentTempRoot()).filePath(QStringLiteral("launcher-sessions")),
        QDir(qmlAgentDataRoot()).filePath(QStringLiteral("launcher-sessions")),
    };
}

static QString launcherExitDir()
{
    return QDir(qmlAgentTempRoot()).filePath(QStringLiteral("launcher-exits"));
}

static QString signalNameForCode(int code)
{
    switch (code) {
    case 6:
        return QStringLiteral("SIGABRT");
    case 9:
        return QStringLiteral("SIGKILL");
    case 11:
        return QStringLiteral("SIGSEGV");
    case 15:
        return QStringLiteral("SIGTERM");
    default:
        return {};
    }
}

static void writeLauncherExitReport(const QString &sessionId, qint64 targetPid,
                                    const QString &sessionType, const QString &launchCommand,
                                    const QString &previewRoot, int exitCode,
                                    QProcess::ExitStatus status)
{
    if (status == QProcess::NormalExit && exitCode == 0)
        return;

    const QString dir = launcherExitDir();
    if (!ensurePrivateDirectory(dir))
        return;
    QSaveFile report(QDir(dir).filePath(QStringLiteral("%1.json").arg(sessionId)));
    if (!report.open(QIODevice::WriteOnly))
        return;

    QJsonObject object{
        { QStringLiteral("event"), QStringLiteral("targetFinished") },
        { QStringLiteral("launcherSession"), sessionId },
        { QStringLiteral("launcherPid"), qint64(QCoreApplication::applicationPid()) },
        { QStringLiteral("targetPid"), targetPid },
        { QStringLiteral("sessionType"), sessionType },
        { QStringLiteral("launchCommand"), launchCommand },
        { QStringLiteral("previewRoot"), previewRoot },
        { QStringLiteral("exitCode"), exitCode },
        { QStringLiteral("crashed"), status == QProcess::CrashExit },
        { QStringLiteral("exitStatus"), status == QProcess::CrashExit
                ? QStringLiteral("crash")
                : QStringLiteral("normal") },
        { QStringLiteral("timestampUtc"),
          QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) },
    };
    const QString signalName = signalNameForCode(exitCode);
    if (!signalName.isEmpty())
        object.insert(QStringLiteral("signalName"), signalName);

    report.write(compactJson(object));
    report.write("\n");
    if (report.commit())
        QFile::setPermissions(report.fileName(), privateFilePermissions());
}

static QStringList launcherRegistryDirs()
{
    QStringList dirs;
    const QString workspaceStateDir = QDir::current().filePath(QStringLiteral(".qmlagent"));
    ensurePrivateDirectory(workspaceStateDir);
    const QFileInfo workspaceStateInfo(workspaceStateDir);
    if (workspaceStateInfo.exists() && workspaceStateInfo.isWritable())
        dirs.append(QDir(workspaceStateDir).filePath(QStringLiteral("launcher-sessions")));

    for (const QString &globalDir : globalLauncherRegistryDirs()) {
        if (!dirs.contains(globalDir))
            dirs.append(globalDir);
    }
    return dirs;
}

static QStringList launcherRegistryPaths(const QString &sessionId)
{
    QStringList paths;
    for (const QString &dir : launcherRegistryDirs())
        paths.append(QDir(dir).filePath(QStringLiteral("%1.json").arg(sessionId)));
    return paths;
}

static void removeLauncherRegistryFiles(const QString &sessionId)
{
    for (const QString &path : launcherRegistryPaths(sessionId))
        QFile::remove(path);
}

static void writeLauncherRegistryFiles(const QString &sessionId, const QJsonObject &metadata)
{
    for (const QString &path : launcherRegistryPaths(sessionId)) {
        if (!ensurePrivateDirectory(QFileInfo(path).absolutePath()))
            continue;
        QSaveFile sessionFile(path);
        if (!sessionFile.open(QIODevice::WriteOnly))
            continue;
        sessionFile.write(compactJson(metadata));
        if (sessionFile.commit())
            QFile::setPermissions(path, privateFilePermissions());
    }
}

static QString launcherRuntimeDir()
{
    const QString path = QDir(qmlAgentTempRoot()).filePath(
            QStringLiteral("qmlagent-%1").arg(qint64(QCoreApplication::applicationPid())));
    ensurePrivateDirectory(qmlAgentTempRoot());
    ensurePrivateDirectory(path);
    return path;
}

static QJsonObject makeRequest(int id, const QString &method, const QJsonObject &params)
{
    return {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), id },
        { QStringLiteral("method"), method },
        { QStringLiteral("params"), params },
    };
}

static QJsonObject makeNotification(const QString &method, const QJsonObject &params)
{
    return {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("method"), method },
        { QStringLiteral("params"), params },
    };
}

static bool hasInternalPreviewHostArgument(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--preview-host-internal"))
            return true;
    }
    return false;
}

class PreviewLoader
{
public:
    PreviewLoader()
    {
        QObject::connect(&m_engine, &QQmlApplicationEngine::warnings,
                         &m_engine, [this](const QList<QQmlError> &warnings) {
            for (const QQmlError &error : warnings)
                m_warnings.append(error.toString());
        });
    }

    QJsonObject reload(const QString &qmlFile)
    {
        // Reload re-instantiates the window, which would otherwise snap back
        // to the QML's declared size at the default position. Capture the
        // current placement first so preview-driven iteration keeps the
        // window where the presenter put it.
        QRect savedGeometry;
        if (QWindow *existing = currentWindow())
            savedGeometry = existing->geometry();

        deleteLoadedRoots();
        m_warnings = {};
        m_engine.clearComponentCache();
        m_engine.load(QUrl::fromLocalFile(qmlFile));

        m_loadedRoots.clear();
        const QObjectList roots = m_engine.rootObjects();
        for (QObject *root : roots)
            m_loadedRoots.append(root);

        bool ok = !roots.isEmpty();
        bool windowPreserved = false;
        QString rootKind;
        if (ok) {
            QObject *root = roots.constFirst();
            if (auto *window = qobject_cast<QWindow *>(root)) {
                rootKind = QStringLiteral("window");
                if (savedGeometry.isValid()) {
                    window->setGeometry(savedGeometry);
                    windowPreserved = true;
                }
                window->show();
            } else if (auto *item = qobject_cast<QQuickItem *>(root)) {
                rootKind = QStringLiteral("item");
                m_hostWindow = new QQuickWindow;
                m_hostWindow->setTitle(QStringLiteral("QmlAgent Preview"));
                if (savedGeometry.isValid()) {
                    m_hostWindow->setGeometry(savedGeometry);
                    windowPreserved = true;
                } else {
                    m_hostWindow->resize(640, 420);
                }
                item->setParentItem(m_hostWindow->contentItem());
                item->setParent(m_hostWindow);
                if (item->width() <= 0)
                    item->setWidth(m_hostWindow->width());
                if (item->height() <= 0)
                    item->setHeight(m_hostWindow->height());
                m_hostWindow->show();
            } else {
                ok = false;
                rootKind = QStringLiteral("unsupported");
                m_warnings.append(QStringLiteral("Root object is neither QWindow nor QQuickItem: %1")
                                          .arg(QString::fromUtf8(root->metaObject()->className())));
            }
        }

        return {
            { QStringLiteral("ok"), ok },
            { QStringLiteral("root"), qmlFile },
            { QStringLiteral("rootKind"), rootKind },
            { QStringLiteral("rootStatePreserved"), false },
            { QStringLiteral("windowPreserved"), windowPreserved },
            { QStringLiteral("loader"), QStringLiteral("QQmlApplicationEngine") },
            { QStringLiteral("errors"), m_warnings },
        };
    }

    QWindow *currentWindow() const
    {
        if (m_hostWindow)
            return m_hostWindow;
        for (const QPointer<QObject> &root : m_loadedRoots) {
            if (auto *window = qobject_cast<QWindow *>(root.data()))
                return window;
        }
        return nullptr;
    }

private:
    void deleteLoadedRoots()
    {
        delete m_hostWindow;
        m_hostWindow = nullptr;

        for (const QPointer<QObject> &root : std::as_const(m_loadedRoots)) {
            if (root)
                delete root;
        }
        m_loadedRoots.clear();
    }

    QQmlApplicationEngine m_engine;
    QJsonArray m_warnings;
    QList<QPointer<QObject>> m_loadedRoots;
    QQuickWindow *m_hostWindow = nullptr;
};

static int runInternalPreviewHost(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qmlagent-preview-host-internal"));

    const QStringList arguments = QCoreApplication::arguments();
    const int marker = arguments.indexOf(QStringLiteral("--preview-host-internal"));
    const int controlMarker = arguments.indexOf(QStringLiteral("--preview-control"));
    if (marker < 0 || marker + 1 >= arguments.size()
            || controlMarker < 0 || controlMarker + 1 >= arguments.size()) {
        return 2;
    }

    const QString qmlFile = arguments.at(marker + 1);
    const QString controlSocketName = arguments.at(controlMarker + 1);

    PreviewLoader previewLoader;

    QLocalServer controlServer;
    QLocalServer::removeServer(controlSocketName);
    if (!controlServer.listen(controlSocketName))
        return 3;

    QObject::connect(&controlServer, &QLocalServer::newConnection, &controlServer, [&]() {
        while (QLocalSocket *socket = controlServer.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, &previewLoader, qmlFile]() {
                socket->readAll();
                const QJsonObject response{
                    { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
                    { QStringLiteral("id"), 1 },
                    { QStringLiteral("result"), previewLoader.reload(qmlFile) },
                };
                socket->write(compactJson(response));
                socket->write("\n");
                socket->flush();
                socket->disconnectFromServer();
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    const QJsonObject initial = previewLoader.reload(qmlFile);
    if (!initial.value(QStringLiteral("ok")).toBool())
        return 1;

    return app.exec();
}

static QJsonObject errorObject(const QString &id, const QString &message)
{
    return {
        { QStringLiteral("ok"), false },
        { QStringLiteral("error"), QJsonObject{
            { QStringLiteral("id"), id },
            { QStringLiteral("message"), message },
        } },
    };
}

class LauncherControlServer : public QObject
{
    Q_OBJECT

public:
    LauncherControlServer(const QString &controlMailboxDir, const QString &debugSocketPath,
                          const QString &sessionType, const QString &launchCommand,
                          const QString &previewRoot, const QString &previewControlName,
                          QProcess *target, QmlAgentClient *agentClient, QObject *parent = nullptr)
        : QObject(parent),
          m_controlMailboxDir(controlMailboxDir),
          m_debugSocketPath(debugSocketPath),
          m_sessionType(sessionType),
          m_launchCommand(launchCommand),
          m_previewRoot(previewRoot),
          m_previewControlName(previewControlName),
          m_target(target),
          m_agentClient(agentClient)
    {
        connect(&m_mailboxWatcher, &QFileSystemWatcher::directoryChanged, this,
                &LauncherControlServer::processMailboxRequests);
        // The macOS directory watcher delivers with a fixed ~500ms latency,
        // which becomes the per-command floor for every qmlagentctl and MCP
        // request. A fast poll over the small private mailbox keeps command
        // latency in the low milliseconds; the watcher remains as backstop.
        m_mailboxPollTimer.setInterval(10);
        connect(&m_mailboxPollTimer, &QTimer::timeout, this,
                &LauncherControlServer::processMailboxRequests);
        connect(m_agentClient, &QmlAgentClient::received, this,
                &LauncherControlServer::handleAgentMessage);
    }

    bool listen(QString *error)
    {
        if (!ensurePrivateDirectory(m_controlMailboxDir, error))
            return false;
        m_mailboxWatcher.addPath(m_controlMailboxDir);
        m_mailboxPollTimer.start();
        return true;
    }

    QString controlMailboxDir() const
    {
        return m_controlMailboxDir;
    }

    QJsonObject controlEndpoint() const
    {
        return {
            { QStringLiteral("kind"), QStringLiteral("fileMailbox") },
            { QStringLiteral("path"), m_controlMailboxDir },
        };
    }

    void setSessionReady(bool ready) { m_sessionReady = ready; }

    bool stopRequested() const { return m_stopRequested; }

    void requestStop()
    {
        m_stopRequested = true;
        terminateTargetProcess(m_target);
    }

private:
    struct PendingAgentRequest
    {
        QString responsePath;
        int controlId = 0;
    };

    static QString responsePathForRequest(const QString &requestPath, const QJsonObject &request)
    {
        const QString replyTo = request.value(QStringLiteral("replyTo")).toString();
        const QFileInfo info(requestPath);
        if (!replyTo.isEmpty() && responsePathAllowed(replyTo, info.dir().absolutePath()))
            return replyTo;
        QString name = info.fileName();
        if (name.startsWith(QLatin1String("request-")))
            name.replace(0, 8, QStringLiteral("response-"));
        return info.dir().filePath(name);
    }

    void processMailboxRequests()
    {
        QDir dir(m_controlMailboxDir);
        const QStringList entries = dir.entryList({ QStringLiteral("request-*.json") }, QDir::Files,
                                                  QDir::Name);
        for (const QString &entry : entries) {
            const QString requestPath = dir.filePath(entry);
            const QFileInfo requestInfo(requestPath);
            if (requestInfo.isSymLink() || !pathIsInsideDirectory(requestPath, m_controlMailboxDir)) {
                QFile::remove(requestPath);
                continue;
            }
            QFile requestFile(requestPath);
            if (!requestFile.open(QIODevice::ReadOnly))
                continue;
            const QByteArray payload = requestFile.readAll();
            requestFile.close();
            QFile::remove(requestPath);
            handleControlRequest(requestPath, payload.trimmed());
        }
    }

    void handleControlRequest(const QString &requestPath, const QByteArray &line)
    {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            const QString responsePath = responsePathForRequest(requestPath, {});
            writeControlResponse(responsePath, 0, errorObject(QStringLiteral("control.invalid_json"),
                                                              QStringLiteral("Invalid JSON control request.")));
            return;
        }

        const QJsonObject request = document.object();
        const QString responsePath = responsePathForRequest(requestPath, request);
        const int controlId = request.value(QStringLiteral("id")).toInt(0);
        const QString method = request.value(QStringLiteral("method")).toString();
        const QJsonObject params = request.value(QStringLiteral("params")).toObject();

        if (method == QLatin1String("Session.status")) {
            writeControlResponse(responsePath, controlId, statusObject());
            return;
        }

        if (method == QLatin1String("Session.stop")) {
            writeControlResponse(responsePath, controlId, {
                { QStringLiteral("ok"), true },
                { QStringLiteral("action"), QStringLiteral("stop") },
                { QStringLiteral("targetPid"), m_target ? qint64(m_target->processId()) : 0 },
            });
            requestStop();
            return;
        }

        if (!m_sessionReady) {
            writeControlResponse(responsePath, controlId,
                                 errorObject(QStringLiteral("session.not_ready"),
                                             QStringLiteral("Target debug session is not ready yet.")));
            return;
        }

        if (method == QLatin1String("QmlAgent.request")) {
            const QString targetMethod = params.value(QStringLiteral("method")).toString();
            if (targetMethod.isEmpty()) {
                writeControlResponse(responsePath, controlId,
                                     errorObject(QStringLiteral("request.method_missing"),
                                                 QStringLiteral("QmlAgent.request requires params.method.")));
                return;
            }
            const int targetId = ++m_nextTargetRequestId;
            m_pendingAgentRequests.insert(targetId, { responsePath, controlId });
            m_agentClient->sendMessage(compactJson(makeRequest(
                    targetId, targetMethod, params.value(QStringLiteral("params")).toObject())));
            return;
        }

        if (method == QLatin1String("Preview.reload")) {
            handlePreviewReload(responsePath, controlId, params);
            return;
        }

        writeControlResponse(responsePath, controlId,
                             errorObject(QStringLiteral("control.unknown_method"),
                                         QStringLiteral("Unknown qmlagent-launcher control method: %1")
                                                 .arg(method)));
    }

    void handlePreviewReload(const QString &responsePath, int controlId, const QJsonObject &params)
    {
        if (m_sessionType != QLatin1String("preview")) {
            QJsonObject result = errorObject(
                    QStringLiteral("preview.reload_not_supported"),
                    QStringLiteral("reload-preview only works with sessions started by: "
                                   "qmlagent-launcher preview <Main.qml>"));
            QJsonObject error = result.value(QStringLiteral("error")).toObject();
            error.insert(QStringLiteral("requiredLaunchCommand"),
                         QStringLiteral("qmlagent-launcher preview <Main.qml>"));
            error.insert(QStringLiteral("currentLaunchCommand"), m_launchCommand);
            error.insert(QStringLiteral("reloadPreviewSupported"), false);
            error.insert(QStringLiteral("nextActions"), QJsonArray{
                             QStringLiteral("rebuild/relaunch the current application after edits"),
                             QStringLiteral("start qmlagent-launcher preview <Main.qml> for agent-owned QML iteration"),
                         });
            result.insert(QStringLiteral("error"), error);
            writeControlResponse(responsePath, controlId, result);
            return;
        }

        QLocalSocket previewSocket;
        previewSocket.connectToServer(m_previewControlName);
        const int timeoutMs = params.value(QStringLiteral("timeoutMs")).toInt(3000);
        if (!previewSocket.waitForConnected(timeoutMs)) {
            writeControlResponse(responsePath, controlId,
                                 errorObject(QStringLiteral("preview.reload_connect_failed"),
                                             QStringLiteral("Timed out connecting to preview shell reload socket.")));
            return;
        }

        previewSocket.write("{\"method\":\"reload\"}\n");
        previewSocket.flush();
        previewSocket.waitForBytesWritten(timeoutMs);
        if (!previewSocket.waitForReadyRead(timeoutMs)) {
            writeControlResponse(responsePath, controlId,
                                 errorObject(QStringLiteral("preview.reload_timeout"),
                                             QStringLiteral("Timed out waiting for preview shell reload response.")));
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(previewSocket.readAll().trimmed(),
                                                               &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            writeControlResponse(responsePath, controlId,
                                 errorObject(QStringLiteral("preview.reload_invalid_response"),
                                             QStringLiteral("Invalid preview shell reload response.")));
            return;
        }

        QJsonObject result = document.object().value(QStringLiteral("result")).toObject();
        result.insert(QStringLiteral("service"), QStringLiteral("QmlAgentPreviewShell"));
        result.insert(QStringLiteral("action"), QStringLiteral("reload-preview"));
        result.insert(QStringLiteral("root"), m_previewRoot);
        result.insert(QStringLiteral("event"), QStringLiteral("Preview.reloaded"));
        result.insert(QStringLiteral("subscriptionsInvalidated"), true);
        m_agentClient->sendMessage(compactJson(makeNotification(
                QStringLiteral("Session.reset"),
                {
                    { QStringLiteral("reason"), QStringLiteral("preview-reloaded") },
                    { QStringLiteral("event"), QStringLiteral("Preview.reloaded") },
                })));
        writeControlResponse(responsePath, controlId, result);
    }

    QJsonObject statusObject() const
    {
        return {
            { QStringLiteral("ok"), true },
            { QStringLiteral("sessionReady"), m_sessionReady },
            { QStringLiteral("sessionType"), m_sessionType },
            { QStringLiteral("launchCommand"), m_launchCommand },
            { QStringLiteral("reloadPreviewSupported"), m_sessionType == QLatin1String("preview") },
            { QStringLiteral("previewRoot"), m_previewRoot },
            { QStringLiteral("debugSocket"), m_debugSocketPath },
            { QStringLiteral("controlEndpoint"), controlEndpoint() },
            { QStringLiteral("controlMailbox"), m_controlMailboxDir },
            { QStringLiteral("targetPid"), m_target ? qint64(m_target->processId()) : 0 },
            { QStringLiteral("targetRunning"), m_target && m_target->state() != QProcess::NotRunning },
            { QStringLiteral("services"), QJsonObject{
                { QStringLiteral("QmlAgent"), m_agentClient->state() == QQmlDebugClient::Enabled },
            } },
            { QStringLiteral("nextActions"), m_sessionType == QLatin1String("preview")
                    ? QJsonArray{
                              QStringLiteral("qmlagentctl methods"),
                              QStringLiteral("qmlagentctl reload-preview"),
                              QStringLiteral("qmlagentctl wait '<selector>' --state found"),
                              QStringLiteral("qmlagentctl query '<selector>'"),
                              QStringLiteral("qmlagentctl query-many --params '{\"queries\":[...]}'"),
                              QStringLiteral("qmlagentctl scroll-into-view '<selector>'"),
                      }
                    : QJsonArray{
                              QStringLiteral("qmlagentctl methods"),
                              QStringLiteral("qmlagentctl query '<selector>'"),
                              QStringLiteral("qmlagentctl query-many --params '{\"queries\":[...]}'"),
                              QStringLiteral("qmlagentctl scroll-into-view '<selector>'"),
                              QStringLiteral("rebuild/relaunch after source edits"),
                      } },
        };
    }

    void handleAgentMessage(const QByteArray &message)
    {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(message, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return;

        const QJsonObject object = document.object();
        const int targetId = object.value(QStringLiteral("id")).toInt(-1);
        if (m_pendingAgentRequests.contains(targetId)) {
            const PendingAgentRequest pending = m_pendingAgentRequests.take(targetId);
            writeControlResponse(pending.responsePath, pending.controlId, object);
            return;
        }

    }

    void writeControlResponse(const QString &responsePath, int id, const QJsonObject &result)
    {
        if (responsePath.isEmpty())
            return;
        if (QFileInfo(responsePath).isSymLink())
            return;
        const QString parent = QFileInfo(responsePath).absolutePath();
        if (!ensurePrivateDirectory(parent))
            return;
        QSaveFile responseFile(responsePath);
        if (!responseFile.open(QIODevice::WriteOnly))
            return;
        responseFile.write(compactJson({
            { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
            { QStringLiteral("id"), id },
            { QStringLiteral("result"), result },
        }));
        responseFile.write("\n");
        if (responseFile.commit())
            QFile::setPermissions(responsePath, privateFilePermissions());
    }

    QString m_controlMailboxDir;
    QString m_debugSocketPath;
    QString m_sessionType;
    QString m_launchCommand;
    QString m_previewRoot;
    QString m_previewControlName;
    QProcess *m_target = nullptr;
    QmlAgentClient *m_agentClient = nullptr;
    QFileSystemWatcher m_mailboxWatcher;
    QTimer m_mailboxPollTimer;
    QHash<int, PendingAgentRequest> m_pendingAgentRequests;
    int m_nextTargetRequestId = 1000;
    bool m_sessionReady = false;
    bool m_stopRequested = false;
};

static QString shellQuote(const QString &value)
{
    if (!value.contains(QRegularExpression(QStringLiteral("[\\s'\"\\\\]"))))
        return value;
    QString quoted = value;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(quoted);
}

int main(int argc, char **argv)
{
    if (hasInternalPreviewHostArgument(argc, argv))
        return runInternalPreviewHost(argc, argv);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qmlagent-launcher"));
    QCoreApplication::setApplicationVersion(QStringLiteral(QMLAGENT_VERSION_STR));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
            "Start a QmlAgent session.\n\n"
            "Workflows:\n"
            "  qmlagent-launcher preview Main.qml\n"
            "      Agent-owned preview shell. Use qmlagentctl reload-preview after edits.\n"
            "  qmlagent-launcher app ./myapp -- --app-arg\n"
            "      Existing app session. No qmlagentctl reload-preview support."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({
        QStringLiteral("timeout"),
        QStringLiteral("Connection timeout in milliseconds."),
        QStringLiteral("ms"),
        QStringLiteral("10000"),
    });
    parser.addOption({
        QStringLiteral("print-json"),
        QStringLiteral("Print launcher/session status as compact JSON lines."),
    });
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("preview or app."));
    parser.addPositionalArgument(QStringLiteral("target"), QStringLiteral("Main.qml or executable."));
    parser.addPositionalArgument(QStringLiteral("arguments"),
                                 QStringLiteral("Arguments for app sessions after --."),
                                 QStringLiteral("[arguments...]"));
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.size() < 2)
        return fail(QStringLiteral("Usage: qmlagent-launcher preview <Main.qml> OR qmlagent-launcher app <executable> [-- args...]"));

    const QString command = positional.at(0);
    if (command != QLatin1String("preview") && command != QLatin1String("app"))
        return fail(QStringLiteral("Unknown qmlagent-launcher command '%1'. Use 'preview' or 'app'.").arg(command));

    bool ok = false;
    const int timeoutMs = parser.value(QStringLiteral("timeout")).toInt(&ok);
    if (!ok || timeoutMs <= 0)
        return fail(QStringLiteral("--timeout must be a positive integer."));

    const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString shortSessionId = sessionId.left(8);
    for (const QString &registryDir : launcherRegistryDirs())
        QDir().mkpath(registryDir);

    const QString runtimeDir = launcherRuntimeDir();
    const QString debugSocketPath = QDir(runtimeDir).filePath(QStringLiteral("d-%1").arg(shortSessionId));
    const QString controlMailboxDir = QDir(runtimeDir).filePath(QStringLiteral("control-%1").arg(shortSessionId));
    const QString previewControlName = QDir(runtimeDir).filePath(QStringLiteral("p-%1").arg(shortSessionId));
    QFile::remove(debugSocketPath);
    QLocalServer::removeServer(previewControlName);

    QString executable;
    QStringList targetArguments;
    QString sessionType;
    QString previewRoot;
    QString launchCommand;
    if (command == QLatin1String("preview")) {
        const QFileInfo qmlFile(positional.at(1));
        if (!qmlFile.isFile() || !qmlFile.isReadable())
            return fail(QStringLiteral("Could not read preview root QML file: %1")
                                .arg(qmlFile.filePath()));
        sessionType = QStringLiteral("preview");
        previewRoot = qmlFile.absoluteFilePath();
        executable = QCoreApplication::applicationFilePath();
        targetArguments = {
            QStringLiteral("--preview-host-internal"),
            previewRoot,
            QStringLiteral("--preview-control"),
            previewControlName,
        };
        launchCommand = QStringLiteral("qmlagent-launcher preview %1").arg(shellQuote(previewRoot));
    } else {
        sessionType = QStringLiteral("application");
        executable = positional.at(1);
        targetArguments = positional.mid(2);
        launchCommand = QStringLiteral("qmlagent-launcher app %1").arg(shellQuote(executable));
    }

    QStringList qmlDebugArguments{
        QStringLiteral("-qmljsdebugger=file:%1,block,services:QmlAgent").arg(debugSocketPath),
    };
    qmlDebugArguments.append(targetArguments);

    QQmlDebugConnection connection;
    QmlAgentClient agentClient(&connection);
    connection.startLocalServer(debugSocketPath);

    QProcess target;
    qint64 launchedTargetPid = 0;
#if defined(Q_OS_UNIX)
    target.setChildProcessModifier([]() {
        ::setsid();
    });
#endif
    LauncherControlServer controlServer(controlMailboxDir, debugSocketPath, sessionType,
                                        launchCommand, previewRoot, previewControlName, &target,
                                        &agentClient, &app);
    QString controlError;
    if (!controlServer.listen(&controlError))
        return fail(controlError);

    target.setProcessChannelMode(QProcess::MergedChannels);
    // Forward target output and keep a bounded copy so launch failures can
    // diagnose the cause (for example Qt's "Debugging has not been enabled").
    QString capturedTargetOutput;
    QObject::connect(&target, &QIODevice::readyRead, &target, [&]() {
        const QString chunk = QString::fromUtf8(target.readAll());
        QTextStream(stderr) << chunk;
        capturedTargetOutput.append(chunk);
        constexpr qsizetype MaxCapturedOutputBytes = 16384;
        if (capturedTargetOutput.size() > MaxCapturedOutputBytes)
            capturedTargetOutput.remove(0, capturedTargetOutput.size() - MaxCapturedOutputBytes);
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&]() {
        removeLauncherRegistryFiles(sessionId);
        QLocalServer::removeServer(previewControlName);
        QFile::remove(debugSocketPath);
        QDir(runtimeDir).removeRecursively();
    });
    std::signal(SIGINT, handleLauncherSignal);
    std::signal(SIGTERM, handleLauncherSignal);
    QTimer signalTimer;
    signalTimer.setInterval(100);
    QObject::connect(&signalTimer, &QTimer::timeout, &app, [&]() {
        const int signalNumber = s_pendingSignal.exchange(0);
        if (!signalNumber)
            return;
        printJsonLine({
            { QStringLiteral("event"), QStringLiteral("launcherSignal") },
            { QStringLiteral("signal"), signalNumber },
            { QStringLiteral("targetPid"), qint64(target.processId()) },
        });
        controlServer.requestStop();
        QTimer::singleShot(2000, &app, &QCoreApplication::quit);
    });
    signalTimer.start();
    QObject::connect(&target, &QProcess::finished, &app, [&](int exitCode, QProcess::ExitStatus status) {
        if (!controlServer.stopRequested()) {
            writeLauncherExitReport(sessionId, launchedTargetPid, sessionType, launchCommand,
                                    previewRoot, exitCode, status);
        }
        removeLauncherRegistryFiles(sessionId);
        QLocalServer::removeServer(previewControlName);
        QFile::remove(debugSocketPath);
        QDir(runtimeDir).removeRecursively();
        const bool requestedStop = controlServer.stopRequested();
        QJsonObject event{
            { QStringLiteral("event"), requestedStop
                    ? QStringLiteral("targetStopped")
                    : QStringLiteral("targetFinished") },
            { QStringLiteral("exitCode"), exitCode },
            { QStringLiteral("crashed"), !requestedStop && status == QProcess::CrashExit },
            { QStringLiteral("requestedStop"), requestedStop },
        };
        const QString signalName = signalNameForCode(exitCode);
        if (!signalName.isEmpty())
            event.insert(QStringLiteral("signalName"), signalName);
        printJsonLine(event);
        app.exit(requestedStop ? 0 : (status == QProcess::NormalExit ? exitCode : 3));
    });

    target.start(executable, qmlDebugArguments);
    if (!target.waitForStarted())
        return fail(QStringLiteral("Could not start target: %1").arg(target.errorString()));
    launchedTargetPid = qint64(target.processId());

    if (!connection.waitForConnected(timeoutMs)) {
        target.kill();
        target.waitForFinished();
        if (capturedTargetOutput.contains(QLatin1String("Debugging has not been enabled"))) {
            return fail(QStringLiteral(
                    "Target was built without QML debugging: it printed \"Debugging has not "
                    "been enabled\". Rebuild it with the QT_QML_DEBUG define, e.g. in CMake:\n"
                    "  target_compile_definitions(<app> PRIVATE QT_QML_DEBUG)"));
        }
        return fail(QStringLiteral(
                "Timed out waiting for target debug connection. If the target output above "
                "shows no \"QML debugging is enabled\" line, rebuild it with the QT_QML_DEBUG "
                "define (target_compile_definitions(<app> PRIVATE QT_QML_DEBUG))."));
    }

    if (agentClient.state() != QQmlDebugClient::Enabled) {
        target.kill();
        target.waitForFinished();
        // The launcher already passed services:QmlAgent, so the usual cause is
        // the qmldbg_agent plugin missing from the Qt the target runs against.
        return fail(QStringLiteral(
                "Target connected, but the QmlAgent debug service is unavailable. The "
                "qmldbg_agent plugin is likely missing from the Qt installation the target "
                "links against: verify <Qt prefix>/plugins/qmltooling/ contains it and that "
                "the target uses the same Qt where QmlAgent was installed (this launcher "
                "runs Qt %1).").arg(QString::fromUtf8(qVersion())));
    }

    controlServer.setSessionReady(true);
    writeLauncherRegistryFiles(sessionId, {
        { QStringLiteral("launcherSession"), sessionId },
        { QStringLiteral("launcherPid"), qint64(QCoreApplication::applicationPid()) },
        { QStringLiteral("targetPid"), launchedTargetPid },
        { QStringLiteral("targetExecutable"), QFileInfo(executable).absoluteFilePath() },
        { QStringLiteral("sessionType"), sessionType },
        { QStringLiteral("launchCommand"), launchCommand },
        { QStringLiteral("reloadPreviewSupported"), sessionType == QLatin1String("preview") },
        { QStringLiteral("previewRoot"), previewRoot },
        { QStringLiteral("controlEndpoint"), controlServer.controlEndpoint() },
        { QStringLiteral("controlMailbox"), controlServer.controlMailboxDir() },
    });

    printJsonLine({
        { QStringLiteral("event"), QStringLiteral("sessionReady") },
        { QStringLiteral("launcherSession"), sessionId },
        { QStringLiteral("sessionType"), sessionType },
        { QStringLiteral("reloadPreviewSupported"), sessionType == QLatin1String("preview") },
        { QStringLiteral("previewRoot"), previewRoot },
        { QStringLiteral("targetPid"), launchedTargetPid },
    });

    return app.exec();
}

#include "launcher_main.moc"
