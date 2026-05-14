// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QQMLAGENTSERVICE_P_H
#define QQMLAGENTSERVICE_P_H

#include "qqmlagentlogcollector_p.h"

#include <private/qqmldebugservice_p.h>

#include <QtCore/qjsonobject.h>
#include <QtCore/qlist.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qset.h>

#include <atomic>

QT_BEGIN_NAMESPACE

class QQmlAgentService : public QQmlDebugService
{
    Q_OBJECT

public:
    explicit QQmlAgentService(QObject *parent = nullptr);
    ~QQmlAgentService() override;

    static QString serviceKey();

    void engineAdded(QJSEngine *engine) override;

protected:
    void messageReceived(const QByteArray &message) override;

private:
    QJsonObject dispatch(const QString &method, const QJsonObject &params);
    QJsonObject sessionInfo() const;
    QJsonObject sessionConfigure(const QJsonObject &params);
    QJsonObject runtimeMutationDisabledResult() const;
    void sendProtocolMessage(const QByteArray &message);
    void sendEntryEvent(const QJsonObject &entry);
    QJsonObject subscribeUi(const QJsonObject &params);
    QJsonObject unsubscribeUi();
    void installUiWatchers();
    void clearUiWatchers();
    void watchUiObject(QObject *object, QSet<QObject *> *seen);
    void scheduleUiWatcherRefresh();
    void scheduleUiChangedEvent(const QString &reason, int windowId = -1);
    void sendUiChangedEvent(const QString &reason, int windowId);

    QString m_sessionId;
    QQmlAgentLogCollector m_logs;
    QList<QMetaObject::Connection> m_uiConnections;
    std::atomic_bool m_uiSubscribed = false;
    std::atomic_bool m_uiEventPending = false;
    std::atomic_bool m_uiWatcherRefreshPending = false;
    bool m_runtimeMutationEnabled = false;
    int m_uiEventSequence = 0;
};

QT_END_NAMESPACE

#endif // QQMLAGENTSERVICE_P_H
