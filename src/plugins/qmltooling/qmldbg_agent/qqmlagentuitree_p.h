// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QQMLAGENTUITREE_P_H
#define QQMLAGENTUITREE_P_H

#include <QtCore/qjsonarray.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qset.h>
#include <QtCore/qstringlist.h>

QT_BEGIN_NAMESPACE

class QObject;
class QQuickItem;

class QQmlAgentUiTree
{
public:
    struct NodeRef
    {
        QObject *object = nullptr;
        int nodeId = -1;
        QJsonObject node;
        QJsonArray issues;
        QString failureReason;
    };

    static QJsonObject getTree(const QJsonObject &params);
    static QJsonObject query(const QJsonObject &params);
    static QJsonObject waitFor(const QJsonObject &params);
    static QJsonObject describeNode(const QJsonObject &params);
    static QJsonObject getBoxModel(const QJsonObject &params);
    static QJsonObject nodeForObject(QObject *object, int windowId, int depth, bool includeInvisible,
                                     bool includeSource, const QStringList &properties,
                                     QSet<QObject *> *seen = nullptr,
                                     const QString &visualPath = {});
    static QObject *objectForNodeId(int nodeId);
    static NodeRef resolveNodeRef(const QJsonObject &params, bool includeSource = true,
                                  const QJsonArray &properties = {});
};

QT_END_NAMESPACE

#endif // QQMLAGENTUITREE_P_H
