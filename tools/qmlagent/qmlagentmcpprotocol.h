// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QMLAGENTMCPPROTOCOL_H
#define QMLAGENTMCPPROTOCOL_H

#include <QtCore/qjsonarray.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qjsonvalue.h>

namespace QmlAgentMcp {

QJsonArray toolList();
QJsonObject jsonResponse(const QJsonValue &id, const QJsonValue &result);
QJsonObject jsonError(const QJsonValue &id, int code, const QString &message,
                      const QJsonValue &data = {});
QJsonObject toolResult(const QJsonValue &payload, bool isError = false);
QJsonObject toolErrorResult(const QString &message);

} // namespace QmlAgentMcp

#endif // QMLAGENTMCPPROTOCOL_H
