// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QQMLAGENTPROTOCOL_P_H
#define QQMLAGENTPROTOCOL_P_H

#include <QtCore/qbytearray.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qjsonvalue.h>
#include <QtCore/qstring.h>

QT_BEGIN_NAMESPACE

class QQmlAgentProtocol
{
public:
    struct Request
    {
        bool valid = false;
        QJsonValue id;
        QString method;
        QJsonObject params;
        int errorCode = 0;
        QString errorMessage;
        QJsonObject errorData;
    };

    static Request parseRequest(const QByteArray &message);
    static QByteArray response(const QJsonValue &id, const QJsonObject &result);
    static QByteArray error(const QJsonValue &id, int code, const QString &message,
                            const QJsonObject &data = {});
    static QByteArray event(const QString &method, const QJsonObject &params);
};

QT_END_NAMESPACE

#endif // QQMLAGENTPROTOCOL_P_H
