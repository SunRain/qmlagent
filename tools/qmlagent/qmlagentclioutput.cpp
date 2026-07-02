// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qmlagentclioutput.h"

#include <QtCore/qjsonarray.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qtextstream.h>

QString jsonValueToSummaryString(const QJsonValue &value)
{
    if (value.isUndefined())
        return QStringLiteral("<missing>");
    if (value.isNull())
        return QStringLiteral("null");
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble())
        return QString::number(value.toDouble());
    if (value.isString())
        return value.toString();
    return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}

static QString sourceLocationSummary(const QJsonObject &location)
{
    const QString file = location.value(QStringLiteral("file")).toString();
    if (file.isEmpty())
        return QStringLiteral("unknown");

    const int line = location.value(QStringLiteral("line")).toInt();
    const int column = location.value(QStringLiteral("column")).toInt();
    const QString method = location.value(QStringLiteral("method")).toString();
    const double confidence = location.value(QStringLiteral("confidence")).toDouble();
    return QStringLiteral("%1:%2:%3 method=%4 confidence=%5")
            .arg(file)
            .arg(line)
            .arg(column)
            .arg(method)
            .arg(confidence, 0, 'f', 2);
}

static void printSummaryLine(QTextStream &stream, const QString &line)
{
    stream << line << Qt::endl;
}

static void printNodeSummary(QTextStream &stream, const QJsonObject &node,
                             const QString &prefix = QStringLiteral("node"))
{
    const QJsonArray bbox = node.value(QStringLiteral("bbox")).toArray();
    QStringList bboxParts;
    for (const QJsonValue &value : bbox)
        bboxParts.append(QString::number(value.toDouble()));

    printSummaryLine(stream,
                     QStringLiteral("%1 nodeId=%2 qmlId=%3 objectName=%4 type=%5 insideViewport=%6 bbox=[%7] source=%8")
                             .arg(prefix)
                             .arg(node.value(QStringLiteral("nodeId")).toInt(-1))
                             .arg(node.value(QStringLiteral("qmlId")).toString(QStringLiteral("<none>")))
                             .arg(node.value(QStringLiteral("objectName")).toString(QStringLiteral("<none>")))
                             .arg(node.value(QStringLiteral("type")).toString(QStringLiteral("<unknown>")))
                             .arg(node.value(QStringLiteral("insideViewport")).toBool()
                                          ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(bboxParts.join(QLatin1Char(',')))
                             .arg(sourceLocationSummary(node.value(QStringLiteral("sourceLocation")).toObject())));
}

static QString treeNodeLabel(const QJsonObject &node)
{
    QStringList parts;
    const QString qmlId = node.value(QStringLiteral("qmlId")).toString();
    const QString objectName = node.value(QStringLiteral("objectName")).toString();
    parts.append(node.value(QStringLiteral("type")).toString(QStringLiteral("<unknown>")));
    if (!qmlId.isEmpty())
        parts.append(QStringLiteral("id=%1").arg(qmlId));
    if (!objectName.isEmpty())
        parts.append(QStringLiteral("objectName=%1").arg(objectName));
    parts.append(QStringLiteral("nodeId=%1").arg(node.value(QStringLiteral("nodeId")).toInt(-1)));
    return parts.join(QLatin1Char(' '));
}

static QString treeRepeatKey(const QJsonObject &node)
{
    const QJsonObject source = node.value(QStringLiteral("sourceLocation")).toObject();
    return QStringList{
        node.value(QStringLiteral("type")).toString(),
        node.value(QStringLiteral("qmlId")).toString(),
        node.value(QStringLiteral("objectName")).toString(),
        source.value(QStringLiteral("file")).toString(),
        QString::number(source.value(QStringLiteral("line")).toInt()),
        QString::number(source.value(QStringLiteral("column")).toInt()),
    }.join(QLatin1Char('|'));
}

static void printTreeNodeSummary(QTextStream &stream, const QJsonObject &node, int depth)
{
    const QString indent(depth * 2, QLatin1Char(' '));
    if (node.value(QStringLiteral("collapsed")).toBool()) {
        printSummaryLine(stream, QStringLiteral("%1repeated count=%2 %3")
                                 .arg(indent)
                                 .arg(node.value(QStringLiteral("count")).toInt())
                                 .arg(treeNodeLabel(node)));
        return;
    }

    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    printSummaryLine(stream, QStringLiteral("%1%2 children=%3")
                             .arg(indent, treeNodeLabel(node))
                             .arg(children.size()));

    for (int i = 0; i < children.size();) {
        const QJsonObject child = children.at(i).toObject();
        const QString key = treeRepeatKey(child);
        int count = 1;
        while (i + count < children.size()
               && treeRepeatKey(children.at(i + count).toObject()) == key) {
            ++count;
        }

        if (count >= 3) {
            printSummaryLine(stream, QStringLiteral("%1  repeated count=%2 %3")
                                     .arg(indent)
                                     .arg(count)
                                     .arg(treeNodeLabel(child)));
        } else {
            for (int j = 0; j < count; ++j)
                printTreeNodeSummary(stream, children.at(i + j).toObject(), depth + 1);
        }
        i += count;
    }
}

static void printIssueSummary(QTextStream &stream, const QJsonObject &issue)
{
    printSummaryLine(stream,
                     QStringLiteral("issue id=%1 severity=%2 nodeId=%3 confidence=%4 message=%5")
                             .arg(issue.value(QStringLiteral("id")).toString())
                             .arg(issue.value(QStringLiteral("severity")).toString())
                             .arg(issue.value(QStringLiteral("nodeId")).toInt(-1))
                             .arg(issue.value(QStringLiteral("confidence")).toDouble(), 0, 'f', 2)
                             .arg(issue.value(QStringLiteral("message")).toString()));

    const QJsonArray hints = issue.value(QStringLiteral("repairHints")).toArray();
    for (const QJsonValue &hintValue : hints) {
        const QJsonObject hint = hintValue.toObject();
        printSummaryLine(stream,
                         QStringLiteral("repairHint kind=%1 direction=%2 confidence=%3 reason=%4")
                                 .arg(hint.value(QStringLiteral("kind")).toString())
                                 .arg(hint.value(QStringLiteral("suggestedDirection")).toString())
                                 .arg(hint.value(QStringLiteral("confidence")).toDouble(), 0, 'f', 2)
                                 .arg(hint.value(QStringLiteral("reason")).toString()));
    }
}

static void printWorkflowSummary(QTextStream &stream, const QJsonObject &workflow)
{
    printSummaryLine(stream,
                     QStringLiteral("workflow kind=%1 target=%2 expected=%3")
                             .arg(workflow.value(QStringLiteral("kind")).toString())
                             .arg(workflow.value(QStringLiteral("targetSelector")).toString())
                             .arg(workflow.value(QStringLiteral("expectedSelector")).toString()));

    const QJsonObject input = workflow.value(QStringLiteral("input")).toObject();
    printSummaryLine(stream,
                     QStringLiteral("input delivered=%1 reason=%2")
                             .arg(input.value(QStringLiteral("delivered")).toBool()
                                          ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(input.value(QStringLiteral("reason")).toString(QStringLiteral("<none>"))));

    const QJsonObject verification = workflow.value(QStringLiteral("verification")).toObject();
    printSummaryLine(stream,
                     QStringLiteral("verification passed=%1 property=%2 operator=%3 expected=%4 before=%5 after=%6")
                             .arg(verification.value(QStringLiteral("passed")).toBool()
                                          ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(verification.value(QStringLiteral("property")).toString())
                             .arg(verification.value(QStringLiteral("operator")).toString())
                             .arg(jsonValueToSummaryString(verification.value(QStringLiteral("expected"))))
                             .arg(jsonValueToSummaryString(verification.value(QStringLiteral("before"))))
                             .arg(jsonValueToSummaryString(verification.value(QStringLiteral("after")))));

    printSummaryLine(stream, QStringLiteral("events count=%1")
                             .arg(workflow.value(QStringLiteral("events")).toArray().size()));

    const QJsonArray issues = workflow.value(QStringLiteral("issues")).toArray();
    if (!issues.isEmpty()) {
        printSummaryLine(stream, QStringLiteral("issues count=%1").arg(issues.size()));
        for (const QJsonValue &issue : issues)
            printIssueSummary(stream, issue.toObject());
    }
}

void printObjectSummary(const QJsonObject &object)
{
    QTextStream stream(stdout);
    const QString kind = object.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("click-and-verify") || kind == QLatin1String("key-and-verify")) {
        printWorkflowSummary(stream, object);
        return;
    }

    if (object.contains(QStringLiteral("workflow"))) {
        printWorkflowSummary(stream, object.value(QStringLiteral("workflow")).toObject());
        return;
    }

    if (!object.value(QStringLiteral("method")).toString().isEmpty()) {
        printSummaryLine(stream, QStringLiteral("event method=%1")
                                 .arg(object.value(QStringLiteral("method")).toString()));
        return;
    }

    if (object.contains(QStringLiteral("error"))) {
        const QJsonObject error = object.value(QStringLiteral("error")).toObject();
        printSummaryLine(stream, QStringLiteral("error code=%1 message=%2")
                                 .arg(error.value(QStringLiteral("code")).toInt())
                                 .arg(error.value(QStringLiteral("message")).toString()));
        return;
    }

    const QJsonObject result = object.value(QStringLiteral("result")).toObject();
    if (result.contains(QStringLiteral("service"))) {
        printSummaryLine(stream, QStringLiteral("session service=%1 protocol=%2 qt=%3")
                                 .arg(result.value(QStringLiteral("service")).toString())
                                 .arg(result.value(QStringLiteral("protocolVersion")).toString())
                                 .arg(result.value(QStringLiteral("qtVersion")).toString()));
    }

    if (result.contains(QStringLiteral("enabled"))) {
        printSummaryLine(stream, QStringLiteral("subscription enabled=%1 replayed=%2")
                                 .arg(result.value(QStringLiteral("enabled")).toBool()
                                              ? QStringLiteral("true") : QStringLiteral("false"))
                                 .arg(result.value(QStringLiteral("replayed")).toInt()));
    }

    if (result.contains(QStringLiteral("delivered"))) {
        printSummaryLine(stream, QStringLiteral("input delivered=%1 reason=%2")
                                 .arg(result.value(QStringLiteral("delivered")).toBool()
                                              ? QStringLiteral("true") : QStringLiteral("false"))
                                 .arg(result.value(QStringLiteral("reason")).toString(QStringLiteral("<none>"))));
    }

    if (result.contains(QStringLiteral("focused"))) {
        printSummaryLine(stream, QStringLiteral("focus focused=%1 activeFocus=%2 reason=%3")
                                 .arg(result.value(QStringLiteral("focused")).toBool()
                                              ? QStringLiteral("true") : QStringLiteral("false"))
                                 .arg(result.value(QStringLiteral("activeFocus")).toBool()
                                              ? QStringLiteral("true") : QStringLiteral("false"))
                                 .arg(result.value(QStringLiteral("reason")).toString(QStringLiteral("<none>"))));
    }

    if (result.contains(QStringLiteral("captured"))) {
        printSummaryLine(stream,
                         QStringLiteral("screenshot captured=%1 reason=%2 windowId=%3 size=%4x%5 format=%6 encoding=%7 bytes=%8")
                                 .arg(result.value(QStringLiteral("captured")).toBool()
                                              ? QStringLiteral("true") : QStringLiteral("false"))
                                 .arg(result.value(QStringLiteral("reason")).toString(QStringLiteral("<none>")))
                                 .arg(result.value(QStringLiteral("windowId")).toInt(-1))
                                 .arg(result.value(QStringLiteral("width")).toInt())
                                 .arg(result.value(QStringLiteral("height")).toInt())
                                 .arg(result.value(QStringLiteral("format")).toString(QStringLiteral("<none>")))
                                 .arg(result.value(QStringLiteral("encoding")).toString(QStringLiteral("<none>")))
                                 .arg(result.contains(QStringLiteral("data"))
                                              ? result.value(QStringLiteral("data")).toString().toLatin1().size()
                                              : result.value(QStringLiteral("byteSize")).toInt()));
        if (result.value(QStringLiteral("dataOmitted")).toBool(false))
            printSummaryLine(stream, QStringLiteral("screenshot data omitted; pass --out file.png to write PNG bytes, or --include-data/includeData:true only when base64 fallback bytes are needed"));
    }

    const QJsonArray windows = result.value(QStringLiteral("windows")).toArray();
    if (!windows.isEmpty()) {
        printSummaryLine(stream, QStringLiteral("windows count=%1").arg(windows.size()));
        for (const QJsonValue &windowValue : windows) {
            const QJsonObject window = windowValue.toObject();
            printSummaryLine(stream, QStringLiteral("window windowId=%1 size=%2x%3 title=%4")
                                     .arg(window.value(QStringLiteral("windowId")).toInt(-1))
                                     .arg(window.value(QStringLiteral("width")).toInt())
                                     .arg(window.value(QStringLiteral("height")).toInt())
                                     .arg(window.value(QStringLiteral("title")).toString()));
            printTreeNodeSummary(stream, window.value(QStringLiteral("root")).toObject(), 1);
        }
    }

    const QJsonArray matches = result.value(QStringLiteral("matches")).toArray();
    if (!matches.isEmpty()) {
        printSummaryLine(stream, QStringLiteral("matches count=%1").arg(matches.size()));
        for (const QJsonValue &match : matches)
            printNodeSummary(stream, match.toObject(), QStringLiteral("match"));
    }

    if (result.contains(QStringLiteral("node")))
        printNodeSummary(stream, result.value(QStringLiteral("node")).toObject());

    const QJsonArray issues = result.value(QStringLiteral("issues")).toArray();
    if (!issues.isEmpty()) {
        printSummaryLine(stream, QStringLiteral("issues count=%1").arg(issues.size()));
        for (const QJsonValue &issue : issues)
            printIssueSummary(stream, issue.toObject());
    }

    if (object.contains(QStringLiteral("verification"))) {
        const QJsonObject verification = object.value(QStringLiteral("verification")).toObject();
        printSummaryLine(stream,
                         QStringLiteral("verification passed=%1 property=%2 operator=%3 expected=%4 actual=%5 matchCount=%6")
                                 .arg(verification.value(QStringLiteral("passed")).toBool()
                                              ? QStringLiteral("true") : QStringLiteral("false"))
                                 .arg(verification.value(QStringLiteral("property")).toString())
                                 .arg(verification.value(QStringLiteral("operator")).toString())
                                 .arg(jsonValueToSummaryString(verification.value(QStringLiteral("expected"))))
                                 .arg(jsonValueToSummaryString(verification.value(QStringLiteral("actual"))))
                                 .arg(verification.value(QStringLiteral("matchCount")).toInt()));
    }
}
