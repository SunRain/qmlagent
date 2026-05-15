// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qqmlagentlogcollector_p.h"

#include "qqmlagentsourceresolver_p.h"

#include <QtCore/qdatetime.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qlogging.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlerror.h>

QT_BEGIN_NAMESPACE

QQmlAgentLogCollector *QQmlAgentLogCollector::s_activeCollector = nullptr;
QMutex QQmlAgentLogCollector::s_handlerMutex;

static QString levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("debug");
    case QtInfoMsg:
        return QStringLiteral("info");
    case QtWarningMsg:
        return QStringLiteral("warning");
    case QtCriticalMsg:
        return QStringLiteral("error");
    case QtFatalMsg:
        return QStringLiteral("fatal");
    }
    return QStringLiteral("info");
}

QQmlAgentLogCollector::QQmlAgentLogCollector(QObject *parent)
    : QObject(parent)
{
    installMessageHandler();
}

QQmlAgentLogCollector::~QQmlAgentLogCollector()
{
    restoreMessageHandler();
}

void QQmlAgentLogCollector::installMessageHandler()
{
    QMutexLocker locker(&s_handlerMutex);
    if (!s_activeCollector) {
        s_activeCollector = this;
        m_previousHandler = qInstallMessageHandler(QQmlAgentLogCollector::messageHandler);
    }
}

void QQmlAgentLogCollector::restoreMessageHandler()
{
    QMutexLocker locker(&s_handlerMutex);
    if (s_activeCollector != this)
        return;

    QtMessageHandler current = qInstallMessageHandler(m_previousHandler);
    s_activeCollector = nullptr;
    if (current != QQmlAgentLogCollector::messageHandler)
        qInstallMessageHandler(current);
}

void QQmlAgentLogCollector::attachEngine(QQmlEngine *engine)
{
    if (!engine)
        return;
    if (m_engines.contains(engine))
        return;
    m_engines.insert(engine);

    QObject::connect(engine, &QQmlEngine::warnings, this, [this](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            addWarning(warning);
    });
    QObject::connect(engine, &QObject::destroyed, this, [this, engine]() {
        m_engines.remove(engine);
    });
}

void QQmlAgentLogCollector::addWarning(const QQmlError &warning)
{
    QJsonObject entry{
        { QStringLiteral("level"), QStringLiteral("warning") },
        { QStringLiteral("category"), QStringLiteral("qml") },
        { QStringLiteral("text"), warning.description() },
        { QStringLiteral("sourceLocation"), QQmlAgentSourceResolver::sourceLocationForWarning(warning) },
        { QStringLiteral("timestamp"), double(QDateTime::currentMSecsSinceEpoch()) / 1000.0 },
    };
    addEntry(entry);
}

void QQmlAgentLogCollector::captureMessage(
        QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QString category = QString::fromUtf8(context.category ? context.category : "");
    const bool qmlLike = category.contains(QLatin1String("qml"), Qt::CaseInsensitive)
            || message.contains(QLatin1String("ReferenceError:"))
            || message.contains(QLatin1String("TypeError:"))
            || message.contains(QLatin1String("QML "));
    if (!qmlLike && type < QtWarningMsg)
        return;

    QJsonObject entry{
        { QStringLiteral("level"), levelName(type) },
        { QStringLiteral("category"), category.isEmpty() ? QStringLiteral("qt") : category },
        { QStringLiteral("text"), message },
        { QStringLiteral("sourceLocation"),
          QQmlAgentSourceResolver::sourceLocationForMessageContext(context) },
        { QStringLiteral("timestamp"), double(QDateTime::currentMSecsSinceEpoch()) / 1000.0 },
    };
    addEntry(entry);
}

void QQmlAgentLogCollector::addEntry(const QJsonObject &entry)
{
    bool shouldEmit = false;
    {
        QMutexLocker locker(&m_mutex);
        const QJsonObject location = entry.value(QStringLiteral("sourceLocation")).toObject();
        const QString key = entry.value(QStringLiteral("level")).toString()
                + QLatin1Char('|') + entry.value(QStringLiteral("text")).toString()
                + QLatin1Char('|') + location.value(QStringLiteral("file")).toString()
                + QLatin1Char('|') + QString::number(location.value(QStringLiteral("line")).toInt());
        if (!m_buffer.append(key, entry))
            return;

        shouldEmit = m_eventsEnabled;
    }

    if (shouldEmit)
        emit entryAdded(entry);
}

QJsonObject QQmlAgentLogCollector::enable(const QJsonObject &params)
{
    QJsonArray replay;
    {
        QMutexLocker locker(&m_mutex);
        m_eventsEnabled = true;
        if (params.value(QStringLiteral("replayBuffered")).toBool(false))
            replay = m_buffer.entries();
    }

    for (const QJsonValue &entry : replay)
        emit entryAdded(entry.toObject());

    return {
        { QStringLiteral("enabled"), true },
        { QStringLiteral("replayed"), replay.size() },
    };
}

QJsonObject QQmlAgentLogCollector::entries(const QJsonObject &params) const
{
    const QString level = params.value(QStringLiteral("level")).toString();
    const double sinceTimestamp = params.value(QStringLiteral("sinceTimestamp")).toDouble(-1.0);
    const int maxEntries = params.value(QStringLiteral("maxEntries")).toInt(-1);
    QJsonArray result;
    int skippedBeforeCursor = 0;
    int skippedByLevel = 0;
    int truncated = 0;
    double nextSinceTimestamp = sinceTimestamp;
    QMutexLocker locker(&m_mutex);
    for (const QJsonValue &entryValue : m_buffer.entries()) {
        const QJsonObject entry = entryValue.toObject();
        const double timestamp = entry.value(QStringLiteral("timestamp")).toDouble(-1.0);
        if (sinceTimestamp >= 0 && timestamp >= 0 && timestamp <= sinceTimestamp) {
            ++skippedBeforeCursor;
            continue;
        }
        if (!level.isEmpty() && entry.value(QStringLiteral("level")).toString() != level) {
            ++skippedByLevel;
            continue;
        }
        if (maxEntries >= 0 && result.size() >= maxEntries) {
            ++truncated;
            continue;
        }
        result.append(entry);
        if (timestamp > nextSinceTimestamp)
            nextSinceTimestamp = timestamp;
    }
    return {
        { QStringLiteral("entries"), result },
        { QStringLiteral("entryCount"), result.size() },
        { QStringLiteral("nextSinceTimestamp"), nextSinceTimestamp },
        { QStringLiteral("skippedBeforeCursor"), skippedBeforeCursor },
        { QStringLiteral("skippedByLevel"), skippedByLevel },
        { QStringLiteral("truncated"), truncated > 0 },
        { QStringLiteral("omittedEntryCount"), truncated },
    };
}

QJsonObject QQmlAgentLogCollector::clear()
{
    reset();
    return { { QStringLiteral("cleared"), true } };
}

void QQmlAgentLogCollector::reset()
{
    QMutexLocker locker(&m_mutex);
    m_buffer.clear();
}

void QQmlAgentLogCollector::messageHandler(
        QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    QtMessageHandler previous = nullptr;

    {
        QMutexLocker locker(&s_handlerMutex);
        QQmlAgentLogCollector *collector = s_activeCollector;
        if (collector) {
            previous = collector->m_previousHandler;
            collector->captureMessage(type, context, message);
        }
    }

    if (previous)
        previous(type, context, message);
}

QT_END_NAMESPACE

#include "moc_qqmlagentlogcollector_p.cpp"
