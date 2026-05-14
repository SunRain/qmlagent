// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QQMLAGENTSOURCERESOLVER_P_H
#define QQMLAGENTSOURCERESOLVER_P_H

#include <QtCore/qjsonarray.h>
#include <QtCore/qjsonobject.h>

QT_BEGIN_NAMESPACE

class QObject;
class QQmlError;
struct QMessageLogContext;

class QQmlAgentSourceResolver
{
public:
    static QJsonObject sourceLocationForObject(const QObject *object);
    static QJsonObject bindingProvenanceForProperty(QObject *object, const QString &propertyName);
    static QJsonArray fallbackLocationsForObject(const QObject *object,
                                                 const QJsonObject &primaryLocation);
    static QJsonObject sourceLocationForWarning(const QQmlError &warning);
    static QJsonObject sourceLocationForMessageContext(const QMessageLogContext &context);
    static QJsonObject unknownLocation(const QString &method = QStringLiteral("unknown"));
};

QT_END_NAMESPACE

#endif // QQMLAGENTSOURCERESOLVER_P_H
