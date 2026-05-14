// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QQMLAGENTLOGCOLLECTOR_P_H
#define QQMLAGENTLOGCOLLECTOR_P_H

#include "qqmlagentlogbuffer_p.h"

#include <QtCore/qjsonobject.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qmutex.h>
#include <QtCore/qobject.h>
#include <QtCore/qset.h>

QT_BEGIN_NAMESPACE

class QQmlEngine;
class QQmlError;

class QQmlAgentLogCollector : public QObject
{
    Q_OBJECT

public:
    explicit QQmlAgentLogCollector(QObject *parent = nullptr);
    ~QQmlAgentLogCollector() override;

    void attachEngine(QQmlEngine *engine);
    QJsonObject enable(const QJsonObject &params);
    QJsonObject entries(const QJsonObject &params) const;
    QJsonObject clear();
    void reset();

signals:
    void entryAdded(const QJsonObject &entry);

private:
    void installMessageHandler();
    void restoreMessageHandler();
    void addEntry(const QJsonObject &entry);
    void addWarning(const QQmlError &warning);
    void captureMessage(QtMsgType type, const QMessageLogContext &context, const QString &message);

    static void messageHandler(QtMsgType type, const QMessageLogContext &context,
                               const QString &message);

    mutable QMutex m_mutex;
    QQmlAgentLogBuffer m_buffer;
    QSet<QQmlEngine *> m_engines;
    bool m_eventsEnabled = false;
    QtMessageHandler m_previousHandler = nullptr;

    static QQmlAgentLogCollector *s_activeCollector;
};

QT_END_NAMESPACE

#endif // QQMLAGENTLOGCOLLECTOR_P_H
