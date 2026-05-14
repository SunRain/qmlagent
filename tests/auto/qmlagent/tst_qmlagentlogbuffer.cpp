// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qqmlagentlogbuffer_p.h"

#include <QtCore/qjsondocument.h>
#include <QtTest/qtest.h>

class tst_QQmlAgentLogBuffer : public QObject
{
    Q_OBJECT

private slots:
    void enforcesEntryLimit();
    void enforcesByteLimit();
    void evictsDedupKeys();
};

static QJsonObject entry(const QString &text)
{
    return {
        { QStringLiteral("level"), QStringLiteral("warning") },
        { QStringLiteral("category"), QStringLiteral("qml") },
        { QStringLiteral("text"), text },
    };
}

void tst_QQmlAgentLogBuffer::enforcesEntryLimit()
{
    QQmlAgentLogBuffer buffer;
    for (qsizetype i = 0; i < QQmlAgentLogBuffer::maxEntries + 20; ++i)
        QVERIFY(buffer.append(QString::number(i), entry(QString::number(i))));

    QCOMPARE(buffer.size(), QQmlAgentLogBuffer::maxEntries);
}

void tst_QQmlAgentLogBuffer::enforcesByteLimit()
{
    QQmlAgentLogBuffer buffer;
    const QString largeText(256 * 1024, QLatin1Char('x'));

    for (int i = 0; i < 10; ++i)
        QVERIFY(buffer.append(QString::number(i), entry(largeText + QString::number(i))));

    QVERIFY(buffer.byteSize() <= QQmlAgentLogBuffer::maxBytes);
}

void tst_QQmlAgentLogBuffer::evictsDedupKeys()
{
    QQmlAgentLogBuffer buffer;
    QVERIFY(buffer.append(QStringLiteral("old"), entry(QStringLiteral("old"))));
    QVERIFY(buffer.containsKey(QStringLiteral("old")));

    const QString largeText(2 * 1024 * 1024, QLatin1Char('x'));
    QVERIFY(buffer.append(QStringLiteral("large"), entry(largeText)));

    QVERIFY(!buffer.containsKey(QStringLiteral("old")));
    QVERIFY(buffer.append(QStringLiteral("old"), entry(QStringLiteral("old"))));
}

QTEST_GUILESS_MAIN(tst_QQmlAgentLogBuffer)

#include "tst_qmlagentlogbuffer.moc"
