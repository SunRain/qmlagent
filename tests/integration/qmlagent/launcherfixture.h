// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef LAUNCHERFIXTURE_H
#define LAUNCHERFIXTURE_H

#include <QtCore/qjsonobject.h>
#include <QtCore/qprocess.h>
#include <QtCore/qtemporarydir.h>

#include <limits>
#include <memory>

constexpr int ProcessTimeoutMs = 5000;
constexpr qint64 UnobservablePid = std::numeric_limits<int>::max();

struct ProcessResult
{
    int exitCode = -1;
    QByteArray output;
    QByteArray error;
};

class OwnedProcess : public QProcess
{
public:
    ~OwnedProcess() override
    {
        if (state() == NotRunning)
            return;
        terminate();
        if (!waitForFinished(ProcessTimeoutMs)) {
            kill();
            waitForFinished(ProcessTimeoutMs);
        }
    }
};

QJsonObject parseObject(const QByteArray &bytes);
QByteArray jsonLine(const QJsonObject &object);

class LauncherTestFixture
{
public:
    bool initialize();
    void configure(QProcess *process) const;
    ProcessResult run(const QString &program, const QStringList &arguments,
                      const QByteArray &input = {}) const;
    ProcessResult ctl(const QStringList &arguments) const;
    QJsonObject mcp(const QString &tool, const QJsonObject &arguments) const;
    QStringList registryPaths(const QString &sessionId) const;
    bool writeRegistry(const QStringList &paths, const QJsonObject &metadata) const;
    void verifyRegistry(const QStringList &paths, const QByteArray &expected) const;
    QJsonObject launch(OwnedProcess *launcher, const QString &workingDirectory = {}) const;
    QString currentPidNamespace() const;
    bool canBindUnixSocket() const;

protected:
    std::unique_ptr<QTemporaryDir> m_root;
    QProcessEnvironment m_environment;
};

#endif // LAUNCHERFIXTURE_H
