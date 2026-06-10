// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qqmlagentinput_p.h"
#include "qqmlagentdiagnostics_p.h"
#include "qqmlagentlogcollector_p.h"
#include "qqmlagentruntime_p.h"
#include "qqmlagentuitree_p.h"

#include <private/qqmldebugservice_p.h>

#include <QtCore/qjsonarray.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qlogging.h>
#include <QtCore/qobject.h>
#include <QtCore/qtimer.h>
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
    void waitForObservesInvisibleProperty();
    void waitForPrefersUniqueVisibleMatch();
    void dispatchBudgetsCoverLongRunningRequests();
    void selectorStabilityReflectsTreeUniqueness();
    void queryManyAlignsResultsAndAppliesDefaults();
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
    QTest::newRow("unknown_window") << QStringLiteral("windowlessItem")
                                    << QStringLiteral("unknown_window");
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

void tst_QQmlAgentInput::waitForObservesInvisibleProperty()
{
    QQuickWindow window;
    window.resize(200, 200);
    QQuickItem item;
    item.setParentItem(window.contentItem());
    item.setObjectName(QStringLiteral("waitInvisibleTarget"));
    item.setWidth(20);
    item.setHeight(20);
    item.setVisible(true);

    QTimer::singleShot(100, &item, [&item]() { item.setVisible(false); });

    const QJsonObject result = QQmlAgentUiTree::waitFor({
        { QStringLiteral("selector"), QStringLiteral("objectName=\"waitInvisibleTarget\"") },
        { QStringLiteral("until"), QJsonObject{
            { QStringLiteral("property"), QStringLiteral("visible") },
            { QStringLiteral("op"), QStringLiteral("=") },
            { QStringLiteral("value"), false },
        } },
        { QStringLiteral("timeoutMs"), 3000 },
    });

    QVERIFY2(result.value(QStringLiteral("ok")).toBool(false),
             qPrintable(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))));
    QCOMPARE(result.value(QStringLiteral("reason")).toString(),
             QStringLiteral("predicate_satisfied"));
    QCOMPARE(result.value(QStringLiteral("actual")).toBool(true), false);
}

void tst_QQmlAgentInput::waitForPrefersUniqueVisibleMatch()
{
    QQuickWindow window;
    window.resize(200, 200);

    QQuickItem visibleItem;
    visibleItem.setParentItem(window.contentItem());
    visibleItem.setObjectName(QStringLiteral("waitDuplicateTarget"));
    visibleItem.setWidth(40);
    visibleItem.setHeight(20);
    visibleItem.setVisible(true);

    QQuickItem invisibleItem;
    invisibleItem.setParentItem(window.contentItem());
    invisibleItem.setObjectName(QStringLiteral("waitDuplicateTarget"));
    invisibleItem.setWidth(1);
    invisibleItem.setHeight(1);
    invisibleItem.setVisible(false);

    const QJsonObject result = QQmlAgentUiTree::waitFor({
        { QStringLiteral("selector"), QStringLiteral("objectName=\"waitDuplicateTarget\"") },
        { QStringLiteral("until"), QJsonObject{
            { QStringLiteral("property"), QStringLiteral("width") },
            { QStringLiteral("op"), QStringLiteral(">") },
            { QStringLiteral("value"), 10 },
        } },
        { QStringLiteral("timeoutMs"), 500 },
    });

    QVERIFY2(result.value(QStringLiteral("ok")).toBool(false),
             qPrintable(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))));
    QCOMPARE(result.value(QStringLiteral("reason")).toString(),
             QStringLiteral("predicate_satisfied"));
}

void tst_QQmlAgentInput::dispatchBudgetsCoverLongRunningRequests()
{
    // UI.waitFor budget follows the requested timeout, top-level or in until.
    QCOMPARE(QQmlAgentUiTree::waitForBudgetMs({ { QStringLiteral("timeoutMs"), 20000 } }), 20000);
    QCOMPARE(QQmlAgentUiTree::waitForBudgetMs({
        { QStringLiteral("until"), QJsonObject{ { QStringLiteral("timeoutMs"), 8000 } } },
    }), 8000);
    QCOMPARE(QQmlAgentUiTree::waitForBudgetMs({ { QStringLiteral("timeoutMs"), 90000 } }), 30000);

    // Long press budget covers hold plus settle.
    QVERIFY(QQmlAgentInput::dispatchBudgetMs(QStringLiteral("Input.longPressNode"),
                                             { { QStringLiteral("holdMs"), 8000 } }) >= 8000);

    // Settle budgets are honored and clamped.
    const QJsonObject longSettle{
        { QStringLiteral("settle"), QJsonObject{ { QStringLiteral("timeoutMs"), 12000 } } },
    };
    QVERIFY(QQmlAgentInput::dispatchBudgetMs(QStringLiteral("Input.clickNode"), longSettle) >= 12000);
    QVERIFY(QQmlAgentRuntime::dispatchBudgetMs(longSettle) >= 12000);
    const QJsonObject hugeSettle{
        { QStringLiteral("settle"), QJsonObject{ { QStringLiteral("timeoutMs"), 600000 } } },
    };
    QVERIFY(QQmlAgentInput::dispatchBudgetMs(QStringLiteral("Input.clickNode"), hugeSettle) <= 60000);
}

static QString objectNameSelectorStability(const QJsonObject &tree, const QString &objectName)
{
    QList<QJsonObject> stack;
    const QJsonArray windows = tree.value(QStringLiteral("windows")).toArray();
    for (const QJsonValue &window : windows)
        stack.append(window.toObject().value(QStringLiteral("root")).toObject());
    while (!stack.isEmpty()) {
        const QJsonObject node = stack.takeLast();
        const QJsonArray children = node.value(QStringLiteral("children")).toArray();
        for (const QJsonValue &child : children)
            stack.append(child.toObject());
        if (node.value(QStringLiteral("objectName")).toString() != objectName)
            continue;
        const QJsonArray selectors = node.value(QStringLiteral("selectors")).toArray();
        for (const QJsonValue &selectorValue : selectors) {
            const QJsonObject selector = selectorValue.toObject();
            if (selector.value(QStringLiteral("kind")).toString()
                    == QLatin1String("objectName")) {
                return selector.value(QStringLiteral("stability")).toString();
            }
        }
    }
    return {};
}

void tst_QQmlAgentInput::selectorStabilityReflectsTreeUniqueness()
{
    QQuickWindow window;
    window.resize(200, 200);

    QQuickItem first;
    first.setParentItem(window.contentItem());
    first.setObjectName(QStringLiteral("duplicateName"));
    QQuickItem second;
    second.setParentItem(window.contentItem());
    second.setObjectName(QStringLiteral("duplicateName"));
    QQuickItem third;
    third.setParentItem(window.contentItem());
    third.setObjectName(QStringLiteral("uniqueName"));

    const QJsonObject tree = QQmlAgentUiTree::getTree({
        { QStringLiteral("includeInvisible"), true },
    });

    QCOMPARE(objectNameSelectorStability(tree, QStringLiteral("duplicateName")),
             QStringLiteral("medium"));
    QCOMPARE(objectNameSelectorStability(tree, QStringLiteral("uniqueName")),
             QStringLiteral("high"));
}

void tst_QQmlAgentInput::queryManyAlignsResultsAndAppliesDefaults()
{
    QQuickWindow window;
    window.resize(200, 200);
    QQuickItem first;
    first.setParentItem(window.contentItem());
    first.setObjectName(QStringLiteral("batchFirst"));
    first.setWidth(11);
    QQuickItem second;
    second.setParentItem(window.contentItem());
    second.setObjectName(QStringLiteral("batchSecond"));
    second.setWidth(22);

    const QJsonObject batch = QQmlAgentUiTree::queryMany({
        { QStringLiteral("queries"), QJsonArray{
            QJsonObject{ { QStringLiteral("selector"), QStringLiteral("objectName=\"batchFirst\"") } },
            QJsonObject{ { QStringLiteral("selector"), QStringLiteral("objectName=\"batchSecond\"") } },
        } },
        { QStringLiteral("defaults"), QJsonObject{
            { QStringLiteral("includeInvisible"), true },
            { QStringLiteral("properties"), QJsonArray{ QStringLiteral("width") } },
        } },
    });

    QCOMPARE(batch.value(QStringLiteral("resultCount")).toInt(), 2);
    const QJsonArray results = batch.value(QStringLiteral("results")).toArray();
    QCOMPARE(results.size(), 2);
    const QJsonObject firstMatch = results.at(0).toObject()
            .value(QStringLiteral("matches")).toArray().at(0).toObject();
    const QJsonObject secondMatch = results.at(1).toObject()
            .value(QStringLiteral("matches")).toArray().at(0).toObject();
    QCOMPARE(firstMatch.value(QStringLiteral("objectName")).toString(),
             QStringLiteral("batchFirst"));
    QCOMPARE(secondMatch.value(QStringLiteral("objectName")).toString(),
             QStringLiteral("batchSecond"));
    QCOMPARE(firstMatch.value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("width")).toDouble(), 11.0);
    QCOMPARE(secondMatch.value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("width")).toDouble(), 22.0);

    const QJsonObject empty = QQmlAgentUiTree::queryMany({});
    QCOMPARE(empty.value(QStringLiteral("diagnostics")).toArray().at(0).toObject()
                     .value(QStringLiteral("id")).toString(),
             QStringLiteral("batch.queries_required"));

    QJsonArray tooMany;
    for (int i = 0; i < 51; ++i)
        tooMany.append(QJsonObject{ { QStringLiteral("selector"), QStringLiteral("nodeId=1") } });
    const QJsonObject overflow = QQmlAgentUiTree::queryMany({
        { QStringLiteral("queries"), tooMany },
    });
    QCOMPARE(overflow.value(QStringLiteral("diagnostics")).toArray().at(0).toObject()
                     .value(QStringLiteral("id")).toString(),
             QStringLiteral("batch.too_many_queries"));
}

QTEST_MAIN(tst_QQmlAgentInput)

#include "tst_qmlagentinput.moc"
