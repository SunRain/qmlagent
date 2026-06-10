// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QMLAGENTMCPOUTPUT_H
#define QMLAGENTMCPOUTPUT_H

#include <QtCore/qjsonobject.h>

QJsonObject summarizedQueryResult(const QJsonObject &result);
QJsonObject summarizedQueryManyResult(const QJsonObject &result);
QJsonObject summarizedWorkflowReport(const QJsonObject &report);

#endif // QMLAGENTMCPOUTPUT_H
