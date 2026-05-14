// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QMLAGENTCLIOUTPUT_H
#define QMLAGENTCLIOUTPUT_H

#include <QtCore/qjsonobject.h>
#include <QtCore/qjsonvalue.h>

QString jsonValueToSummaryString(const QJsonValue &value);
void printObjectSummary(const QJsonObject &object);

#endif // QMLAGENTCLIOUTPUT_H
