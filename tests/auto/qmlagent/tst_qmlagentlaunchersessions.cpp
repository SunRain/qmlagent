// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qmlagentlauncher.h"
#include "qmlagentprocess.h"

#include <QtCore/qcoreapplication.h>
#include <QtTest/qtest.h>

class QmlAgentLauncherSessionsTest : public QObject
{
    Q_OBJECT

private slots:
    void procHelpersObserveOwnProcess();
    void summariesExposeTargetHealth();
    void summariesKeepUnknownTargetHealthUnreachable();
    void namespaceDiagnostics_data();
    void namespaceDiagnostics();
};

void QmlAgentLauncherSessionsTest::procHelpersObserveOwnProcess()
{
#if !defined(Q_OS_LINUX)
    QSKIP("Process identity helpers require Linux /proc");
#endif
    QVERIFY(qmlagentProcessProcAvailable());
    const qint64 pid = QCoreApplication::applicationPid();
    const QString cmdline = qmlagentReadProcessCmdline(pid);
    QVERIFY(!cmdline.isEmpty());
    QVERIFY(!cmdline.contains(QStringLiteral("qmlagent-launcher")));

    const QString pidNamespace = qmlagentReadPidNamespace(pid);
    QVERIFY(!pidNamespace.isEmpty());
    QVERIFY(pidNamespace.startsWith(QStringLiteral("pid:[")));
    QCOMPARE(qmlagentClientPidNamespace(), pidNamespace);

    QCOMPARE(qmlagentReadProcessCmdline(-1), QString());
    QCOMPARE(qmlagentReadPidNamespace(0), QString());
    QCOMPARE(qmlagentLocalProcessDefinitelyAbsent(pid), false);
}

void QmlAgentLauncherSessionsTest::namespaceDiagnostics_data()
{
    QTest::addColumn<QString>("launcher");
    QTest::addColumn<QString>("client");
    QTest::addColumn<QJsonValue>("sameNamespace");
    QTest::newRow("same") << QStringLiteral("pid:[1]") << QStringLiteral("pid:[1]") << QJsonValue(true);
    QTest::newRow("different") << QStringLiteral("pid:[1]") << QStringLiteral("pid:[2]") << QJsonValue(false);
    QTest::newRow("launcher-read-failed") << QString() << QStringLiteral("pid:[1]") << QJsonValue(QJsonValue::Null);
    QTest::newRow("client-read-failed") << QStringLiteral("pid:[1]") << QString() << QJsonValue(QJsonValue::Null);
    QTest::newRow("neither-known") << QString() << QString() << QJsonValue(QJsonValue::Null);
}

void QmlAgentLauncherSessionsTest::namespaceDiagnostics()
{
    QFETCH(QString, launcher);
    QFETCH(QString, client);
    QFETCH(QJsonValue, sameNamespace);
    const QJsonValue metadataNamespace = qmlagentPidNamespaceValue(launcher);
    QCOMPARE(metadataNamespace.isNull(), launcher.isEmpty());
    QJsonObject gateway;
    qmlagentInsertPidNamespace(gateway, metadataNamespace, client);
#if defined(Q_OS_LINUX)
    QVERIFY(gateway.contains(QStringLiteral("pidNamespace")));
    const QJsonObject ns = gateway.value(QStringLiteral("pidNamespace")).toObject();
    QCOMPARE(ns.value(QStringLiteral("launcher")), metadataNamespace);
    QCOMPARE(ns.value(QStringLiteral("client")), qmlagentPidNamespaceValue(client));
    QCOMPARE(ns.value(QStringLiteral("sameNamespace")), sameNamespace);
    QCOMPARE(gateway.contains(QStringLiteral("pidNamespaceHint")), sameNamespace == QJsonValue(false));
#else
    QVERIFY(!gateway.contains(QStringLiteral("pidNamespace")));
    QVERIFY(!gateway.contains(QStringLiteral("pidNamespaceHint")));
#endif
}

void QmlAgentLauncherSessionsTest::summariesExposeTargetHealth()
{
    LauncherSession session;
    session.id = QStringLiteral("healthy");
    session.metadata = {
        { QStringLiteral("launcherCwd"), QStringLiteral("/tmp/project-a") },
    };
    session.status = {
        { QStringLiteral("controlReachable"), true },
        { QStringLiteral("sessionReady"), true },
        { QStringLiteral("targetRunning"), true },
        { QStringLiteral("launcherCwd"), QStringLiteral("/tmp/project-b") },
    };

    const QJsonObject summary = launcherSessionSummaries({ session }).first().toObject();
    QCOMPARE(summary.value(QStringLiteral("sessionReady")).toBool(), true);
    QCOMPARE(summary.value(QStringLiteral("targetRunning")).toBool(), true);
    QCOMPARE(summary.value(QStringLiteral("launcherCwd")).toString(), QStringLiteral("/tmp/project-b"));
}

void QmlAgentLauncherSessionsTest::summariesKeepUnknownTargetHealthUnreachable()
{
    LauncherSession session;
    session.id = QStringLiteral("unreachable");
    session.metadata = {
        { QStringLiteral("launcherCwd"), QStringLiteral("/tmp/project-a") },
    };
    session.status = {
        { QStringLiteral("controlReachable"), false },
        { QStringLiteral("controlError"), QStringLiteral("mailbox unavailable") },
    };

    const QJsonObject summary = launcherSessionSummaries({ session }).first().toObject();
    QVERIFY(summary.contains(QStringLiteral("sessionReady")));
    QVERIFY(summary.value(QStringLiteral("sessionReady")).isNull());
    QVERIFY(summary.contains(QStringLiteral("targetRunning")));
    QVERIFY(summary.value(QStringLiteral("targetRunning")).isNull());
    QCOMPARE(summary.value(QStringLiteral("launcherCwd")).toString(), QStringLiteral("/tmp/project-a"));
}

QTEST_GUILESS_MAIN(QmlAgentLauncherSessionsTest)
#include "tst_qmlagentlaunchersessions.moc"
