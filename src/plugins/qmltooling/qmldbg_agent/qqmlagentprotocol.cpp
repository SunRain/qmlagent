// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qqmlagentprotocol_p.h"

#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonvalue.h>

QT_BEGIN_NAMESPACE

QQmlAgentProtocol::Request QQmlAgentProtocol::parseRequest(const QByteArray &message)
{
    Request request;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(message, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        request.errorCode = -32700;
        request.errorMessage = QStringLiteral("Parse error");
        return request;
    }

    if (!document.isObject()) {
        request.errorCode = -32600;
        request.errorMessage = QStringLiteral("Invalid request");
        return request;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("jsonrpc")).toString() != QLatin1String("2.0")
            || !object.value(QStringLiteral("method")).isString()) {
        request.id = object.value(QStringLiteral("id"));
        request.errorCode = -32600;
        request.errorMessage = QStringLiteral("Invalid request");
        return request;
    }

    const QJsonValue params = object.value(QStringLiteral("params"));
    if (!params.isUndefined() && !params.isObject()) {
        request.id = object.value(QStringLiteral("id"));
        request.errorCode = -32602;
        request.errorMessage = QStringLiteral("Invalid params");
        return request;
    }

    request.valid = true;
    request.id = object.value(QStringLiteral("id"));
    request.method = object.value(QStringLiteral("method")).toString();
    request.params = params.toObject();
    return request;
}

static QByteArray compactJson(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray QQmlAgentProtocol::response(const QJsonValue &id, const QJsonObject &result)
{
    return compactJson({
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), id },
        { QStringLiteral("result"), result },
    });
}

QByteArray QQmlAgentProtocol::error(
        const QJsonValue &id, int code, const QString &message, const QJsonObject &data)
{
    QJsonObject errorObject{
        { QStringLiteral("code"), code },
        { QStringLiteral("message"), message },
    };
    if (!data.isEmpty())
        errorObject.insert(QStringLiteral("data"), data);

    return compactJson({
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), id },
        { QStringLiteral("error"), errorObject },
    });
}

QByteArray QQmlAgentProtocol::event(const QString &method, const QJsonObject &params)
{
    return compactJson({
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("method"), method },
        { QStringLiteral("params"), params },
    });
}

QT_END_NAMESPACE
