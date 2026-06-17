// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QQMLAGENTINPUT_P_H
#define QQMLAGENTINPUT_P_H

#include <QtCore/qjsonobject.h>
#include <QtCore/qstring.h>

QT_BEGIN_NAMESPACE

class QQmlAgentInput
{
public:
    static int dispatchBudgetMs(const QString &method, const QJsonObject &params);
    static QJsonObject clickNode(const QJsonObject &params);
    static QJsonObject longPressNode(const QJsonObject &params);
    static QJsonObject wheel(const QJsonObject &params);
    static QJsonObject scrollIntoView(const QJsonObject &params);
    static QJsonObject focusNode(const QJsonObject &params);
    static QJsonObject dispatchMouseEvent(const QJsonObject &params);
    static QJsonObject dragNode(const QJsonObject &params);
    static QJsonObject dispatchTouchEvent(const QJsonObject &params);
    static QJsonObject dispatchKeyEvent(const QJsonObject &params);
    static QJsonObject typeText(const QJsonObject &params);
    static QJsonObject dismissPopup(const QJsonObject &params);
};

QT_END_NAMESPACE

#endif // QQMLAGENTINPUT_P_H
