// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QQMLAGENTLOGBUFFER_P_H
#define QQMLAGENTLOGBUFFER_P_H

#include <QtCore/qjsonarray.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qlist.h>
#include <QtCore/qset.h>

QT_BEGIN_NAMESPACE

class QQmlAgentLogBuffer
{
public:
    static constexpr qsizetype maxEntries = 500;
    static constexpr qsizetype maxBytes = 1024 * 1024;

    bool append(const QString &dedupKey, const QJsonObject &entry);
    void clear();

    QJsonArray entries() const { return m_entries; }
    qsizetype byteSize() const { return m_bytes; }
    qsizetype size() const { return m_entries.size(); }
    bool containsKey(const QString &dedupKey) const { return m_dedupKeys.contains(dedupKey); }

private:
    QJsonArray m_entries;
    QList<qsizetype> m_entrySizes;
    QList<QString> m_entryKeys;
    qsizetype m_bytes = 0;
    QSet<QString> m_dedupKeys;
};

QT_END_NAMESPACE

#endif // QQMLAGENTLOGBUFFER_P_H
