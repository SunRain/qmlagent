// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "launcherfixture.h"

#include <QtCore/qdir.h>
#include <QtCore/qelapsedtimer.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qprocess.h>
#include <QtCore/qsavefile.h>
#include <QtCore/qtemporarydir.h>
#include <QtNetwork/qlocalserver.h>
#include <QtTest/qtest.h>

#include <unistd.h>

#include <limits>
#include <memory>

QJsonObject parseObject(const QByteArray &bytes)
{
    return QJsonDocument::fromJson(bytes).object();
}

QByteArray jsonLine(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

static QJsonObject toolRequest(const QString &name, const QJsonObject &arguments)
{
    return {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), 2 },
        { QStringLiteral("method"), QStringLiteral("tools/call") },
        { QStringLiteral("params"), QJsonObject{
            { QStringLiteral("name"), name },
            { QStringLiteral("arguments"), arguments },
        } },
    };
}

bool LauncherTestFixture::initialize()
{
    m_root = std::make_unique<QTemporaryDir>();
    if (!m_root->isValid())
        return false;
    m_environment = QProcessEnvironment::systemEnvironment();
    const QDir root(m_root->path());
    const QStringList names{ QStringLiteral("tmp"), QStringLiteral("data"),
                             QStringLiteral("cache") };
    const QStringList variables{ QStringLiteral("TMPDIR"), QStringLiteral("XDG_DATA_HOME"),
                                 QStringLiteral("XDG_CACHE_HOME") };
    for (int i = 0; i < names.size(); ++i) {
        if (!root.mkpath(names.at(i)))
            return false;
        m_environment.insert(variables.at(i), root.filePath(names.at(i)));
    }
    m_environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    return true;
}

void LauncherTestFixture::configure(QProcess *process) const
{
    process->setWorkingDirectory(m_root->path());
    process->setProcessEnvironment(m_environment);
}

ProcessResult LauncherTestFixture::run(const QString &program, const QStringList &arguments,
                                       const QByteArray &input) const
{
    OwnedProcess process;
    configure(&process);
    process.start(program, arguments);
    if (!process.waitForStarted(ProcessTimeoutMs))
        return { -1, {}, process.errorString().toUtf8() };
    if (!input.isEmpty())
        process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(ProcessTimeoutMs))
        return { -1, process.readAllStandardOutput(), process.errorString().toUtf8() };
    return { process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1,
             process.readAllStandardOutput(), process.readAllStandardError() };
}

ProcessResult LauncherTestFixture::ctl(const QStringList &arguments) const
{
    QStringList options = arguments;
    options.append({ QStringLiteral("--format"), QStringLiteral("compact"),
                     QStringLiteral("--timeout"), QStringLiteral("1000") });
    return run(QString::fromLocal8Bit(TEST_QMLAGENTCTL_EXECUTABLE), options);
}

QJsonObject LauncherTestFixture::mcp(const QString &tool, const QJsonObject &arguments) const
{
    const QByteArray input = QByteArrayLiteral(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}\n")
            + jsonLine(toolRequest(tool, arguments));
    const ProcessResult result = run(QString::fromLocal8Bit(TEST_QMLAGENTMCP_EXECUTABLE),
                                    { QStringLiteral("--timeout"), QStringLiteral("1000") }, input);
    if (result.exitCode != 0)
        return { { QStringLiteral("processError"), QString::fromUtf8(result.error) } };
    for (const QByteArray &line : result.output.split('\n')) {
        const QJsonObject response = parseObject(line);
        if (response.value(QStringLiteral("id")).toInt() == 2)
            return response.value(QStringLiteral("result")).toObject();
    }
    return { { QStringLiteral("missingResponse"), QString::fromUtf8(result.output) } };
}

QStringList LauncherTestFixture::registryPaths(const QString &sessionId) const
{
    const QDir root(m_root->path());
    const QString name = sessionId + QStringLiteral(".json");
    return {
        root.filePath(QStringLiteral(".qmlagent/launcher-sessions/") + name),
        root.filePath(QStringLiteral("tmp/QmlAgent/launcher-sessions/") + name),
        root.filePath(QStringLiteral("data/QmlAgent/launcher-sessions/") + name),
    };
}

bool LauncherTestFixture::writeRegistry(const QStringList &paths,
                                        const QJsonObject &metadata) const
{
    for (const QString &path : paths) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return false;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(jsonLine(metadata)) < 0 || !file.commit())
            return false;
    }
    return true;
}

void LauncherTestFixture::verifyRegistry(const QStringList &paths, const QByteArray &expected) const
{
    for (const QString &path : paths) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path + file.errorString()));
        QCOMPARE(file.readAll(), expected);
    }
}

QJsonObject LauncherTestFixture::launch(OwnedProcess *launcher,
                                         const QString &workingDirectory) const
{
    configure(launcher);
    if (!workingDirectory.isEmpty())
        launcher->setWorkingDirectory(workingDirectory);
    launcher->start(QString::fromLocal8Bit(TEST_QMLAGENTLAUNCHER_EXECUTABLE), {
        QStringLiteral("--print-json"), QStringLiteral("app"),
        QString::fromLocal8Bit(TEST_QMLAGENTSMOKE_EXECUTABLE),
    });
    if (!launcher->waitForStarted(ProcessTimeoutMs))
        return {};
    QElapsedTimer timer;
    timer.start();
    QByteArray pending;
    while (timer.elapsed() < ProcessTimeoutMs) {
        pending += launcher->readAllStandardOutput();
        while (pending.contains('\n')) {
            const qsizetype end = pending.indexOf('\n');
            const QJsonObject event = parseObject(pending.left(end));
            pending.remove(0, end + 1);
            if (event.value(QStringLiteral("event")) == QLatin1String("sessionReady"))
                return event;
        }
        if (launcher->state() == QProcess::NotRunning)
            break;
        launcher->waitForReadyRead(ProcessTimeoutMs - int(timer.elapsed()));
    }
    return {};
}

QString LauncherTestFixture::currentPidNamespace() const
{
#if defined(Q_OS_LINUX)
    char buffer[256];
    const ssize_t length = ::readlink("/proc/self/ns/pid", buffer, sizeof(buffer) - 1);
    if (length <= 0)
        return {};
    buffer[length] = '\0';
    return QString::fromLocal8Bit(buffer, int(length));
#else
    return {};
#endif
}

bool LauncherTestFixture::canBindUnixSocket() const
{
#if defined(Q_OS_UNIX)
    const QString path = QDir(m_root->path()).filePath(QStringLiteral("probe.sock"));
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    const bool listening = server.listen(path) || server.isListening();
    server.close();
    QFile::remove(path);
    return listening;
#else
    return false;
#endif
}

