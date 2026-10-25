// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef LAUNCHERMAILBOXFIXTURE_H
#define LAUNCHERMAILBOXFIXTURE_H

#include "launcherfixture.h"

#include <QtCore/qset.h>
#include <QtCore/qthread.h>

#include <atomic>

// Real file-mailbox transport without a debug socket or a Qt Quick target.
// The responder substitutes only the external launcher for client regressions.
class LauncherMailboxFixture : private QThread
{
public:
    LauncherMailboxFixture(const QString &mailbox, const QJsonObject &status);
    ~LauncherMailboxFixture() override;

    bool stopped() const { return m_stopped.load(); }

private:
    void run() override;
    void respond(const QString &path);

    QString m_mailbox;
    QJsonObject m_status;
    QSet<QString> m_seen;
    std::atomic<bool> m_stopped{false};
};

#endif // LAUNCHERMAILBOXFIXTURE_H
