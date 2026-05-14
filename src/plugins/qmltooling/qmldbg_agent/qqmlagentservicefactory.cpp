// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qqmlagentservicefactory_p.h"

#include "qqmlagentservice_p.h"

QT_BEGIN_NAMESPACE

QQmlDebugService *QQmlAgentServiceFactory::create(const QString &key)
{
    if (key == QQmlAgentService::serviceKey())
        return new QQmlAgentService(this);
    return nullptr;
}

QT_END_NAMESPACE

#include "moc_qqmlagentservicefactory_p.cpp"
