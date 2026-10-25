// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "launchermailboxfixture.h"

#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qjsonarray.h>
#include <QtTest/qtest.h>

class QmlAgentLauncherGatewayTest : public QObject, public LauncherTestFixture
{
    Q_OBJECT

private slots:
    void init() { QVERIFY(initialize()); }
    void targetStatusNamespace_data();
    void targetStatusNamespace();
    void reachableMailboxOverridesRegistryEvidence_data();
    void reachableMailboxOverridesRegistryEvidence();
    void targetNotRunningStillRoutesStop();
    void multiSessionNamespaceIsUnselected();
    void emptyGatewayKeepsPlatformNamespaceContract();

private:
    QJsonObject registry(const QString &id, const QString &ns, qint64 pid = UnobservablePid) const;
    bool registerSession(const QJsonObject &record) const;
    static QJsonObject healthyStatus(bool running = true);
    QJsonObject gateway() const;
    void verifyNamespace(const QJsonObject &object, const QString &launcher) const;
};

QJsonObject QmlAgentLauncherGatewayTest::registry(const QString &id, const QString &ns, qint64 pid) const
{
    const QString mailbox = QDir(m_root->path()).filePath(
            QStringLiteral("tmp/QmlAgent/qmlagent-%1/control-%2").arg(pid).arg(id.left(8)));
    return {
        { QStringLiteral("launcherSession"), id },
        { QStringLiteral("launcherPid"), pid },
        { QStringLiteral("launcherCwd"), QDir(m_root->path()).filePath(id) },
        { QStringLiteral("pidNamespace"), ns.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(ns) },
        { QStringLiteral("controlMailbox"), mailbox },
    };
}

bool QmlAgentLauncherGatewayTest::registerSession(const QJsonObject &record) const
{
    return QDir().mkpath(record.value(QStringLiteral("controlMailbox")).toString())
            && writeRegistry(registryPaths(record.value(QStringLiteral("launcherSession")).toString()), record);
}

QJsonObject QmlAgentLauncherGatewayTest::healthyStatus(bool running)
{
    return {
        { QStringLiteral("sessionReady"), true },
        { QStringLiteral("targetRunning"), running },
        { QStringLiteral("sessionType"), QStringLiteral("application") },
    };
}

QJsonObject QmlAgentLauncherGatewayTest::gateway() const
{
    return mcp(QStringLiteral("qmlagent_target_status"), {}).value(QStringLiteral("structuredContent"))
            .toObject().value(QStringLiteral("launcherGateway")).toObject();
}

void QmlAgentLauncherGatewayTest::verifyNamespace(const QJsonObject &object, const QString &launcher) const
{
#if defined(Q_OS_LINUX)
    const QJsonObject ns = object.value(QStringLiteral("pidNamespace")).toObject();
    QVERIFY(object.contains(QStringLiteral("pidNamespace")));
    if (launcher.isEmpty())
        QVERIFY(ns.value(QStringLiteral("launcher")).isNull());
    else
        QCOMPARE(ns.value(QStringLiteral("launcher")).toString(), launcher);
    QCOMPARE(ns.value(QStringLiteral("client")).toString(), currentPidNamespace());
    if (launcher.isEmpty())
        QVERIFY(ns.value(QStringLiteral("sameNamespace")).isNull());
    else
        QCOMPARE(ns.value(QStringLiteral("sameNamespace")).toBool(), launcher == currentPidNamespace());
    QCOMPARE(object.contains(QStringLiteral("pidNamespaceHint")),
             !launcher.isEmpty() && launcher != currentPidNamespace());
#else
    Q_UNUSED(launcher);
    QVERIFY(!object.contains(QStringLiteral("pidNamespace")));
    QVERIFY(!object.contains(QStringLiteral("pidNamespaceHint")));
#endif
}

void QmlAgentLauncherGatewayTest::targetStatusNamespace_data()
{
    QTest::addColumn<QString>("ns");
    QTest::newRow("same") << currentPidNamespace();
    QTest::newRow("different") << QStringLiteral("pid:[foreign]");
    QTest::newRow("launcher-read-failed") << QString();
}

void QmlAgentLauncherGatewayTest::targetStatusNamespace()
{
    QFETCH(QString, ns);
    const QString id = QStringLiteral("namespace-session");
    const QJsonObject record = registry(id, ns);
    QVERIFY(registerSession(record));
    LauncherMailboxFixture mailbox(record.value(QStringLiteral("controlMailbox")).toString(), healthyStatus());
    const QJsonObject report = gateway();
    QCOMPARE(report.value(QStringLiteral("available")).toBool(), true);
    verifyNamespace(report, ns);
    const QJsonObject session = report.value(QStringLiteral("session")).toObject();
    QCOMPARE(session.value(QStringLiteral("launcherSession")).toString(), id);
    QCOMPARE(session.value(QStringLiteral("sessionReady")).toBool(), true);
    QCOMPARE(session.value(QStringLiteral("targetRunning")).toBool(), true);
    QCOMPARE(session.value(QStringLiteral("launcherCwd")), record.value(QStringLiteral("launcherCwd")));
    verifyNamespace(session, ns);
}

void QmlAgentLauncherGatewayTest::reachableMailboxOverridesRegistryEvidence_data()
{
    QTest::addColumn<bool>("rewritePid");
    QTest::addColumn<bool>("useMcp");
    QTest::newRow("stale-pid-cli") << false << false;
    QTest::newRow("stale-pid-mcp") << false << true;
    QTest::newRow("rewritten-pid-cli") << true << false;
    QTest::newRow("rewritten-pid-mcp") << true << true;
}

void QmlAgentLauncherGatewayTest::reachableMailboxOverridesRegistryEvidence()
{
    QFETCH(bool, rewritePid);
    QFETCH(bool, useMcp);
    const QString id = QStringLiteral("rpc-authority-session");
    // Our process is not a launcher. A responsive mailbox must still win,
    // including when registry PID metadata no longer matches its runtime.
    QJsonObject record = registry(id, currentPidNamespace(), QCoreApplication::applicationPid());
    if (rewritePid)
        record.insert(QStringLiteral("launcherPid"), UnobservablePid);
    QVERIFY(registerSession(record));
    LauncherMailboxFixture mailbox(record.value(QStringLiteral("controlMailbox")).toString(), healthyStatus());
    const QJsonObject listing = parseObject(ctl({ QStringLiteral("sessions") }).output);
    QCOMPARE(listing.value(QStringLiteral("staleRemoved")).toInt(), 0);
    QCOMPARE(listing.value(QStringLiteral("sessionCount")).toInt(), 1);
    QCOMPARE(listing.value(QStringLiteral("sessions")).toArray().first().toObject()
                     .value(QStringLiteral("controlReachable")).toBool(), true);
    if (useMcp) {
        const QJsonObject response = mcp(QStringLiteral("qmlagent_ui_query"), {
            { QStringLiteral("session"), id }, { QStringLiteral("selector"), QStringLiteral("id=\"root\"") },
        });
        QCOMPARE(response.value(QStringLiteral("isError")).toBool(), false);
        const QJsonArray matches = response.value(QStringLiteral("structuredContent")).toObject()
                                          .value(QStringLiteral("matches")).toArray();
        QCOMPARE(matches.size(), 1);
        QCOMPARE(matches.first().toObject().value(QStringLiteral("objectName")).toString(),
                 QStringLiteral("fixture.root"));
    } else {
        const ProcessResult response = ctl({ QStringLiteral("call"), QStringLiteral("Session.getInfo"),
                                             QStringLiteral("--session"), id });
        QCOMPARE(response.exitCode, 0);
        QVERIFY(parseObject(response.output).value(QStringLiteral("result")).toObject()
                        .value(QStringLiteral("fixture")).toBool());
    }
    verifyRegistry(registryPaths(id), jsonLine(record));
    QCOMPARE(ctl({ QStringLiteral("stop"), QStringLiteral("--session"), id }).exitCode, 0);
    QVERIFY(mailbox.stopped());
}

void QmlAgentLauncherGatewayTest::targetNotRunningStillRoutesStop()
{
    const QString id = QStringLiteral("not-running-session");
    const QJsonObject record = registry(id, currentPidNamespace());
    QVERIFY(registerSession(record));
    LauncherMailboxFixture mailbox(record.value(QStringLiteral("controlMailbox")).toString(), healthyStatus(false));
    const QJsonObject report = mcp(QStringLiteral("qmlagent_target_status"), {})
                                       .value(QStringLiteral("structuredContent")).toObject();
    const QJsonObject launcherGateway = report.value(QStringLiteral("launcherGateway")).toObject();
    QCOMPARE(launcherGateway.value(QStringLiteral("available")).toBool(), true);
    QCOMPARE(launcherGateway.value(QStringLiteral("session")).toObject()
                     .value(QStringLiteral("targetRunning")).toBool(true), false);
    QVERIFY(report.value(QStringLiteral("nextStep")).toString().contains(QStringLiteral("Rebuild or relaunch")));
    const QJsonObject response = mcp(QStringLiteral("qmlagent_launcher_stop"), {
        { QStringLiteral("session"), id },
    });
    QCOMPARE(response.value(QStringLiteral("isError")).toBool(), false);
    QCOMPARE(response.value(QStringLiteral("structuredContent")).toObject()
                     .value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(mailbox.stopped());
}

void QmlAgentLauncherGatewayTest::multiSessionNamespaceIsUnselected()
{
    const QJsonObject first = registry(QStringLiteral("first-session"), currentPidNamespace());
    const QJsonObject second = registry(QStringLiteral("second-session"), currentPidNamespace());
    QVERIFY(registerSession(first));
    QVERIFY(registerSession(second));
    LauncherMailboxFixture firstMailbox(first.value(QStringLiteral("controlMailbox")).toString(), healthyStatus());
    LauncherMailboxFixture secondMailbox(second.value(QStringLiteral("controlMailbox")).toString(), healthyStatus());
    const QJsonObject report = gateway();
    QCOMPARE(report.value(QStringLiteral("available")).toBool(), false);
    QCOMPARE(report.value(QStringLiteral("reachableSessionCount")).toInt(), 2);
    verifyNamespace(report, {});
    const QJsonArray sessions = report.value(QStringLiteral("sessions")).toArray();
    QCOMPARE(sessions.size(), 2);
    QStringList cwds;
    for (const QJsonValue &session : sessions) {
        verifyNamespace(session.toObject(), currentPidNamespace());
        cwds.append(session.toObject().value(QStringLiteral("launcherCwd")).toString());
    }
    QVERIFY(cwds.contains(first.value(QStringLiteral("launcherCwd")).toString()));
    QVERIFY(cwds.contains(second.value(QStringLiteral("launcherCwd")).toString()));
    QCOMPARE(ctl({ QStringLiteral("stop"), QStringLiteral("--session"), QStringLiteral("first-session") }).exitCode, 0);
    QVERIFY(firstMailbox.stopped());
    QVERIFY(!secondMailbox.stopped());
}

void QmlAgentLauncherGatewayTest::emptyGatewayKeepsPlatformNamespaceContract()
{
    const QJsonObject report = gateway();
    QCOMPARE(report.value(QStringLiteral("reachableSessionCount")).toInt(), 0);
    verifyNamespace(report, {});
}

QTEST_GUILESS_MAIN(QmlAgentLauncherGatewayTest)
#include "tst_qmlagentlaunchergateway.moc"
