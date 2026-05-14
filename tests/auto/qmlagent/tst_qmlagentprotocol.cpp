// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qqmlagentprotocol_p.h"
#include "qmlagentmcpprotocol.h"

#include <QtCore/qjsondocument.h>
#include <QtCore/qset.h>
#include <QtTest/qtest.h>

class tst_QQmlAgentProtocol : public QObject
{
    Q_OBJECT

private slots:
    void parsesValidRequest();
    void rejectsMalformedJsonAsParseError();
    void rejectsNonObjectJsonAsInvalidRequest();
    void rejectsInvalidParams();
    void formatsResponse();
    void formatsError();
    void formatsEvent();
    void mcpToolSchemasExposeAgentFirstContracts();
};

static QJsonObject objectFromJson(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

void tst_QQmlAgentProtocol::parsesValidRequest()
{
    const auto request = QQmlAgentProtocol::parseRequest(
            R"({"jsonrpc":"2.0","id":7,"method":"UI.getTree","params":{"depth":1}})");

    QVERIFY(request.valid);
    QCOMPARE(request.id.toInt(), 7);
    QCOMPARE(request.method, QStringLiteral("UI.getTree"));
    QCOMPARE(request.params.value(QStringLiteral("depth")).toInt(), 1);
}

void tst_QQmlAgentProtocol::rejectsMalformedJsonAsParseError()
{
    const auto request = QQmlAgentProtocol::parseRequest("{");

    QVERIFY(!request.valid);
    QCOMPARE(request.errorCode, -32700);
    QCOMPARE(request.errorMessage, QStringLiteral("Parse error"));
}

void tst_QQmlAgentProtocol::rejectsNonObjectJsonAsInvalidRequest()
{
    const auto request = QQmlAgentProtocol::parseRequest("[]");

    QVERIFY(!request.valid);
    QCOMPARE(request.errorCode, -32600);
    QCOMPARE(request.errorMessage, QStringLiteral("Invalid request"));
}

void tst_QQmlAgentProtocol::rejectsInvalidParams()
{
    const auto request = QQmlAgentProtocol::parseRequest(
            R"({"jsonrpc":"2.0","id":8,"method":"UI.getTree","params":[]})");

    QVERIFY(!request.valid);
    QCOMPARE(request.id.toInt(), 8);
    QCOMPARE(request.errorCode, -32602);
    QCOMPARE(request.errorMessage, QStringLiteral("Invalid params"));
}

void tst_QQmlAgentProtocol::formatsResponse()
{
    const QJsonObject response = objectFromJson(QQmlAgentProtocol::response(
            9, { { QStringLiteral("ok"), true } }));

    QCOMPARE(response.value(QStringLiteral("jsonrpc")).toString(), QStringLiteral("2.0"));
    QCOMPARE(response.value(QStringLiteral("id")).toInt(), 9);
    QVERIFY(response.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool());
}

void tst_QQmlAgentProtocol::formatsError()
{
    const QJsonObject response = objectFromJson(QQmlAgentProtocol::error(
            10, -32602, QStringLiteral("Invalid params"),
            { { QStringLiteral("field"), QStringLiteral("nodeId") } }));
    const QJsonObject error = response.value(QStringLiteral("error")).toObject();

    QCOMPARE(response.value(QStringLiteral("id")).toInt(), 10);
    QCOMPARE(error.value(QStringLiteral("code")).toInt(), -32602);
    QCOMPARE(error.value(QStringLiteral("message")).toString(), QStringLiteral("Invalid params"));
    QCOMPARE(error.value(QStringLiteral("data")).toObject().value(QStringLiteral("field")).toString(),
             QStringLiteral("nodeId"));
}

void tst_QQmlAgentProtocol::formatsEvent()
{
    const QJsonObject event = objectFromJson(QQmlAgentProtocol::event(
            QStringLiteral("Log.entryAdded"), { { QStringLiteral("count"), 1 } }));

    QCOMPARE(event.value(QStringLiteral("jsonrpc")).toString(), QStringLiteral("2.0"));
    QCOMPARE(event.value(QStringLiteral("method")).toString(), QStringLiteral("Log.entryAdded"));
    QVERIFY(!event.contains(QStringLiteral("id")));
    QCOMPARE(event.value(QStringLiteral("params")).toObject().value(QStringLiteral("count")).toInt(), 1);
}

static QJsonObject mcpToolByName(const QString &name)
{
    const QJsonArray tools = QmlAgentMcp::toolList();
    for (const QJsonValue &toolValue : tools) {
        const QJsonObject tool = toolValue.toObject();
        if (tool.value(QStringLiteral("name")).toString() == name)
            return tool;
    }
    return {};
}

void tst_QQmlAgentProtocol::mcpToolSchemasExposeAgentFirstContracts()
{
    const QJsonArray tools = QmlAgentMcp::toolList();
    QVERIFY(!tools.isEmpty());

    QSet<QString> names;
    for (const QJsonValue &toolValue : tools) {
        const QJsonObject tool = toolValue.toObject();
        const QString name = tool.value(QStringLiteral("name")).toString();
        QVERIFY2(!name.isEmpty(), qPrintable(QString::fromUtf8(QJsonDocument(tool).toJson())));
        QVERIFY2(!names.contains(name), qPrintable(name));
        names.insert(name);
        QCOMPARE(tool.value(QStringLiteral("inputSchema")).toObject()
                         .value(QStringLiteral("additionalProperties")).toBool(true),
                 false);
    }

    const QJsonObject screenshot = mcpToolByName(QStringLiteral("qmlagent.render_capture_screenshot"));
    QVERIFY(!screenshot.isEmpty());
    QVERIFY2(screenshot.value(QStringLiteral("description")).toString()
                    .contains(QStringLiteral("Fallback visual evidence")),
             qPrintable(QJsonDocument(screenshot).toJson(QJsonDocument::Compact)));
    const QJsonObject screenshotProperties = screenshot.value(QStringLiteral("inputSchema"))
            .toObject().value(QStringLiteral("properties")).toObject();
    QVERIFY(screenshotProperties.contains(QStringLiteral("includeData")));
    QVERIFY(screenshotProperties.contains(QStringLiteral("scale")));
    QVERIFY(screenshotProperties.contains(QStringLiteral("region")));

    const QJsonObject workflowKey = mcpToolByName(QStringLiteral("qmlagent.workflow_key"));
    QVERIFY(!workflowKey.isEmpty());
    QVERIFY2(workflowKey.value(QStringLiteral("description")).toString()
                    .contains(QStringLiteral("startsWith")),
             qPrintable(QJsonDocument(workflowKey).toJson(QJsonDocument::Compact)));
    QVERIFY2(workflowKey.value(QStringLiteral("inputSchema")).toObject()
                    .value(QStringLiteral("properties")).toObject()
                    .value(QStringLiteral("expect")).toObject()
                    .value(QStringLiteral("description")).toString()
                    .contains(QStringLiteral("contains")),
             qPrintable(QJsonDocument(workflowKey).toJson(QJsonDocument::Compact)));

    const QJsonObject uiWait = mcpToolByName(QStringLiteral("qmlagent.ui_wait_for"));
    QVERIFY(!uiWait.isEmpty());
    const QJsonArray waitOperators = uiWait.value(QStringLiteral("inputSchema")).toObject()
            .value(QStringLiteral("properties")).toObject()
            .value(QStringLiteral("until")).toObject()
            .value(QStringLiteral("properties")).toObject()
            .value(QStringLiteral("op")).toObject()
            .value(QStringLiteral("enum")).toArray();
    QVERIFY(waitOperators.contains(QStringLiteral("contains")));
    QVERIFY(waitOperators.contains(QStringLiteral("startsWith")));
    QVERIFY(waitOperators.contains(QStringLiteral("endsWith")));
}

QTEST_GUILESS_MAIN(tst_QQmlAgentProtocol)

#include "tst_qmlagentprotocol.moc"
