// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qqmlagentinput_p.h"
#include "qqmlagentdiagnostics_p.h"
#include "qqmlagentlogcollector_p.h"

#include <private/qqmldebugservice_p.h>

#include <QtCore/qjsonarray.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qlogging.h>
#include <QtCore/qobject.h>
#include <QtQuick/qquickitem.h>
#include <QtQuick/qquickwindow.h>
#include <QtTest/qtest.h>

QT_USE_NAMESPACE

class tst_QQmlAgentInput : public QObject
{
    Q_OBJECT

private slots:
    void clickNodeFailureReasons_data();
    void clickNodeFailureReasons();
    void dispatchKeyEventRejectsInvalidKey();
    void dispatchMouseEventRejectsInvalidType();
    void dispatchTouchEventRejectsInvalidPoints();
    void dragNodeRejectsMissingTarget();
    void typeTextRejectsEmptyText();
    void wheelRejectsMissingDelta();
    void diagnosticsPromoteOnlyRepairRelevantLogs();
};

static QJsonObject clickNode(int nodeId)
{
    return QQmlAgentInput::clickNode({ { QStringLiteral("nodeId"), nodeId } });
}

void tst_QQmlAgentInput::clickNodeFailureReasons_data()
{
    QTest::addColumn<QString>("caseName");
    QTest::addColumn<QString>("expectedReason");

    QTest::newRow("node_not_found") << QStringLiteral("missing")
                                    << QStringLiteral("node_not_found");
    QTest::newRow("not_qquickitem") << QStringLiteral("object")
                                    << QStringLiteral("not_qquickitem");
    QTest::newRow("not_visible") << QStringLiteral("invisibleItem")
                                 << QStringLiteral("not_visible");
    QTest::newRow("disabled") << QStringLiteral("disabledItem")
                              << QStringLiteral("disabled");
    QTest::newRow("zero_size") << QStringLiteral("zeroSizeItem")
                               << QStringLiteral("zero_size");
    QTest::newRow("no_window") << QStringLiteral("windowlessItem")
                               << QStringLiteral("no_window");
}

void tst_QQmlAgentInput::clickNodeFailureReasons()
{
    QFETCH(QString, caseName);
    QFETCH(QString, expectedReason);

    int nodeId = -1;
    std::unique_ptr<QQuickWindow> window;
    std::unique_ptr<QObject> object;

    if (caseName == QLatin1String("object")) {
        object = std::make_unique<QObject>();
        nodeId = QQmlDebugService::idForObject(object.get());
    } else if (caseName == QLatin1String("invisibleItem")) {
        window = std::make_unique<QQuickWindow>();
        window->resize(100, 100);
        auto item = std::make_unique<QQuickItem>();
        item->setParentItem(window->contentItem());
        item->setWidth(10);
        item->setHeight(10);
        item->setVisible(false);
        nodeId = QQmlDebugService::idForObject(item.get());
        object = std::move(item);
    } else if (caseName == QLatin1String("disabledItem")) {
        window = std::make_unique<QQuickWindow>();
        window->resize(100, 100);
        auto item = std::make_unique<QQuickItem>();
        item->setParentItem(window->contentItem());
        item->setWidth(10);
        item->setHeight(10);
        item->setVisible(true);
        item->setEnabled(false);
        nodeId = QQmlDebugService::idForObject(item.get());
        object = std::move(item);
    } else if (caseName == QLatin1String("zeroSizeItem")) {
        window = std::make_unique<QQuickWindow>();
        window->resize(100, 100);
        auto item = std::make_unique<QQuickItem>();
        item->setParentItem(window->contentItem());
        item->setVisible(true);
        nodeId = QQmlDebugService::idForObject(item.get());
        object = std::move(item);
    } else if (caseName == QLatin1String("windowlessItem")) {
        auto item = std::make_unique<QQuickItem>();
        item->setWidth(10);
        item->setHeight(10);
        item->setVisible(true);
        nodeId = QQmlDebugService::idForObject(item.get());
        object = std::move(item);
    }

    const QJsonObject result = clickNode(nodeId);
    QCOMPARE(result.value(QStringLiteral("delivered")).toBool(true), false);
    QCOMPARE(result.value(QStringLiteral("reason")).toString(), expectedReason);

    const QJsonArray diagnostics = result.value(QStringLiteral("diagnostics")).toArray();
    QCOMPARE(diagnostics.size(), 1);
    const QJsonObject diagnostic = diagnostics.at(0).toObject();
    QCOMPARE(diagnostic.value(QStringLiteral("id")).toString(),
             QStringLiteral("input.not_clickable"));
    QVERIFY2(!diagnostic.value(QStringLiteral("message")).toString().isEmpty(),
             qPrintable(QString::fromUtf8(QJsonDocument(diagnostic)
                                          .toJson(QJsonDocument::Compact))));
}

void tst_QQmlAgentInput::dispatchKeyEventRejectsInvalidKey()
{
    const QJsonObject result = QQmlAgentInput::dispatchKeyEvent({});
    QCOMPARE(result.value(QStringLiteral("delivered")).toBool(true), false);
    QCOMPARE(result.value(QStringLiteral("reason")).toString(), QStringLiteral("invalid_key"));

    const QJsonArray diagnostics = result.value(QStringLiteral("diagnostics")).toArray();
    QCOMPARE(diagnostics.size(), 1);
    QCOMPARE(diagnostics.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("input.not_deliverable"));
}

void tst_QQmlAgentInput::dispatchMouseEventRejectsInvalidType()
{
    const QJsonObject result = QQmlAgentInput::dispatchMouseEvent(
            { { QStringLiteral("nodeId"), 1 } });
    QCOMPARE(result.value(QStringLiteral("delivered")).toBool(true), false);
    QCOMPARE(result.value(QStringLiteral("reason")).toString(), QStringLiteral("invalid_type"));

    const QJsonArray diagnostics = result.value(QStringLiteral("diagnostics")).toArray();
    QCOMPARE(diagnostics.size(), 1);
    QCOMPARE(diagnostics.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("input.not_mouse_dispatchable"));
}

void tst_QQmlAgentInput::dispatchTouchEventRejectsInvalidPoints()
{
    const QJsonObject result = QQmlAgentInput::dispatchTouchEvent({
        { QStringLiteral("nodeId"), 1 },
        { QStringLiteral("type"), QStringLiteral("touchBegin") },
    });
    QCOMPARE(result.value(QStringLiteral("delivered")).toBool(true), false);
    QCOMPARE(result.value(QStringLiteral("reason")).toString(), QStringLiteral("invalid_points"));

    const QJsonArray diagnostics = result.value(QStringLiteral("diagnostics")).toArray();
    QCOMPARE(diagnostics.size(), 1);
    QCOMPARE(diagnostics.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("input.not_touch_dispatchable"));
}

void tst_QQmlAgentInput::dragNodeRejectsMissingTarget()
{
    const QJsonObject result = QQmlAgentInput::dragNode({ { QStringLiteral("nodeId"), 1 } });
    QCOMPARE(result.value(QStringLiteral("delivered")).toBool(true), false);
    QCOMPARE(result.value(QStringLiteral("reason")).toString(), QStringLiteral("invalid_points"));

    const QJsonArray diagnostics = result.value(QStringLiteral("diagnostics")).toArray();
    QCOMPARE(diagnostics.size(), 1);
    QCOMPARE(diagnostics.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("input.not_draggable"));
}

void tst_QQmlAgentInput::typeTextRejectsEmptyText()
{
    const QJsonObject result = QQmlAgentInput::typeText({});
    QCOMPARE(result.value(QStringLiteral("delivered")).toBool(true), false);
    QCOMPARE(result.value(QStringLiteral("reason")).toString(), QStringLiteral("invalid_text"));

    const QJsonArray diagnostics = result.value(QStringLiteral("diagnostics")).toArray();
    QCOMPARE(diagnostics.size(), 1);
    QCOMPARE(diagnostics.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("input.not_deliverable"));
}

void tst_QQmlAgentInput::wheelRejectsMissingDelta()
{
    const QJsonObject result = QQmlAgentInput::wheel({ { QStringLiteral("nodeId"), 1 } });
    QCOMPARE(result.value(QStringLiteral("delivered")).toBool(true), false);
    QCOMPARE(result.value(QStringLiteral("reason")).toString(), QStringLiteral("invalid_delta"));

    const QJsonArray diagnostics = result.value(QStringLiteral("diagnostics")).toArray();
    QCOMPARE(diagnostics.size(), 1);
    QCOMPARE(diagnostics.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("input.not_wheelable"));
}

void tst_QQmlAgentInput::diagnosticsPromoteOnlyRepairRelevantLogs()
{
    QQmlAgentLogCollector logs;

    QTest::ignoreMessage(QtWarningMsg, "Populating font family aliases took 42 ms");
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO, "qt.qpa.fonts")
            .warning("Populating font family aliases took 42 ms");
    QTest::ignoreMessage(QtWarningMsg, "ReferenceError: missingName is not defined");
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO, "qml")
            .warning("ReferenceError: missingName is not defined");

    const QJsonObject result = QQmlAgentDiagnostics::analyzeTree({}, &logs);
    const QJsonArray issues = result.value(QStringLiteral("issues")).toArray();
    QCOMPARE(issues.size(), 1);
    QCOMPARE(issues.at(0).toObject().value(QStringLiteral("message")).toString(),
             QStringLiteral("ReferenceError: missingName is not defined"));

    const QJsonObject summary = result.value(QStringLiteral("summary")).toObject();
    QCOMPARE(summary.value(QStringLiteral("logEntryCount")).toInt(), 2);
    QCOMPARE(summary.value(QStringLiteral("promotedLogIssueCount")).toInt(), 1);
    QCOMPARE(summary.value(QStringLiteral("ignoredLogEntryCount")).toInt(), 1);
    QCOMPARE(summary.value(QStringLiteral("issueCount")).toInt(), issues.size());
}

QTEST_MAIN(tst_QQmlAgentInput)

#include "tst_qmlagentinput.moc"
