// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "launchermailboxfixture.h"

#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qsavefile.h>

LauncherMailboxFixture::LauncherMailboxFixture(const QString &mailbox, const QJsonObject &status)
    : m_mailbox(mailbox), m_status(status)
{
    start();
}

LauncherMailboxFixture::~LauncherMailboxFixture()
{
    requestInterruption();
    wait();
}

void LauncherMailboxFixture::run()
{
    while (!isInterruptionRequested()) {
        const QDir mailbox(m_mailbox);
        for (const QString &entry : mailbox.entryList({ QStringLiteral("request-*.json") }, QDir::Files)) {
            if (!m_seen.contains(entry)) {
                m_seen.insert(entry);
                respond(mailbox.filePath(entry));
            }
        }
        msleep(1);
    }
}

void LauncherMailboxFixture::respond(const QString &path)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly))
        qFatal("Could not read mailbox fixture request");
    const QJsonObject request = parseObject(input.readAll());
    const QString method = request.value(QStringLiteral("method")).toString();
    QJsonObject result;
    if (method == QLatin1String("Session.status")) {
        result = m_status;
    } else if (method == QLatin1String("Session.stop")) {
        m_stopped.store(true);
        result.insert(QStringLiteral("ok"), true);
    } else {
        result.insert(QStringLiteral("result"), QJsonObject{
            { QStringLiteral("fixture"), true },
            { QStringLiteral("matches"), QJsonArray{ QJsonObject{
                { QStringLiteral("nodeId"), 1 },
                { QStringLiteral("objectName"), QStringLiteral("fixture.root") },
            } } },
        });
    }
    QSaveFile output(request.value(QStringLiteral("replyTo")).toString());
    if (!output.open(QIODevice::WriteOnly) || output.write(jsonLine({
            { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
            { QStringLiteral("id"), request.value(QStringLiteral("id")) },
            { QStringLiteral("result"), result },
        })) < 0 || !output.commit())
        qFatal("Could not publish mailbox fixture response");
}
