// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qqmlagentlogbuffer_p.h"

#include <QtCore/qjsondocument.h>

QT_BEGIN_NAMESPACE

bool QQmlAgentLogBuffer::append(const QString &dedupKey, const QJsonObject &entry)
{
    if (m_dedupKeys.contains(dedupKey))
        return false;

    const qsizetype entrySize = QJsonDocument(entry).toJson(QJsonDocument::Compact).size();
    m_dedupKeys.insert(dedupKey);
    m_entries.append(entry);
    m_entrySizes.append(entrySize);
    m_entryKeys.append(dedupKey);
    m_bytes += entrySize;

    while (m_entries.size() > maxEntries || m_bytes > maxBytes) {
        m_entries.removeFirst();
        m_bytes -= m_entrySizes.takeFirst();
        m_dedupKeys.remove(m_entryKeys.takeFirst());
    }

    return true;
}

void QQmlAgentLogBuffer::clear()
{
    m_entries = QJsonArray();
    m_entrySizes.clear();
    m_entryKeys.clear();
    m_bytes = 0;
    m_dedupKeys.clear();
}

QT_END_NAMESPACE
