// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QQMLAGENTACTIONABILITY_P_H
#define QQMLAGENTACTIONABILITY_P_H

#include <QtCore/qjsonarray.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qpoint.h>

QT_BEGIN_NAMESPACE

class QObject;

class QQmlAgentActionability
{
public:
    static QJsonArray reasons(QObject *object);
    static QJsonArray reasonsAtPoint(QObject *object, const QPointF &scenePoint);
    static QJsonObject state(QObject *object);
    static QJsonObject acceptsInputEvidence(QObject *object);
};

QT_END_NAMESPACE

#endif // QQMLAGENTACTIONABILITY_P_H
