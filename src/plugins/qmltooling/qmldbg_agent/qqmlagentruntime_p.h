// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QQMLAGENTRUNTIME_P_H
#define QQMLAGENTRUNTIME_P_H

#include <QtCore/qjsonobject.h>

QT_BEGIN_NAMESPACE

class QQmlAgentRuntime
{
public:
    static QJsonObject setProperty(const QJsonObject &params);
    static QJsonObject invokeMethod(const QJsonObject &params);
};

QT_END_NAMESPACE

#endif // QQMLAGENTRUNTIME_P_H
