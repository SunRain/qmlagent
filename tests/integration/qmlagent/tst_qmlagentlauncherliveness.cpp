// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "launcherfixture.h"

#include <QtCore/qdir.h>
#include <QtCore/qelapsedtimer.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qsavefile.h>
#include <QtTest/qtest.h>

class QmlAgentLauncherLivenessTest : public QObject, public LauncherTestFixture
{
    Q_OBJECT

private slots:
    void init() { QVERIFY(initialize()); }
    void sameNamespacePidReuseRemovesStaleRegistry();
    void foreignPidNamespaceRetainsUnreachableRegistry();
    void deadPidCleanedByEveryEntryPoint_data();
    void deadPidCleanedByEveryEntryPoint();
    void cleanupUsesRecordedRuntimeAcrossTmpdirs();
    void unsafeRuntimeRetainsRegistry_data();
    void unsafeRuntimeRetainsRegistry();
    void cleanupFailureRemainsVisible_data();
    void cleanupFailureRemainsVisible();
    void duplicateRegistryMismatchRetainsBothRecords();
    void unreadableRuntimeRetainsRegistry();

private:
    QJsonObject metadata(const QString &id, qint64 pid, const QString &base = {}) const;
    bool createMailbox(const QJsonObject &record) const;
    static bool writeMarker(const QString &path);

};

void QmlAgentLauncherLivenessTest::sameNamespacePidReuseRemovesStaleRegistry()
{
#if !defined(Q_OS_LINUX)
    QSKIP("PID reuse verification requires Linux /proc");
#endif
    // A live unrelated process proves the PID is reused, not merely gone.
    OwnedProcess unrelated;
    configure(&unrelated);
    unrelated.start(QString::fromLocal8Bit(TEST_QMLAGENTSMOKE_EXECUTABLE), {});
    QVERIFY(unrelated.waitForStarted(ProcessTimeoutMs));

    const QStringList paths = registryPaths(QStringLiteral("pid-reuse"));
    const QJsonObject metadata = this->metadata(QStringLiteral("pid-reuse"), unrelated.processId());
    QVERIFY(createMailbox(metadata));
    QVERIFY(writeRegistry(paths, metadata));

    QElapsedTimer timer;
    timer.start();
    const ProcessResult result = ctl({ QStringLiteral("sessions") });
    const qint64 elapsedMs = timer.elapsed();
    qInfo("stale mailbox discovery took %lldms", static_cast<long long>(elapsedMs));
    QCOMPARE(result.exitCode, 0);
    QVERIFY2(elapsedMs < 50, qPrintable(QStringLiteral("discovery took %1ms").arg(elapsedMs)));

    const QJsonObject payload = parseObject(result.output);
    QCOMPARE(payload.value(QStringLiteral("sessionCount")).toInt(), 0);
    QCOMPARE(payload.value(QStringLiteral("staleRemoved")).toInt(), 1);
    const QJsonArray removals = payload.value(QStringLiteral("staleRemovals")).toArray();
    QCOMPARE(removals.size(), 1);
    QCOMPARE(removals.first().toObject().value(QStringLiteral("reason")).toString(),
             QStringLiteral("pid_reuse_verified"));
    for (const QString &path : paths)
        QVERIFY2(!QFileInfo::exists(path), qPrintable(path));
    QCOMPARE(unrelated.state(), QProcess::Running);
}

void QmlAgentLauncherLivenessTest::foreignPidNamespaceRetainsUnreachableRegistry()
{
    const QStringList paths = registryPaths(QStringLiteral("foreign-namespace"));
    const QJsonObject metadata{
        { QStringLiteral("launcherSession"), QStringLiteral("foreign-namespace") },
        { QStringLiteral("launcherPid"), qint64(QCoreApplication::applicationPid()) },
        { QStringLiteral("pidNamespace"), QStringLiteral("pid:[4026531837]") },
        { QStringLiteral("controlMailbox"), QDir(m_root->path()).filePath(QStringLiteral("foreign-mailbox")) },
    };
    QVERIFY(writeRegistry(paths, metadata));

    const ProcessResult result = ctl({ QStringLiteral("sessions") });
    QCOMPARE(result.exitCode, 0);
    const QJsonObject payload = parseObject(result.output);
    QCOMPARE(payload.value(QStringLiteral("staleRemoved")).toInt(), 0);
    QCOMPARE(payload.value(QStringLiteral("sessionCount")).toInt(), 1);
    const QJsonObject session = payload.value(QStringLiteral("sessions")).toArray().first().toObject();
    QCOMPARE(session.value(QStringLiteral("controlReachable")).toBool(true), false);
    verifyRegistry(paths, jsonLine(metadata));
}

QJsonObject QmlAgentLauncherLivenessTest::metadata(const QString &id, qint64 pid,
                                                  const QString &base) const
{
    const QString root = base.isEmpty() ? QDir(m_root->path()).filePath(QStringLiteral("tmp")) : base;
    const QString mailbox = QDir(root).filePath(QStringLiteral("QmlAgent/qmlagent-%1/control-%2")
                                                      .arg(pid).arg(id.left(8)));
    return {
        { QStringLiteral("launcherSession"), id },
        { QStringLiteral("launcherPid"), pid },
        { QStringLiteral("launcherCwd"), m_root->path() },
        { QStringLiteral("pidNamespace"), currentPidNamespace() },
        { QStringLiteral("controlMailbox"), mailbox },
        { QStringLiteral("controlEndpoint"), QJsonObject{
            { QStringLiteral("kind"), QStringLiteral("fileMailbox") },
            { QStringLiteral("path"), mailbox },
        } },
    };
}

bool QmlAgentLauncherLivenessTest::createMailbox(const QJsonObject &record) const
{
    return QDir().mkpath(record.value(QStringLiteral("controlMailbox")).toString());
}

bool QmlAgentLauncherLivenessTest::writeMarker(const QString &path)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("must survive") > 0 && file.commit();
}

void QmlAgentLauncherLivenessTest::deadPidCleanedByEveryEntryPoint_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::newRow("sessions") << QStringList{ QStringLiteral("sessions") };
    QTest::newRow("status") << QStringList{ QStringLiteral("status") };
    QTest::newRow("stop") << QStringList{ QStringLiteral("stop") };
    QTest::newRow("reload-preview") << QStringList{ QStringLiteral("reload-preview") };
    QTest::newRow("query") << QStringList{ QStringLiteral("query"), QStringLiteral("id=\"root\"") };
    QTest::newRow("call") << QStringList{ QStringLiteral("call"), QStringLiteral("Session.getInfo") };
}

void QmlAgentLauncherLivenessTest::deadPidCleanedByEveryEntryPoint()
{
#if !defined(Q_OS_LINUX)
    QSKIP("Dead PID verification requires Linux /proc and a matching namespace");
#endif
    QFETCH(QStringList, arguments);
    const QJsonObject record = metadata(QStringLiteral("dead-pid-session"), UnobservablePid);
    const QStringList paths = registryPaths(QStringLiteral("dead-pid-session"));
    QVERIFY(createMailbox(record));
    QVERIFY(writeRegistry(paths, record));
    QElapsedTimer timer;
    timer.start();
    const ProcessResult result = ctl(arguments);
    const qint64 elapsed = timer.elapsed();
    const bool listing = arguments.first() == QLatin1String("sessions");
    QCOMPARE(result.exitCode, listing ? 0 : 1);
    QVERIFY2(elapsed < 50, qPrintable(QStringLiteral("dead PID discovery took %1ms").arg(elapsed)));
    for (const QString &path : paths)
        QVERIFY2(!QFileInfo::exists(path), qPrintable(path));
    QVERIFY(!QFileInfo::exists(QFileInfo(record.value(QStringLiteral("controlMailbox")).toString()).absolutePath()));
    if (listing) {
        const QJsonObject report = parseObject(result.output);
        QCOMPARE(report.value(QStringLiteral("staleRemoved")).toInt(), 1);
        QCOMPARE(report.value(QStringLiteral("staleRemovals")).toArray().first().toObject()
                         .value(QStringLiteral("reason")).toString(), QStringLiteral("pid_absent_verified"));
    }
}

void QmlAgentLauncherLivenessTest::cleanupUsesRecordedRuntimeAcrossTmpdirs()
{
#if !defined(Q_OS_LINUX)
    QSKIP("Stale cleanup requires Linux /proc");
#endif
    const QString id = QStringLiteral("different-tmpdir");
    const qint64 pid = QCoreApplication::applicationPid();
    const QString otherTmp = QDir(m_root->path()).filePath(QStringLiteral("other-tmp"));
    const QJsonObject record = metadata(id, pid, otherTmp);
    const QJsonObject localRecord = metadata(id, pid);
    QVERIFY(createMailbox(record));
    QVERIFY(createMailbox(localRecord));
    const QString localRuntime = QFileInfo(localRecord.value(QStringLiteral("controlMailbox")).toString()).absolutePath();
    const QString marker = QDir(localRuntime).filePath(QStringLiteral("unrelated-marker"));
    QVERIFY(writeMarker(marker));
    QStringList paths = registryPaths(id);
    paths.append(QDir(otherTmp).filePath(QStringLiteral("QmlAgent/launcher-sessions/") + id + QStringLiteral(".json")));
    QVERIFY(writeRegistry(paths, record));
    const ProcessResult result = ctl({ QStringLiteral("sessions") });
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(parseObject(result.output).value(QStringLiteral("staleRemoved")).toInt(), 1);
    QVERIFY(QFileInfo::exists(marker));
    QVERIFY(!QFileInfo::exists(QFileInfo(record.value(QStringLiteral("controlMailbox")).toString()).absolutePath()));
    for (const QString &path : paths)
        QVERIFY2(!QFileInfo::exists(path), qPrintable(path));
}

void QmlAgentLauncherLivenessTest::unsafeRuntimeRetainsRegistry_data()
{
    QTest::addColumn<QString>("condition");
    QTest::newRow("runtime-symlink") << QStringLiteral("symlink");
    QTest::newRow("unowned-entry") << QStringLiteral("entry");
    QTest::newRow("conflicting-endpoint") << QStringLiteral("endpoint");
    QTest::newRow("mailbox-symlink") << QStringLiteral("mailbox-link");
}

void QmlAgentLauncherLivenessTest::unsafeRuntimeRetainsRegistry()
{
#if !defined(Q_OS_LINUX)
    QSKIP("Stale cleanup requires Linux /proc");
#endif
    QFETCH(QString, condition);
    const QString id = QStringLiteral("unsafe-runtime");
    QJsonObject record = metadata(id, QCoreApplication::applicationPid());
    const QString runtime = QFileInfo(record.value(QStringLiteral("controlMailbox")).toString()).absolutePath();
    const QString outside = QDir(m_root->path()).filePath(QStringLiteral("outside"));
    QVERIFY(QDir().mkpath(outside));
    const QString marker = QDir(outside).filePath(QStringLiteral("marker"));
    QVERIFY(writeMarker(marker));
    if (condition == QLatin1String("symlink")) {
        QVERIFY(QDir().mkpath(QFileInfo(runtime).absolutePath()));
        QVERIFY(QFile::link(outside, runtime));
    } else if (condition == QLatin1String("mailbox-link")) {
        QVERIFY(QDir().mkpath(runtime));
        QVERIFY(QFile::link(outside, record.value(QStringLiteral("controlMailbox")).toString()));
    } else {
        QVERIFY(createMailbox(record));
        if (condition == QLatin1String("entry"))
            QVERIFY(writeMarker(QDir(runtime).filePath(QStringLiteral("unowned-marker"))));
        else
            record.insert(QStringLiteral("controlEndpoint"), QJsonObject{
                { QStringLiteral("kind"), QStringLiteral("fileMailbox") },
                { QStringLiteral("path"), outside },
            });
    }
    const QStringList paths = registryPaths(id);
    QVERIFY(writeRegistry(paths, record));
    const ProcessResult result = ctl({ QStringLiteral("sessions") });
    const QJsonObject report = parseObject(result.output);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(report.value(QStringLiteral("staleRemoved")).toInt(), 0);
    QCOMPARE(report.value(QStringLiteral("sessionCount")).toInt(), 1);
    QCOMPARE(report.value(QStringLiteral("cleanupFailures")).toArray().size(), 1);
    QVERIFY(QFileInfo::exists(marker));
    QVERIFY(QFileInfo::exists(runtime));
    verifyRegistry(paths, jsonLine(record));
}

void QmlAgentLauncherLivenessTest::duplicateRegistryMismatchRetainsBothRecords()
{
#if !defined(Q_OS_LINUX)
    QSKIP("Stale cleanup requires Linux /proc");
#endif
    const QString id = QStringLiteral("conflicting-records");
    const QJsonObject record = metadata(id, QCoreApplication::applicationPid());
    const QStringList paths = registryPaths(id);
    QVERIFY(createMailbox(record));
    QVERIFY(writeRegistry(paths, record));
    QJsonObject otherRecord = record;
    otherRecord.insert(QStringLiteral("launcherPid"), UnobservablePid);
    QVERIFY(writeRegistry({ paths.last() }, otherRecord));
    const QJsonObject report = parseObject(ctl({ QStringLiteral("sessions") }).output);
    QCOMPARE(report.value(QStringLiteral("staleRemoved")).toInt(), 0);
    QCOMPARE(report.value(QStringLiteral("sessionCount")).toInt(), 1);
    QCOMPARE(report.value(QStringLiteral("cleanupFailures")).toArray().size(), 1);
    verifyRegistry({ paths.first(), paths.at(1) }, jsonLine(record));
    verifyRegistry({ paths.last() }, jsonLine(otherRecord));
    QVERIFY(QFileInfo::exists(record.value(QStringLiteral("controlMailbox")).toString()));
}

void QmlAgentLauncherLivenessTest::unreadableRuntimeRetainsRegistry()
{
#if !defined(Q_OS_LINUX)
    QSKIP("Stale cleanup requires Linux /proc");
#endif
    const QString id = QStringLiteral("unreadable-runtime");
    const QJsonObject record = metadata(id, QCoreApplication::applicationPid());
    const QStringList paths = registryPaths(id);
    QVERIFY(createMailbox(record));
    QVERIFY(writeRegistry(paths, record));
    const QString runtime = QFileInfo(record.value(QStringLiteral("controlMailbox")).toString()).absolutePath();
    const auto permissions = QFileInfo(runtime).permissions();
    QVERIFY(QFile::setPermissions(runtime, {}));
    const ProcessResult result = ctl({ QStringLiteral("sessions") });
    const bool restored = QFile::setPermissions(runtime, permissions);
    QVERIFY(restored);
    const QJsonObject report = parseObject(result.output);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(report.value(QStringLiteral("staleRemoved")).toInt(), 0);
    QCOMPARE(report.value(QStringLiteral("sessionCount")).toInt(), 1);
    QCOMPARE(report.value(QStringLiteral("cleanupFailures")).toArray().size(), 1);
    verifyRegistry(paths, jsonLine(record));
    QVERIFY(QFileInfo::exists(runtime));
}

void QmlAgentLauncherLivenessTest::cleanupFailureRemainsVisible_data()
{
    QTest::addColumn<bool>("registryFailure");
    QTest::newRow("runtime-read-only") << false;
    QTest::newRow("partial-registry-failure") << true;
}

void QmlAgentLauncherLivenessTest::cleanupFailureRemainsVisible()
{
#if !defined(Q_OS_LINUX)
    QSKIP("Stale cleanup requires Linux /proc");
#endif
    QFETCH(bool, registryFailure);
    const QString id = QStringLiteral("cleanup-failure");
    const QJsonObject record = metadata(id, QCoreApplication::applicationPid(), registryFailure
            ? QString() : QDir(m_root->path()).filePath(QStringLiteral("other-tmp")));
    const QStringList paths = registryPaths(id);
    QVERIFY(createMailbox(record));
    QVERIFY(writeRegistry(paths, record));
    const QString blockedPath = registryFailure ? QFileInfo(paths.first()).absolutePath()
            : QFileInfo(QFileInfo(record.value(QStringLiteral("controlMailbox")).toString()).absolutePath()).absolutePath();
    const auto permissions = QFileInfo(blockedPath).permissions();
    QVERIFY(QFile::setPermissions(blockedPath, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const ProcessResult result = ctl({ QStringLiteral("sessions") });
    const bool restored = QFile::setPermissions(blockedPath, permissions);
    QVERIFY(restored);
    const QJsonObject report = parseObject(result.output);
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(report.value(QStringLiteral("staleRemoved")).toInt(), 0);
    QCOMPARE(report.value(QStringLiteral("sessionCount")).toInt(), 1);
    const QJsonArray failures = report.value(QStringLiteral("cleanupFailures")).toArray();
    QCOMPARE(failures.size(), 1);
    const QJsonObject failure = failures.first().toObject();
    QCOMPARE(failure.value(QStringLiteral("complete")).toBool(true), false);
    QVERIFY(!failure.value(QStringLiteral("errors")).toArray().isEmpty());
    QCOMPARE(failure.value(QStringLiteral("moved")).toArray().isEmpty(), !registryFailure);
    QVERIFY(QFileInfo::exists(paths.first()));
    const QJsonObject session = report.value(QStringLiteral("sessions")).toArray().first().toObject();
    QCOMPARE(session.value(QStringLiteral("cleanupError")).toObject(), failure);
    // A partial cleanup is still discoverable, including on the next routing attempt.
    if (registryFailure) {
        const ProcessResult retry = ctl({ QStringLiteral("stop") });
        QCOMPARE(retry.exitCode, 1);
        QVERIFY(!QFileInfo::exists(paths.first()));
    }
}

QTEST_GUILESS_MAIN(QmlAgentLauncherLivenessTest)
#include "tst_qmlagentlauncherliveness.moc"
