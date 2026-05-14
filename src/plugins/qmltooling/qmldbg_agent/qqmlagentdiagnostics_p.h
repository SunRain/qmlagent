// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QQMLAGENTDIAGNOSTICS_P_H
#define QQMLAGENTDIAGNOSTICS_P_H

#include <QtCore/qjsonobject.h>
#include <QtCore/qstring.h>

QT_BEGIN_NAMESPACE

class QQmlAgentLogCollector;

class QQmlAgentDiagnostics
{
public:
    static QJsonObject analyzeNode(const QJsonObject &params);
    static QJsonObject analyzeTree(const QJsonObject &params, QQmlAgentLogCollector *logs = nullptr);
    static QJsonObject analyzeBinding(const QJsonObject &params);
    static QJsonObject selectorNotFound(const QString &kind, const QString &value,
                                        const QJsonObject &tree);
};

QT_END_NAMESPACE

#endif // QQMLAGENTDIAGNOSTICS_P_H
