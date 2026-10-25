// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "launcherfixture.h"

#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qjsonarray.h>
#include <QtTest/qtest.h>

class QmlAgentLauncherTest : public QObject, public LauncherTestFixture
{
    Q_OBJECT

private slots:
    void init();
    void reachableMailboxIgnoresRegistryPid_data();
    void reachableMailboxIgnoresRegistryPid();
    void unreachableMailboxRetainsRegistry_data();
    void unreachableMailboxRetainsRegistry();
    void stopDoesNotSignalRegistryPids_data();
    void stopDoesNotSignalRegistryPids();
    void sessionsExposeHealthCwdAndNamespace();

};

void QmlAgentLauncherTest::init()
{
    QVERIFY(initialize());
}

void QmlAgentLauncherTest::reachableMailboxIgnoresRegistryPid_data()
{
    QTest::addColumn<bool>("useMcp");
    QTest::newRow("cli") << false;
    QTest::newRow("mcp") << true;
}

void QmlAgentLauncherTest::reachableMailboxIgnoresRegistryPid()
{
    QFETCH(bool, useMcp);
    if (!canBindUnixSocket())
        QSKIP("This environment does not permit Unix domain socket listens");
    OwnedProcess launcher;
    const QJsonObject ready = launch(&launcher);
    QVERIFY2(!ready.isEmpty(), launcher.readAllStandardError().constData());
    const QString sessionId = ready.value(QStringLiteral("launcherSession")).toString();
    const QStringList paths = registryPaths(sessionId);
    QFile file(paths.first());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonObject metadata = parseObject(file.readAll());
    file.close();
    metadata.insert(QStringLiteral("launcherPid"), UnobservablePid);
    QVERIFY(writeRegistry(paths, metadata));

    const ProcessResult sessions = ctl({ QStringLiteral("sessions") });
    QCOMPARE(sessions.exitCode, 0);
    const QJsonArray entries = parseObject(sessions.output).value(QStringLiteral("sessions")).toArray();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().toObject().value(QStringLiteral("controlReachable")).toBool(), true);
    const QString selector = QStringLiteral("objectName=\"smoke.content\"");
    const QJsonObject query = useMcp
            ? mcp(QStringLiteral("qmlagent_ui_query"), {
                { QStringLiteral("session"), sessionId },
                { QStringLiteral("selector"), selector },
                { QStringLiteral("properties"), QJsonArray{ QStringLiteral("visible") } },
                { QStringLiteral("verbosity"), QStringLiteral("full") },
              }).value(QStringLiteral("structuredContent")).toObject()
            : parseObject(ctl({ QStringLiteral("query"), selector, QStringLiteral("--session"),
                                sessionId, QStringLiteral("--property"), QStringLiteral("visible") })
                                  .output).value(QStringLiteral("result")).toObject();
    const QJsonArray matches = query.value(QStringLiteral("matches")).toArray();
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().toObject().value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("visible")).toBool(), true);
    verifyRegistry(paths, jsonLine(metadata));
    QCOMPARE(ctl({ QStringLiteral("stop"), QStringLiteral("--session"), sessionId }).exitCode, 0);
}

void QmlAgentLauncherTest::unreachableMailboxRetainsRegistry_data()
{
    QTest::addColumn<bool>("createMailbox");
    QTest::newRow("missing") << false;
    QTest::newRow("unresponsive") << true;
}

void QmlAgentLauncherTest::unreachableMailboxRetainsRegistry()
{
    QFETCH(bool, createMailbox);
    const QString mailbox = QDir(m_root->path()).filePath(QStringLiteral("mailbox"));
    if (createMailbox)
        QVERIFY(QDir().mkpath(mailbox));
    const QStringList paths = registryPaths(QStringLiteral("unobservable"));
    const QJsonObject metadata{
        { QStringLiteral("launcherSession"), QStringLiteral("unobservable") },
        { QStringLiteral("launcherPid"), UnobservablePid },
        { QStringLiteral("controlMailbox"), mailbox },
    };
    QVERIFY(writeRegistry(paths, metadata));
    const ProcessResult result = ctl({ QStringLiteral("sessions") });
    QCOMPARE(result.exitCode, 0);
    const QJsonArray sessions = parseObject(result.output).value(QStringLiteral("sessions")).toArray();
    QCOMPARE(sessions.size(), 1);
    const QJsonObject status = sessions.first().toObject();
    QCOMPARE(status.value(QStringLiteral("controlReachable")).toBool(true), false);
    QVERIFY(!status.value(QStringLiteral("controlError")).toString().isEmpty());
    const QJsonObject failure = mcp(QStringLiteral("qmlagent_ui_query"), {
        { QStringLiteral("session"), QStringLiteral("unobservable") },
        { QStringLiteral("selector"), QStringLiteral("id=\"root\"") },
    });
    QCOMPARE(failure.value(QStringLiteral("isError")).toBool(), true);
    QVERIFY(failure.value(QStringLiteral("structuredContent")).toObject()
                    .value(QStringLiteral("error")).toString().contains(QStringLiteral("retained")));
    verifyRegistry(paths, jsonLine(metadata));
    QCOMPARE(QFileInfo::exists(mailbox), createMailbox);
}

void QmlAgentLauncherTest::stopDoesNotSignalRegistryPids_data()
{
    reachableMailboxIgnoresRegistryPid_data();
}

void QmlAgentLauncherTest::stopDoesNotSignalRegistryPids()
{
    QFETCH(bool, useMcp);
    OwnedProcess unrelated;
    configure(&unrelated);
    unrelated.start(QString::fromLocal8Bit(TEST_QMLAGENTSMOKE_EXECUTABLE), {});
    QVERIFY(unrelated.waitForStarted(ProcessTimeoutMs));
    const QStringList paths = registryPaths(QStringLiteral("foreign-pid"));
    const QJsonObject metadata{
        { QStringLiteral("launcherSession"), QStringLiteral("foreign-pid") },
        { QStringLiteral("launcherPid"), qint64(unrelated.processId()) },
        { QStringLiteral("targetPid"), qint64(unrelated.processId()) },
        { QStringLiteral("controlMailbox"), QDir(m_root->path()).filePath(QStringLiteral("absent")) },
    };
    QVERIFY(writeRegistry(paths, metadata));
    if (useMcp) {
        const QJsonObject result = mcp(QStringLiteral("qmlagent_launcher_stop"), {
            { QStringLiteral("session"), QStringLiteral("foreign-pid") },
        });
        QCOMPARE(result.value(QStringLiteral("isError")).toBool(), true);
    } else {
        const ProcessResult result = ctl({ QStringLiteral("stop"), QStringLiteral("--session"),
                                           QStringLiteral("foreign-pid") });
        QCOMPARE(result.exitCode, 1);
        QVERIFY(result.error.contains("retained"));
    }
    QCOMPARE(unrelated.waitForFinished(100), false);
    QCOMPARE(unrelated.state(), QProcess::Running);
    verifyRegistry(paths, jsonLine(metadata));
}

void QmlAgentLauncherTest::sessionsExposeHealthCwdAndNamespace()
{
    if (!canBindUnixSocket())
        QSKIP("This environment does not permit Unix domain socket listens");
    QTemporaryDir firstWorkingDir;
    QTemporaryDir secondWorkingDir;
    QVERIFY(firstWorkingDir.isValid());
    QVERIFY(secondWorkingDir.isValid());

    OwnedProcess first;
    OwnedProcess second;
    const QJsonObject firstReady = launch(&first, firstWorkingDir.path());
    const QJsonObject secondReady = launch(&second, secondWorkingDir.path());
    QVERIFY2(!firstReady.isEmpty(), first.readAllStandardError().constData());
    QVERIFY2(!secondReady.isEmpty(), second.readAllStandardError().constData());

    const ProcessResult result = ctl({ QStringLiteral("sessions") });
    QCOMPARE(result.exitCode, 0);
    const QJsonArray sessions = parseObject(result.output).value(QStringLiteral("sessions")).toArray();
    QCOMPARE(sessions.size(), 2);
    QStringList cwds;
    for (const QJsonValue &value : sessions) {
        const QJsonObject session = value.toObject();
        QCOMPARE(session.value(QStringLiteral("sessionReady")).toBool(), true);
        QCOMPARE(session.value(QStringLiteral("targetRunning")).toBool(), true);
        QVERIFY(!session.value(QStringLiteral("launcherCwd")).toString().isEmpty());
        cwds.append(session.value(QStringLiteral("launcherCwd")).toString());
    }
    QVERIFY(cwds.contains(firstWorkingDir.path()));
    QVERIFY(cwds.contains(secondWorkingDir.path()));

    const QJsonObject status = mcp(QStringLiteral("qmlagent_target_status"), {})
            .value(QStringLiteral("structuredContent")).toObject();
    const QJsonObject gateway = status.value(QStringLiteral("launcherGateway")).toObject();
#if defined(Q_OS_LINUX)
    const QJsonObject pidNamespace = gateway.value(QStringLiteral("pidNamespace")).toObject();
    QVERIFY(!pidNamespace.isEmpty());
    QVERIFY(!pidNamespace.value(QStringLiteral("client")).toString().isEmpty());
    QVERIFY(pidNamespace.value(QStringLiteral("sameNamespace")).isNull());
    QVERIFY(pidNamespace.value(QStringLiteral("launcher")).isNull());
#else
    QVERIFY(!gateway.contains(QStringLiteral("pidNamespace")));
#endif
    QVERIFY(!gateway.contains(QStringLiteral("pidNamespaceHint")));
    QCOMPARE(gateway.value(QStringLiteral("reachableSessionCount")).toInt(), 2);
    const QString firstId = firstReady.value(QStringLiteral("launcherSession")).toString();
    QCOMPARE(ctl({ QStringLiteral("stop"), QStringLiteral("--session"), firstId }).exitCode, 0);
    QVERIFY(first.waitForFinished(ProcessTimeoutMs));
    QCOMPARE(second.state(), QProcess::Running);
    const QJsonArray remaining = parseObject(ctl({ QStringLiteral("sessions") }).output)
                                        .value(QStringLiteral("sessions")).toArray();
    QCOMPARE(remaining.size(), 1);
    QCOMPARE(remaining.first().toObject().value(QStringLiteral("launcherSession")).toString(),
             secondReady.value(QStringLiteral("launcherSession")).toString());
}

QTEST_GUILESS_MAIN(QmlAgentLauncherTest)
#include "tst_qmlagentlauncher.moc"
