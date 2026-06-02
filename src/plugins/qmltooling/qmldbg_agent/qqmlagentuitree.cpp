// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qqmlagentuitree_p.h"

#include "qqmlagentactionability_p.h"
#include "qqmlagentdiagnostics_p.h"
#include "qqmlagentsourceresolver_p.h"

#include <QtCore/qjsonarray.h>
#include <QtCore/qelapsedtimer.h>
#include <QtCore/qeventloop.h>
#include <QtCore/qhash.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qproperty.h>
#include <QtCore/qregularexpression.h>
#include <QtCore/qset.h>
#include <QtCore/qtimer.h>
#include <QtCore/qvariant.h>
#include <QtCore/qvector.h>
#include <QtGui/qcolor.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qwindow.h>
#include <QtQml/qqmlcontext.h>
#include <QtQml/qqmlengine.h>
#include <QtQuick/qquickitem.h>
#include <QtQuick/qquickwindow.h>

#include <private/qqmldebugservice_p.h>
#include <private/qqmlcontextdata_p.h>
#include <private/qqmldata_p.h>
#include <private/qqmlmetatype_p.h>

QT_BEGIN_NAMESPACE

static QJsonObject invalidSelectorDiagnostic(const QString &selectorText);

static QRectF itemBoxInWindow(const QQuickItem *item)
{
    if (!item)
        return {};
    return item->mapRectToScene(QRectF(QPointF(0, 0), QSizeF(item->width(), item->height())));
}

static QJsonArray rectArray(const QRectF &rect)
{
    return { rect.x(), rect.y(), rect.width(), rect.height() };
}

static bool itemInsideViewport(const QQuickItem *item)
{
    if (!item || !item->window())
        return false;
    return QRectF(QPointF(0, 0), item->window()->size()).contains(itemBoxInWindow(item));
}

static QJsonObject itemViewportState(const QQuickItem *item)
{
    if (!item || !item->window()) {
        return {
            { QStringLiteral("available"), false },
            { QStringLiteral("fullyInside"), false },
            { QStringLiteral("partiallyInside"), false },
            { QStringLiteral("centerInside"), false },
        };
    }

    const QRectF bbox = itemBoxInWindow(item);
    const QRectF viewport(QPointF(0, 0), item->window()->size());
    const QRectF intersection = bbox.intersected(viewport);
    return {
        { QStringLiteral("available"), true },
        { QStringLiteral("viewport"), rectArray(viewport) },
        { QStringLiteral("bbox"), rectArray(bbox) },
        { QStringLiteral("visibleRect"), rectArray(intersection) },
        { QStringLiteral("fullyInside"), viewport.contains(bbox) },
        { QStringLiteral("partiallyInside"), !intersection.isEmpty() },
        { QStringLiteral("centerInside"), viewport.contains(bbox.center()) },
        { QStringLiteral("clippedByViewport"), !intersection.isEmpty() && !viewport.contains(bbox) },
    };
}

static QString prettyTypeName(QObject *object)
{
    const QString qmlType = QQmlMetaType::prettyTypeName(object);
    if (!qmlType.isEmpty())
        return qmlType;
    return QString::fromUtf8(object->metaObject()->className());
}

static QJsonArray typeAliases(QObject *object, const QString &primaryType)
{
    QJsonArray aliases;
    QSet<QString> seen{ primaryType };
    const QMetaObject *metaObject = object ? object->metaObject() : nullptr;
    if (metaObject) {
        QString className = QString::fromUtf8(metaObject->className());
        const int qmlTypeMarker = className.indexOf(QLatin1String("_QML"));
        if (qmlTypeMarker > 0)
            className.truncate(qmlTypeMarker);

        const auto appendAlias = [&](const QString &alias) {
            if (alias.isEmpty()
                    || alias == QLatin1String("QObject")
                    || alias == QLatin1String("QQuickItem")
                    || seen.contains(alias)) {
                return;
            }
            seen.insert(alias);
            aliases.append(alias);
        };

        appendAlias(className);
        if (className.startsWith(QLatin1String("QQuick")) && className.size() > 6)
            appendAlias(className.mid(6));
    }
    return aliases;
}

static QString qmlIdForObject(const QObject *object)
{
    const QQmlData *data = QQmlData::get(object);
    if (!data)
        return {};

    if (data->outerContext) {
        const QString id = data->outerContext->findObjectId(object);
        if (!id.isEmpty())
            return id;
    }

    if (data->context) {
        const QString id = data->context->findObjectId(object);
        if (!id.isEmpty())
            return id;
    }

    if (data->ownContext) {
        const QString id = data->ownContext->findObjectId(object);
        if (!id.isEmpty())
            return id;
    }

    return {};
}

static QJsonObject selector(const QString &kind, const QString &value, const QString &stability,
                            const QString &reason = {})
{
    QJsonObject object{
        { QStringLiteral("kind"), kind },
        { QStringLiteral("value"), value },
        { QStringLiteral("stability"), stability },
    };
    if (!reason.isEmpty())
        object.insert(QStringLiteral("reason"), reason);
    return object;
}

static QString sourceLocationSelectorValue(const QJsonObject &location)
{
    const QString file = location.value(QStringLiteral("file")).toString();
    if (file.isEmpty())
        return {};

    const int line = location.value(QStringLiteral("line")).toInt(-1);
    if (line <= 0)
        return file;

    const int column = location.value(QStringLiteral("column")).toInt(-1);
    if (column <= 0)
        return QStringLiteral("%1:%2").arg(file).arg(line);
    return QStringLiteral("%1:%2:%3").arg(file).arg(line).arg(column);
}

static QJsonValue jsonValueFromVariant(const QVariant &value)
{
    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return value.toLongLong();
    case QMetaType::Float:
    case QMetaType::Double:
        return value.toDouble();
    case QMetaType::QString:
        return value.toString();
    case QMetaType::QColor: {
        const QColor color = qvariant_cast<QColor>(value);
        if (!color.isValid())
            return QJsonValue();
        if (color.alpha() == 255)
            return color.name(QColor::HexRgb);
        return color.name(QColor::HexArgb);
    }
    case QMetaType::QPoint:
    case QMetaType::QPointF: {
        const QPointF point = value.toPointF();
        return QJsonObject{
            { QStringLiteral("x"), point.x() },
            { QStringLiteral("y"), point.y() },
        };
    }
    case QMetaType::QSize:
    case QMetaType::QSizeF: {
        const QSizeF size = value.toSizeF();
        return QJsonObject{
            { QStringLiteral("width"), size.width() },
            { QStringLiteral("height"), size.height() },
        };
    }
    case QMetaType::QRect:
    case QMetaType::QRectF: {
        const QRectF rect = value.toRectF();
        return rectArray(rect);
    }
    default:
        return QJsonValue::fromVariant(value);
    }
}

struct TreeBuildOptions
{
    bool includeInvisible = false;
    bool includeSource = true;
    QStringList properties;
    QSet<QString> fields;
    int maxNodes = -1;
    bool collapseRepeated = false;
};

struct TreeBuildState
{
    int nodeCount = 0;
    int omittedNodeCount = 0;
    bool truncated = false;
};

struct SelectorCriteria
{
    QString kind;
    QString value;
    bool hasIndex = false;
    int index = -1;
    bool hasRow = false;
    int row = -1;
    bool hasColumn = false;
    int column = -1;
};

static bool fieldRequested(const TreeBuildOptions &options, const QString &field)
{
    return options.fields.isEmpty() || options.fields.contains(field);
}

static void insertField(QJsonObject *node, const TreeBuildOptions &options,
                        const QString &field, const QJsonValue &value)
{
    if (fieldRequested(options, field))
        node->insert(field, value);
}

static bool isLikelyFrameworkInternal(const QString &qmlId, const QString &objectName,
                                      const QJsonObject &sourceLocation)
{
    if (qmlId.isEmpty() || !objectName.isEmpty())
        return false;

    static const QSet<QString> commonInternalIds{
        QStringLiteral("background"),
        QStringLiteral("contentItem"),
        QStringLiteral("control"),
        QStringLiteral("indicator"),
        QStringLiteral("handle"),
    };
    if (!commonInternalIds.contains(qmlId))
        return false;

    const QString file = sourceLocation.value(QStringLiteral("file")).toString();
    return file.startsWith(QLatin1String("qrc:/qt-project.org/imports/"))
            || file.contains(QLatin1String("/QtQuick/Controls/"));
}

static QString repeatedNodeKey(const QJsonObject &node)
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

static QJsonObject repeatedNodeSummary(const QJsonObject &node, int count)
{
    const QString qmlId = node.value(QStringLiteral("qmlId")).toString();
    const QString objectName = node.value(QStringLiteral("objectName")).toString();
    QString selector;
    if (!qmlId.isEmpty())
        selector = QStringLiteral("id=\"%1\"").arg(qmlId);
    else if (!objectName.isEmpty())
        selector = QStringLiteral("objectName=\"%1\"").arg(objectName);

    QJsonObject summary{
        { QStringLiteral("kind"), QStringLiteral("RepeatedNodes") },
        { QStringLiteral("type"), node.value(QStringLiteral("type")) },
        { QStringLiteral("objectName"), node.value(QStringLiteral("objectName")) },
        { QStringLiteral("count"), count },
        { QStringLiteral("collapsed"), true },
        { QStringLiteral("children"), QJsonArray() },
    };
    if (!qmlId.isEmpty())
        summary.insert(QStringLiteral("qmlId"), qmlId);
    if (node.contains(QStringLiteral("sourceLocation")))
        summary.insert(QStringLiteral("sourceLocation"), node.value(QStringLiteral("sourceLocation")));
    if (!selector.isEmpty()) {
        summary.insert(QStringLiteral("nextHints"), QJsonArray{
            QJsonObject{
                { QStringLiteral("method"), QStringLiteral("UI.query") },
                { QStringLiteral("params"), QJsonObject{
                    { QStringLiteral("selector"), selector },
                    { QStringLiteral("includeSource"), true },
                    { QStringLiteral("maxNodes"), 20 },
                } },
                { QStringLiteral("reason"), QStringLiteral("Query the repeated nodes represented by this collapsed summary.") },
            },
            QJsonObject{
                { QStringLiteral("method"), QStringLiteral("UI.getTree") },
                { QStringLiteral("params"), QJsonObject{
                    { QStringLiteral("selector"), selector },
                    { QStringLiteral("depth"), 1 },
                    { QStringLiteral("includeSource"), true },
                    { QStringLiteral("collapseRepeated"), false },
                } },
                { QStringLiteral("reason"), QStringLiteral("Expand this repeated branch with children when selector context is needed.") },
            },
        });
    }
    return summary;
}

static QString stableIdForDelegateSelector(const QJsonObject &node)
{
    const QString qmlId = node.value(QStringLiteral("qmlId")).toString();
    if (!qmlId.isEmpty())
        return qmlId;
    return node.value(QStringLiteral("objectName")).toString();
}

static QString stableIdKindForDelegateSelector(const QJsonObject &node)
{
    if (!node.value(QStringLiteral("qmlId")).toString().isEmpty())
        return QStringLiteral("id");
    if (!node.value(QStringLiteral("objectName")).toString().isEmpty())
        return QStringLiteral("objectName");
    return {};
}

static int readableDelegateModelIndex(QObject *object)
{
    if (!object)
        return -1;

    const int propertyIndex = object->metaObject()->indexOfProperty("index");
    if (propertyIndex < 0)
        return -1;

    bool ok = false;
    const int index = object->property("index").toInt(&ok);
    if (ok && index >= 0)
        return index;

    QQmlContext *context = QQmlEngine::contextForObject(object);
    if (!context)
        return -1;

    ok = false;
    const int contextIndex = context->contextProperty(QStringLiteral("index")).toInt(&ok);
    return ok && contextIndex >= 0 ? contextIndex : -1;
}

static int readableIntegerValue(QObject *object, const QString &name)
{
    if (!object)
        return -1;

    const QByteArray propertyName = name.toUtf8();
    const int propertyIndex = object->metaObject()->indexOfProperty(propertyName.constData());
    if (propertyIndex >= 0) {
        bool ok = false;
        const int value = object->property(propertyName.constData()).toInt(&ok);
        if (ok && value >= 0)
            return value;
    }

    QQmlContext *context = QQmlEngine::contextForObject(object);
    if (!context)
        return -1;

    bool ok = false;
    const int contextValue = context->contextProperty(name).toInt(&ok);
    return ok && contextValue >= 0 ? contextValue : -1;
}

static int readableDelegateRow(QObject *object)
{
    return readableIntegerValue(object, QStringLiteral("row"));
}

static int readableDelegateColumn(QObject *object)
{
    return readableIntegerValue(object, QStringLiteral("column"));
}

static bool isVirtualizedViewType(const QString &typeName)
{
    return typeName.contains(QLatin1String("ListView"))
            || typeName.contains(QLatin1String("GridView"))
            || typeName.contains(QLatin1String("TableView"))
            || typeName.contains(QLatin1String("TreeView"));
}

static bool hasVirtualizedViewAncestor(QObject *object)
{
    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    while (item) {
        item = item->parentItem();
        if (item && isVirtualizedViewType(prettyTypeName(item)))
            return true;
    }
    return false;
}

static bool hasTableLikeViewAncestor(QObject *object)
{
    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    while (item) {
        item = item->parentItem();
        if (!item)
            continue;
        const QString typeName = prettyTypeName(item);
        if (typeName.contains(QLatin1String("TableView"))
                || typeName.contains(QLatin1String("TreeView"))) {
            return true;
        }
    }
    return false;
}

static bool delegateIndexSourceSupportsSelector(const QString &indexSource)
{
    return indexSource == QLatin1String("modelIndex");
}

static bool delegateCellSourceSupportsSelector(const QString &cellSource)
{
    return cellSource == QLatin1String("delegateRowColumn");
}

static QJsonObject applyDelegateContext(QJsonObject node, int delegateIndex,
                                        const QString &indexSource)
{
    QJsonObject delegate = node.value(QStringLiteral("delegate")).toObject();
    delegate.insert(QStringLiteral("isDelegate"), true);
    delegate.insert(QStringLiteral("index"), delegateIndex);
    delegate.insert(QStringLiteral("indexSource"), indexSource);
    node.insert(QStringLiteral("delegate"), delegate);

    const QString stableIdKind = stableIdKindForDelegateSelector(node);
    const QString stableId = stableIdForDelegateSelector(node);
    if (!stableIdKind.isEmpty() && delegateIndexSourceSupportsSelector(indexSource)) {
        QJsonArray selectors = node.value(QStringLiteral("selectors")).toArray();
        selectors.append(selector(
                QStringLiteral("%1+index").arg(stableIdKind),
                QStringLiteral("%1=\"%2\" index=%3").arg(stableIdKind, stableId)
                        .arg(delegateIndex),
                QStringLiteral("medium"),
                QStringLiteral("index changes when model order changes")));
        node.insert(QStringLiteral("selectors"), selectors);
    }

    QJsonArray annotatedChildren;
    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &child : children)
        annotatedChildren.append(applyDelegateContext(child.toObject(), delegateIndex, indexSource));
    if (!children.isEmpty())
        node.insert(QStringLiteral("children"), annotatedChildren);
    return node;
}

static QJsonObject applyDelegateCellContext(QJsonObject node, int row, int column,
                                            const QString &cellSource)
{
    QJsonObject delegate = node.value(QStringLiteral("delegate")).toObject();
    delegate.insert(QStringLiteral("isDelegate"), true);
    delegate.insert(QStringLiteral("row"), row);
    delegate.insert(QStringLiteral("column"), column);
    delegate.insert(QStringLiteral("cellSource"), cellSource);
    node.insert(QStringLiteral("delegate"), delegate);

    const QString stableIdKind = stableIdKindForDelegateSelector(node);
    const QString stableId = stableIdForDelegateSelector(node);
    if (!stableIdKind.isEmpty() && row >= 0 && column >= 0
            && delegateCellSourceSupportsSelector(cellSource)) {
        QJsonArray selectors = node.value(QStringLiteral("selectors")).toArray();
        selectors.append(selector(
                QStringLiteral("%1+row+column").arg(stableIdKind),
                QStringLiteral("%1=\"%2\" row=%3 column=%4").arg(stableIdKind, stableId)
                        .arg(row).arg(column),
                QStringLiteral("medium"),
                QStringLiteral("row/column changes when model layout changes")));
        node.insert(QStringLiteral("selectors"), selectors);
    }

    QJsonArray annotatedChildren;
    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &child : children)
        annotatedChildren.append(applyDelegateCellContext(child.toObject(), row, column,
                                                         cellSource));
    if (!children.isEmpty())
        node.insert(QStringLiteral("children"), annotatedChildren);
    return node;
}

static QJsonObject withDelegateMetadata(QJsonObject node, QObject *object, int delegateIndex)
{
    QJsonObject source = node.value(QStringLiteral("sourceLocation")).toObject();
    if (source.isEmpty() && object)
        source = QQmlAgentSourceResolver::sourceLocationForObject(object);
    const bool sourceLooksLikeDelegate = source.value(QStringLiteral("method")).toString()
            == QLatin1String("qqmldata-delegate");

    const int row = readableDelegateRow(object);
    const int column = readableDelegateColumn(object);
    if (row >= 0 && column >= 0 && hasTableLikeViewAncestor(object))
        return applyDelegateCellContext(node, row, column, QStringLiteral("delegateRowColumn"));

    const int modelIndex = readableDelegateModelIndex(object);
    if (modelIndex >= 0)
        return applyDelegateContext(node, modelIndex, QStringLiteral("modelIndex"));

    if (!sourceLooksLikeDelegate)
        return node;

    if (!hasVirtualizedViewAncestor(object))
        return applyDelegateContext(node, delegateIndex, QStringLiteral("creationOrder"));
    return applyDelegateContext(node, delegateIndex, QStringLiteral("visualOrder"));
}

static QJsonArray collapseRepeatedChildren(const QJsonArray &children)
{
    QJsonArray collapsed;
    for (int i = 0; i < children.size();) {
        const QJsonObject child = children.at(i).toObject();
        const QString repeatKey = repeatedNodeKey(child);
        int count = 1;
        while (i + count < children.size()
               && repeatedNodeKey(children.at(i + count).toObject()) == repeatKey) {
            ++count;
        }

        if (count >= 3) {
            collapsed.append(repeatedNodeSummary(child, count));
        } else {
            for (int j = 0; j < count; ++j)
                collapsed.append(children.at(i + j));
        }
        i += count;
    }
    return collapsed;
}

static QJsonObject nodeForObjectInternal(QObject *object, int windowId, int depth,
                                         const TreeBuildOptions &options, QSet<QObject *> *seen,
                                         TreeBuildState *state, const QString &visualPath)
{
    if (!object)
        return {};

    QSet<QObject *> localSeen;
    if (!seen)
        seen = &localSeen;
    if (seen->contains(object))
        return {};
    seen->insert(object);

    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    if (item && !options.includeInvisible && !item->isVisible())
        return {};
    if (state && options.maxNodes >= 0 && state->nodeCount >= options.maxNodes) {
        ++state->omittedNodeCount;
        state->truncated = true;
        return {};
    }
    if (state)
        ++state->nodeCount;

    const int nodeId = QQmlDebugService::idForObject(object);
    const QString typeName = prettyTypeName(object);
    const QJsonArray aliases = typeAliases(object, typeName);
    const QString qmlId = qmlIdForObject(object);
    const QString nodeVisualPath = visualPath.isEmpty()
            ? QStringLiteral("/%1[0]").arg(typeName)
            : visualPath;
    QJsonObject node{
    };
    insertField(&node, options, QStringLiteral("nodeId"), nodeId);
    insertField(&node, options, QStringLiteral("windowId"), windowId);
    insertField(&node, options, QStringLiteral("kind"),
                item ? QStringLiteral("QQuickItem") : QStringLiteral("QObject"));
    insertField(&node, options, QStringLiteral("type"), typeName);
    if (!aliases.isEmpty())
        insertField(&node, options, QStringLiteral("typeAliases"), aliases);
    insertField(&node, options, QStringLiteral("objectName"), object->objectName());
    insertField(&node, options, QStringLiteral("visualPath"), nodeVisualPath);
    if (!qmlId.isEmpty())
        insertField(&node, options, QStringLiteral("qmlId"), qmlId);

    const int textIndex = object->metaObject()->indexOfProperty("text");
    if (textIndex >= 0)
        insertField(&node, options, QStringLiteral("text"), object->property("text").toString());

    const int enabledIndex = object->metaObject()->indexOfProperty("enabled");
    if (enabledIndex >= 0)
        insertField(&node, options, QStringLiteral("enabled"), object->property("enabled").toBool());

    if (item) {
        insertField(&node, options, QStringLiteral("visible"), item->isVisible());
        insertField(&node, options, QStringLiteral("opacity"), item->opacity());
        insertField(&node, options, QStringLiteral("bbox"), rectArray(itemBoxInWindow(item)));
        insertField(&node, options, QStringLiteral("insideViewport"), itemInsideViewport(item));
        insertField(&node, options, QStringLiteral("viewport"), itemViewportState(item));
        const QJsonObject interactable = QQmlAgentActionability::state(object);
        insertField(&node, options, QStringLiteral("actionable"),
                    interactable.value(QStringLiteral("ok")).toBool());
        if (options.fields.contains(QStringLiteral("interactable")))
            node.insert(QStringLiteral("interactable"), interactable);
    }

    QJsonArray selectors;
    selectors.append(selector(QStringLiteral("nodeId"), QString::number(nodeId), QStringLiteral("high")));
    if (!qmlId.isEmpty())
        selectors.append(selector(QStringLiteral("id"), qmlId, QStringLiteral("high")));
    if (!object->objectName().isEmpty())
        selectors.append(selector(QStringLiteral("objectName"), object->objectName(), QStringLiteral("high")));
    if (textIndex >= 0 && !object->property("text").toString().isEmpty()) {
        selectors.append(selector(QStringLiteral("text"), object->property("text").toString(),
                                  QStringLiteral("low"), QStringLiteral("text may be translated")));
    }
    selectors.append(selector(QStringLiteral("type"), typeName, QStringLiteral("medium")));
    for (const QJsonValue &alias : aliases) {
        selectors.append(selector(QStringLiteral("type"), alias.toString(), QStringLiteral("medium"),
                                  QStringLiteral("runtime base type alias")));
    }
    selectors.append(selector(QStringLiteral("visualPath"), nodeVisualPath, QStringLiteral("low"),
                              QStringLiteral("depends on visual sibling order")));

    QJsonObject sourceLocation;
    if (options.includeSource || !qmlId.isEmpty())
        sourceLocation = QQmlAgentSourceResolver::sourceLocationForObject(object);

    if (options.includeSource) {
        insertField(&node, options, QStringLiteral("sourceLocation"), sourceLocation);

        const QString sourceSelector = sourceLocationSelectorValue(sourceLocation);
        if (!sourceSelector.isEmpty()) {
            selectors.append(selector(QStringLiteral("sourceLocation"), sourceSelector,
                                      QStringLiteral("medium"),
                                      QStringLiteral("line and column confidence depends on source metadata")));
        }
    }
    if (options.includeSource && isLikelyFrameworkInternal(qmlId, object->objectName(), sourceLocation))
        insertField(&node, options, QStringLiteral("frameworkInternal"), true);
    insertField(&node, options, QStringLiteral("selectors"), selectors);

    if (!options.properties.isEmpty()) {
        QJsonObject propertyObject;
        for (const QString &property : options.properties) {
            const int index = object->metaObject()->indexOfProperty(property.toUtf8().constData());
            if (index >= 0)
                propertyObject.insert(property, jsonValueFromVariant(object->property(property.toUtf8())));
        }
        if (!propertyObject.isEmpty())
            insertField(&node, options, QStringLiteral("properties"), propertyObject);
    }

    if (depth != 0 && item) {
        QJsonArray children;
        QHash<QString, int> delegateIndexes;
        const int childDepth = depth < 0 ? -1 : depth - 1;
        int childIndex = 0;
        for (QQuickItem *child : item->childItems()) {
            const QString childTypeName = prettyTypeName(child);
            const QString childVisualPath = QStringLiteral("%1/%2[%3]")
                    .arg(nodeVisualPath, childTypeName)
                    .arg(childIndex);
            ++childIndex;
            QJsonObject childNode = nodeForObjectInternal(child, windowId, childDepth, options,
                                                          seen, state, childVisualPath);
            if (!childNode.isEmpty()) {
                const QString repeatKey = repeatedNodeKey(childNode);
                const int delegateIndex = delegateIndexes.value(repeatKey, 0);
                delegateIndexes.insert(repeatKey, delegateIndex + 1);
                childNode = withDelegateMetadata(childNode, child, delegateIndex);
                children.append(childNode);
            }
        }
        if (options.collapseRepeated)
            children = collapseRepeatedChildren(children);
        node.insert(QStringLiteral("children"), children);
    } else {
        node.insert(QStringLiteral("children"), QJsonArray());
    }

    return node;
}

QJsonObject QQmlAgentUiTree::nodeForObject(QObject *object, int windowId, int depth,
                                           bool includeInvisible, bool includeSource,
                                           const QStringList &properties, QSet<QObject *> *seen,
                                           const QString &visualPath)
{
    TreeBuildOptions options;
    options.includeInvisible = includeInvisible;
    options.includeSource = includeSource;
    options.properties = properties;
    return nodeForObjectInternal(object, windowId, depth, options, seen, nullptr, visualPath);
}

static QSet<QString> fieldsFromParams(const QJsonObject &params)
{
    QSet<QString> fields;
    const QJsonArray fieldArray = params.value(QStringLiteral("fields")).toArray();
    for (const QJsonValue &field : fieldArray) {
        if (field.isString())
            fields.insert(field.toString());
    }
    return fields;
}

static QStringList propertiesFromParams(const QJsonObject &params)
{
    QStringList properties;
    const QJsonArray propertyArray = params.value(QStringLiteral("properties")).toArray();
    for (const QJsonValue &property : propertyArray) {
        if (property.isString())
            properties.append(property.toString());
    }
    return properties;
}

static bool parseSelector(const QString &selectorText, QString *kind, QString *value);
static bool parseSelector(const QString &selectorText, SelectorCriteria *criteria);
static bool nodeMatchesSelector(const QJsonObject &node, const QString &kind, const QString &value);
static bool nodeMatchesSelector(const QJsonObject &node, const SelectorCriteria &criteria);
static QJsonArray collectQueryMatches(const SelectorCriteria &criteria,
                                      const TreeBuildOptions &matchOptions,
                                      const TreeBuildOptions &resultOptions,
                                      int resultDepth, int maxMatches, bool *truncated);

static QJsonObject pruneToSelector(const QJsonObject &node, const QString &kind, const QString &value)
{
    const bool matched = nodeMatchesSelector(node, kind, value);
    QJsonArray prunedChildren;
    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &child : children) {
        const QJsonObject prunedChild = pruneToSelector(child.toObject(), kind, value);
        if (!prunedChild.isEmpty())
            prunedChildren.append(prunedChild);
    }

    if (!matched && prunedChildren.isEmpty())
        return {};

    QJsonObject pruned = node;
    pruned.insert(QStringLiteral("children"), matched ? children : prunedChildren);
    if (!matched)
        pruned.insert(QStringLiteral("matchAncestor"), true);
    return pruned;
}

static QJsonObject pruneToSelector(const QJsonObject &node, const SelectorCriteria &criteria)
{
    const bool matched = nodeMatchesSelector(node, criteria);
    QJsonArray prunedChildren;
    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &child : children) {
        const QJsonObject prunedChild = pruneToSelector(child.toObject(), criteria);
        if (!prunedChild.isEmpty())
            prunedChildren.append(prunedChild);
    }

    if (!matched && prunedChildren.isEmpty())
        return {};

    QJsonObject pruned = node;
    pruned.insert(QStringLiteral("children"), matched ? children : prunedChildren);
    if (!matched)
        pruned.insert(QStringLiteral("matchAncestor"), true);
    return pruned;
}

static QString fieldForSelectorKind(const QString &kind)
{
    if (kind == QLatin1String("id") || kind == QLatin1String("qmlId"))
        return QStringLiteral("qmlId");
    if (kind == QLatin1String("sourceLocation"))
        return QStringLiteral("sourceLocation");
    if (kind == QLatin1String("nodeId") || kind == QLatin1String("objectName")
            || kind == QLatin1String("text") || kind == QLatin1String("type")
            || kind == QLatin1String("visualPath")) {
        return kind;
    }
    return {};
}

static QJsonObject projectNode(const QJsonObject &node, const QSet<QString> &fields)
{
    if (fields.isEmpty())
        return node;

    QJsonObject projected;
    for (const QString &field : fields) {
        if (node.contains(field))
            projected.insert(field, node.value(field));
    }
    if (fields.contains(QStringLiteral("children")))
        projected.insert(QStringLiteral("children"), node.value(QStringLiteral("children")));
    return projected;
}

QJsonObject QQmlAgentUiTree::getTree(const QJsonObject &params)
{
    const int depth = params.value(QStringLiteral("depth")).toInt(-1);
    TreeBuildOptions options;
    options.includeInvisible = params.value(QStringLiteral("includeInvisible")).toBool(false);
    options.includeSource = params.value(QStringLiteral("includeSource")).toBool(true);
    options.properties = propertiesFromParams(params);
    options.fields = fieldsFromParams(params);
    if (!options.properties.isEmpty() && !options.fields.isEmpty())
        options.fields.insert(QStringLiteral("properties"));
    options.maxNodes = params.contains(QStringLiteral("maxNodes"))
            ? params.value(QStringLiteral("maxNodes")).toInt(-1)
            : -1;
    options.collapseRepeated = params.value(QStringLiteral("collapseRepeated")).toBool(false);

    QString selectorKind;
    QString selectorValue;
    SelectorCriteria selectorCriteria;
    const QString selectorText = params.value(QStringLiteral("selector")).toString();
    const bool hasSelector = !selectorText.isEmpty();
    if (hasSelector && !parseSelector(selectorText, &selectorCriteria)) {
        return {
            { QStringLiteral("windows"), QJsonArray() },
            { QStringLiteral("diagnostics"), QJsonArray{ invalidSelectorDiagnostic(selectorText) } },
        };
    }
    selectorKind = selectorCriteria.kind;
    selectorValue = selectorCriteria.value;
    if (selectorKind == QLatin1String("sourceLocation"))
        options.includeSource = true;
    if (hasSelector && !options.fields.isEmpty()) {
        const QString selectorField = fieldForSelectorKind(selectorKind);
        if (!selectorField.isEmpty())
            options.fields.insert(selectorField);
    }

    QJsonArray windows;
    TreeBuildState state;
    int windowId = 0;
    const QWindowList allWindows = QGuiApplication::allWindows();
    for (QWindow *window : allWindows) {
        QQuickWindow *quickWindow = qobject_cast<QQuickWindow *>(window);
        if (!quickWindow || !quickWindow->contentItem())
            continue;
        ++windowId;
        QSet<QObject *> seen;
        QJsonObject root = nodeForObjectInternal(quickWindow->contentItem(), windowId, depth,
                                                 options, &seen, &state, {});
        if (hasSelector)
            root = pruneToSelector(root, selectorCriteria);
        if (hasSelector && root.isEmpty())
            continue;
        windows.append(QJsonObject{
            { QStringLiteral("windowId"), windowId },
            { QStringLiteral("title"), quickWindow->title() },
            { QStringLiteral("width"), quickWindow->width() },
            { QStringLiteral("height"), quickWindow->height() },
            { QStringLiteral("devicePixelRatio"), quickWindow->devicePixelRatio() },
            { QStringLiteral("root"), root },
        });
    }

    QJsonObject result{ { QStringLiteral("windows"), windows } };
    result.insert(QStringLiteral("nodeCount"), state.nodeCount);
    if (state.truncated) {
        result.insert(QStringLiteral("truncated"), true);
        result.insert(QStringLiteral("omittedNodeCount"), state.omittedNodeCount);
        result.insert(QStringLiteral("nextHints"), QJsonArray{ QJsonObject{
            { QStringLiteral("method"), QStringLiteral("UI.query") },
            { QStringLiteral("reason"), QStringLiteral("Use selector or depth to narrow the tree.") },
        } });
    }
    return result;
}

QObject *QQmlAgentUiTree::objectForNodeId(int nodeId)
{
    QObject *object = QQmlDebugService::objectForId(nodeId);
    if (!object)
        return nullptr;

    if (QQmlDebugService::idForObject(object) != nodeId)
        return nullptr;

    return object;
}

static QJsonObject nodeRefIssue(const QString &id, const QString &message)
{
    return {
        { QStringLiteral("id"), id },
        { QStringLiteral("severity"), QStringLiteral("error") },
        { QStringLiteral("confidence"), 1.0 },
        { QStringLiteral("message"), message },
    };
}

static bool nodeMatchesSelector(const QJsonObject &node, const QString &kind, const QString &value)
{
    if (kind == QLatin1String("nodeId"))
        return node.value(QStringLiteral("nodeId")).toInt() == value.toInt();
    if (kind == QLatin1String("id") || kind == QLatin1String("qmlId"))
        return node.value(QStringLiteral("qmlId")).toString() == value;
    if (kind == QLatin1String("objectName"))
        return node.value(QStringLiteral("objectName")).toString() == value;
    if (kind == QLatin1String("text"))
        return node.value(QStringLiteral("text")).toString() == value;
    if (kind == QLatin1String("type")) {
        if (node.value(QStringLiteral("type")).toString() == value)
            return true;
        const QJsonArray aliases = node.value(QStringLiteral("typeAliases")).toArray();
        for (const QJsonValue &alias : aliases) {
            if (alias.toString() == value)
                return true;
        }
        return false;
    }
    if (kind == QLatin1String("visualPath"))
        return node.value(QStringLiteral("visualPath")).toString() == value;
    if (kind == QLatin1String("sourceLocation"))
        return sourceLocationSelectorValue(node.value(QStringLiteral("sourceLocation")).toObject()) == value;
    return false;
}

static bool nodeMatchesSelector(const QJsonObject &node, const SelectorCriteria &criteria)
{
    if (!nodeMatchesSelector(node, criteria.kind, criteria.value))
        return false;

    const QJsonObject delegate = node.value(QStringLiteral("delegate")).toObject();
    if (criteria.hasIndex && !delegateIndexSourceSupportsSelector(
                delegate.value(QStringLiteral("indexSource")).toString())) {
        return false;
    }
    if (criteria.hasIndex && delegate.value(QStringLiteral("index")).toInt(-1) != criteria.index)
        return false;
    if ((criteria.hasRow || criteria.hasColumn) && !delegateCellSourceSupportsSelector(
                delegate.value(QStringLiteral("cellSource")).toString())) {
        return false;
    }
    if (criteria.hasRow && delegate.value(QStringLiteral("row")).toInt(-1) != criteria.row)
        return false;
    if (criteria.hasColumn && delegate.value(QStringLiteral("column")).toInt(-1) != criteria.column)
        return false;
    return true;
}

static void collectMatches(const QJsonObject &node, const QString &kind, const QString &value,
                           int maxMatches, bool *truncated, QJsonArray *matches)
{
    if (nodeMatchesSelector(node, kind, value)) {
        if (maxMatches >= 0 && matches->size() >= maxMatches) {
            if (truncated)
                *truncated = true;
        } else {
            matches->append(node);
        }
    }

    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &child : children)
        collectMatches(child.toObject(), kind, value, maxMatches, truncated, matches);
}

static void collectMatches(const QJsonObject &node, const SelectorCriteria &criteria,
                           int maxMatches, bool *truncated, QJsonArray *matches)
{
    if (nodeMatchesSelector(node, criteria)) {
        if (maxMatches >= 0 && matches->size() >= maxMatches) {
            if (truncated)
                *truncated = true;
        } else {
            matches->append(node);
        }
    }

    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &child : children)
        collectMatches(child.toObject(), criteria, maxMatches, truncated, matches);
}

static QJsonArray indexedSelectorSuggestions(
        const QJsonArray &matches, const QString &kind, const QString &value)
{
    Q_UNUSED(kind);
    Q_UNUSED(value);

    QJsonArray suggestions;
    QHash<QString, QJsonObject> selectorNodes;
    QHash<QString, int> selectorCounts;
    const qsizetype limit = std::min(qsizetype(20), matches.size());
    for (int i = 0; i < limit; ++i) {
        const QJsonObject node = matches.at(i).toObject();
        const QJsonObject delegate = node.value(QStringLiteral("delegate")).toObject();
        if (!delegate.value(QStringLiteral("isDelegate")).toBool())
            continue;

        QString selectorText;
        QString reason;
        const bool hasCell = delegateCellSourceSupportsSelector(
                delegate.value(QStringLiteral("cellSource")).toString());
        if (hasCell) {
            const int row = delegate.value(QStringLiteral("row")).toInt(-1);
            const int column = delegate.value(QStringLiteral("column")).toInt(-1);
            if (row < 0 || column < 0)
                continue;
            const QString stableIdKind = stableIdKindForDelegateSelector(node);
            const QString stableId = stableIdForDelegateSelector(node);
            if (stableIdKind.isEmpty() || stableId.isEmpty())
                continue;
            selectorText = QStringLiteral("%1=\"%2\" row=%3 column=%4")
                                   .arg(stableIdKind, stableId)
                                   .arg(row).arg(column);
            reason = QStringLiteral("row/column changes when model layout changes");
        } else if (delegateIndexSourceSupportsSelector(
                           delegate.value(QStringLiteral("indexSource")).toString())) {
            const int index = delegate.value(QStringLiteral("index")).toInt(-1);
            if (index < 0)
                continue;

            const QString stableIdKind = stableIdKindForDelegateSelector(node);
            const QString stableId = stableIdForDelegateSelector(node);
            if (stableIdKind.isEmpty() || stableId.isEmpty())
                continue;

            selectorText = QStringLiteral("%1=\"%2\" index=%3")
                                   .arg(stableIdKind, stableId)
                                   .arg(index);
            reason = QStringLiteral("index changes when model order changes");
        } else {
            continue;
        }
        selectorCounts.insert(selectorText, selectorCounts.value(selectorText) + 1);
        QJsonObject selectorNode = node;
        selectorNode.insert(QStringLiteral("_selectorReason"), reason);
        selectorNodes.insert(selectorText, selectorNode);
    }

    QVector<QJsonObject> sortedSuggestions;
    for (auto it = selectorCounts.cbegin(); it != selectorCounts.cend(); ++it) {
        if (it.value() != 1)
            continue;
        const QJsonObject node = selectorNodes.value(it.key());
        sortedSuggestions.append(QJsonObject{
            { QStringLiteral("nodeId"), node.value(QStringLiteral("nodeId")).toInt(-1) },
            { QStringLiteral("selector"), it.key() },
            { QStringLiteral("stability"), QStringLiteral("medium") },
            { QStringLiteral("reason"), node.value(QStringLiteral("_selectorReason")).toString() },
        });
    }

    std::sort(sortedSuggestions.begin(), sortedSuggestions.end(),
              [](const QJsonObject &lhs, const QJsonObject &rhs) {
        const QRegularExpression indexPattern(QStringLiteral("\\b(?:index|row|column)=(\\d+)"));
        const QRegularExpressionMatch lhsMatch =
                indexPattern.match(lhs.value(QStringLiteral("selector")).toString());
        const QRegularExpressionMatch rhsMatch =
                indexPattern.match(rhs.value(QStringLiteral("selector")).toString());
        const int lhsIndex = lhsMatch.hasMatch() ? lhsMatch.captured(1).toInt() : -1;
        const int rhsIndex = rhsMatch.hasMatch() ? rhsMatch.captured(1).toInt() : -1;
        if (lhsIndex != rhsIndex)
            return lhsIndex < rhsIndex;
        return lhs.value(QStringLiteral("selector")).toString()
                < rhs.value(QStringLiteral("selector")).toString();
    });

    for (const QJsonObject &suggestion : sortedSuggestions) {
        suggestions.append(suggestion);
        if (suggestions.size() >= 20)
            break;
    }
    return suggestions;
}

static QJsonArray globallyUniqueIndexedSelectorSuggestions(const QJsonArray &suggestions,
                                                           const TreeBuildOptions &matchOptions,
                                                           const TreeBuildOptions &resultOptions)
{
    QJsonArray filtered;
    for (const QJsonValue &suggestionValue : suggestions) {
        const QJsonObject suggestion = suggestionValue.toObject();
        SelectorCriteria criteria;
        if (!parseSelector(suggestion.value(QStringLiteral("selector")).toString(), &criteria))
            continue;

        TreeBuildOptions verificationOptions = matchOptions;
        const QString selectorField = fieldForSelectorKind(criteria.kind);
        if (!selectorField.isEmpty())
            verificationOptions.fields.insert(selectorField);

        bool truncated = false;
        const QJsonArray selectorMatches = collectQueryMatches(criteria, verificationOptions,
                                                               resultOptions, 0, 2,
                                                               &truncated);
        if (!truncated && selectorMatches.size() == 1)
            filtered.append(suggestion);
    }
    return filtered;
}

static void collectQueryMatchesFromObject(QObject *object, int windowId, const SelectorCriteria &criteria,
                                          const TreeBuildOptions &matchOptions,
                                          const TreeBuildOptions &resultOptions,
                                          int resultDepth, int maxMatches, bool *truncated,
                                          QJsonArray *matches, QSet<QObject *> *seen,
                                          const QString &visualPath, int delegateIndex = -1,
                                          int inheritedDelegateIndex = -1,
                                          const QString &inheritedDelegateIndexSource = {},
                                          int inheritedDelegateRow = -1,
                                          int inheritedDelegateColumn = -1,
                                          const QString &inheritedDelegateCellSource = {})
{
    if (!object || seen->contains(object))
        return;
    seen->insert(object);

    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    if (item && !matchOptions.includeInvisible && !item->isVisible())
        return;

    const QString typeName = prettyTypeName(object);
    const QString nodeVisualPath = visualPath.isEmpty()
            ? QStringLiteral("/%1[0]").arg(typeName)
            : visualPath;

    QJsonObject matchNode = nodeForObjectInternal(object, windowId, 0, matchOptions, nullptr,
                                                  nullptr, nodeVisualPath);
    if (inheritedDelegateRow >= 0 && inheritedDelegateColumn >= 0)
        matchNode = applyDelegateCellContext(matchNode, inheritedDelegateRow,
                                             inheritedDelegateColumn,
                                             inheritedDelegateCellSource);
    else if (inheritedDelegateIndex >= 0)
        matchNode = applyDelegateContext(matchNode, inheritedDelegateIndex,
                                         inheritedDelegateIndexSource);
    else if (delegateIndex >= 0)
        matchNode = withDelegateMetadata(matchNode, object, delegateIndex);
    const QJsonObject effectiveDelegate = matchNode.value(QStringLiteral("delegate")).toObject();
    const int effectiveDelegateIndex =
            effectiveDelegate.value(QStringLiteral("index")).toInt(inheritedDelegateIndex);
    const QString effectiveDelegateIndexSource =
            effectiveDelegate.value(QStringLiteral("indexSource")).toString(
                    inheritedDelegateIndexSource);
    const int effectiveDelegateRow =
            effectiveDelegate.value(QStringLiteral("row")).toInt(inheritedDelegateRow);
    const int effectiveDelegateColumn =
            effectiveDelegate.value(QStringLiteral("column")).toInt(inheritedDelegateColumn);
    const QString effectiveDelegateCellSource =
            effectiveDelegate.value(QStringLiteral("cellSource")).toString(
                    inheritedDelegateCellSource);

    if (nodeMatchesSelector(matchNode, criteria)) {
        if (maxMatches >= 0 && matches->size() >= maxMatches) {
            if (truncated)
                *truncated = true;
        } else {
            QJsonObject resultNode = nodeForObjectInternal(object, windowId, resultDepth,
                                                           resultOptions, nullptr, nullptr,
                                                           nodeVisualPath);
            if (inheritedDelegateRow >= 0 && inheritedDelegateColumn >= 0)
                resultNode = applyDelegateCellContext(resultNode, inheritedDelegateRow,
                                                      inheritedDelegateColumn,
                                                      inheritedDelegateCellSource);
            else if (inheritedDelegateIndex >= 0)
                resultNode = applyDelegateContext(resultNode, inheritedDelegateIndex,
                                                  inheritedDelegateIndexSource);
            else if (delegateIndex >= 0)
                resultNode = withDelegateMetadata(resultNode, object, delegateIndex);
            matches->append(resultNode);
        }
    }

    if (!item)
        return;

    QHash<QString, int> delegateIndexes;
    int childIndex = 0;
    for (QQuickItem *child : item->childItems()) {
        const QString childTypeName = prettyTypeName(child);
        const QString childVisualPath = QStringLiteral("%1/%2[%3]")
                .arg(nodeVisualPath, childTypeName)
                .arg(childIndex);
        ++childIndex;

        QJsonObject childMatchNode = nodeForObjectInternal(child, windowId, 0, matchOptions,
                                                           nullptr, nullptr, childVisualPath);
        const QString repeatKey = repeatedNodeKey(childMatchNode);
        const int childDelegateIndex = delegateIndexes.value(repeatKey, 0);
        delegateIndexes.insert(repeatKey, childDelegateIndex + 1);

        collectQueryMatchesFromObject(child, windowId, criteria, matchOptions, resultOptions,
                                      resultDepth, maxMatches, truncated, matches, seen,
                                      childVisualPath, childDelegateIndex, effectiveDelegateIndex,
                                      effectiveDelegateIndexSource, effectiveDelegateRow,
                                      effectiveDelegateColumn, effectiveDelegateCellSource);
    }
}

static QJsonArray collectQueryMatches(const SelectorCriteria &criteria,
                                      const TreeBuildOptions &matchOptions,
                                      const TreeBuildOptions &resultOptions,
                                      int resultDepth, int maxMatches, bool *truncated)
{
    QJsonArray matches;
    int windowId = 0;
    const QWindowList allWindows = QGuiApplication::allWindows();
    for (QWindow *window : allWindows) {
        QQuickWindow *quickWindow = qobject_cast<QQuickWindow *>(window);
        if (!quickWindow || !quickWindow->contentItem())
            continue;
        ++windowId;
        QSet<QObject *> seen;
        collectQueryMatchesFromObject(quickWindow->contentItem(), windowId, criteria,
                                      matchOptions, resultOptions, resultDepth, maxMatches,
                                      truncated, &matches, &seen, {});
    }
    return matches;
}

static bool parseSelector(const QString &selectorText, QString *kind, QString *value)
{
    const int equals = selectorText.indexOf(QLatin1Char('='));
    if (equals < 0) {
        if (selectorText.startsWith(QLatin1String("nodeId"))) {
            *kind = QStringLiteral("nodeId");
            *value = selectorText.mid(6).trimmed();
            return !value->isEmpty();
        }
        return false;
    }

    *kind = selectorText.left(equals).trimmed();
    *value = selectorText.mid(equals + 1).trimmed();
    if (value->startsWith(QLatin1Char('"')) && value->endsWith(QLatin1Char('"')) && value->size() >= 2)
        *value = value->mid(1, value->size() - 2);
    return !kind->isEmpty();
}

static bool parseSelector(const QString &selectorText, SelectorCriteria *criteria)
{
    if (!selectorText.contains(QLatin1Char('=')))
        return parseSelector(selectorText, &criteria->kind, &criteria->value);

    static const QRegularExpression tokenExpression(
            QStringLiteral("(\\w+)\\s*=\\s*(?:\"([^\"]*)\"|(\\S+))"));
    QRegularExpressionMatchIterator it = tokenExpression.globalMatch(selectorText);
    bool sawPrimary = false;
    int consumed = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (match.capturedStart() != consumed)
            return false;
        consumed = match.capturedEnd();
        while (consumed < selectorText.size() && selectorText.at(consumed).isSpace())
            ++consumed;

        const QString kind = match.captured(1);
        const QString value = match.hasCaptured(2) ? match.captured(2) : match.captured(3);
        if (kind == QLatin1String("index")) {
            bool ok = false;
            const int index = value.toInt(&ok);
            if (!ok || index < 0)
                return false;
            criteria->hasIndex = true;
            criteria->index = index;
        } else if (kind == QLatin1String("row")) {
            bool ok = false;
            const int row = value.toInt(&ok);
            if (!ok || row < 0)
                return false;
            criteria->hasRow = true;
            criteria->row = row;
        } else if (kind == QLatin1String("column")) {
            bool ok = false;
            const int column = value.toInt(&ok);
            if (!ok || column < 0)
                return false;
            criteria->hasColumn = true;
            criteria->column = column;
        } else {
            if (sawPrimary)
                return false;
            criteria->kind = kind;
            criteria->value = value;
            sawPrimary = true;
        }
    }

    if (consumed != selectorText.size())
        return false;

    if (!sawPrimary)
        return false;
    if (criteria->hasIndex && (criteria->hasRow || criteria->hasColumn))
        return false;
    if (criteria->hasRow != criteria->hasColumn)
        return false;
    return !criteria->kind.isEmpty();
}

static QJsonArray selectorSyntaxExamples()
{
    return {
        QStringLiteral("id=\"saveButton\""),
        QStringLiteral("objectName=\"settings.save\""),
        QStringLiteral("type=\"Button\""),
        QStringLiteral("text=\"Save\""),
        QStringLiteral("nodeId=42"),
        QStringLiteral("sourceLocation=\"src/Main.qml:44:7\""),
        QStringLiteral("id=\"delegateRoot\" index=4"),
        QStringLiteral("id=\"tableCell\" row=0 column=1"),
    };
}

static QJsonObject invalidSelectorDiagnostic(const QString &selectorText)
{
    return {
        { QStringLiteral("id"), QStringLiteral("selector.invalid") },
        { QStringLiteral("severity"), QStringLiteral("error") },
        { QStringLiteral("message"), QStringLiteral("Unsupported selector syntax.") },
        { QStringLiteral("selector"), selectorText },
        { QStringLiteral("supportedForms"), selectorSyntaxExamples() },
        { QStringLiteral("limitations"), QJsonArray{
            QStringLiteral("Compound predicates such as 'and', 'or', parentheses, and CSS-style selectors are not supported yet."),
            QStringLiteral("Repeated delegates should use id/objectName plus index when delegate index metadata is available."),
            QStringLiteral("TableView-style delegates should use id/objectName plus row and column when cell metadata is available."),
        } },
    };
}

static QJsonArray stableSelectorHints(const QString &kind, int matchCount)
{
    QJsonArray hints{
        QJsonObject{
            { QStringLiteral("selector"), QStringLiteral("id=\"meaningfulId\"") },
            { QStringLiteral("reason"), QStringLiteral("Authored QML ids are stable across launches and are preferred for agent-created or repaired components.") },
        },
        QJsonObject{
            { QStringLiteral("selector"), QStringLiteral("objectName=\"feature.meaningfulName\"") },
            { QStringLiteral("reason"), QStringLiteral("Use a meaningful product/app objectName when an external stable selector is genuinely useful; avoid automation-only test hooks.") },
        },
    };
    if (matchCount > 1) {
        hints.append(QJsonObject{
            { QStringLiteral("selector"), QStringLiteral("id=\"delegateRoot\" index=0") },
            { QStringLiteral("reason"), QStringLiteral("For repeated delegates, prefer id/objectName plus runtime delegate index metadata when available.") },
        });
    }
    if (kind != QLatin1String("type")) {
        hints.append(QJsonObject{
            { QStringLiteral("selector"), QStringLiteral("type=\"Button\"") },
            { QStringLiteral("reason"), QStringLiteral("Type selectors are useful for broad inspection, but often ambiguous for input and verification.") },
        });
    }
    return hints;
}

QJsonObject QQmlAgentUiTree::query(const QJsonObject &params)
{
    SelectorCriteria criteria;
    const QString selectorText = params.value(QStringLiteral("selector")).toString();
    if (!parseSelector(selectorText, &criteria)) {
        return {
            { QStringLiteral("matches"), QJsonArray() },
            { QStringLiteral("diagnostics"), QJsonArray{ invalidSelectorDiagnostic(selectorText) } },
        };
    }
    const QString kind = criteria.kind;
    const QString value = criteria.value;

    const bool includeInvisible = params.value(QStringLiteral("includeInvisible")).toBool(false);

    TreeBuildOptions matchOptions;
    matchOptions.includeInvisible = includeInvisible;
    matchOptions.includeSource = kind == QLatin1String("sourceLocation");
    matchOptions.fields.insert(QStringLiteral("nodeId"));
    matchOptions.fields.insert(QStringLiteral("type"));
    matchOptions.fields.insert(QStringLiteral("visualPath"));
    const QString selectorField = fieldForSelectorKind(kind);
    if (!selectorField.isEmpty())
        matchOptions.fields.insert(selectorField);
    if (kind == QLatin1String("type"))
        matchOptions.fields.insert(QStringLiteral("typeAliases"));

    TreeBuildOptions resultOptions;
    resultOptions.includeInvisible = includeInvisible;
    resultOptions.includeSource = params.value(QStringLiteral("includeSource")).toBool(true)
            || kind == QLatin1String("sourceLocation");
    resultOptions.properties = propertiesFromParams(params);
    resultOptions.fields = fieldsFromParams(params);
    if (!resultOptions.properties.isEmpty() && !resultOptions.fields.isEmpty())
        resultOptions.fields.insert(QStringLiteral("properties"));

    bool truncated = false;
    const int maxMatches = params.contains(QStringLiteral("maxNodes"))
            ? params.value(QStringLiteral("maxNodes")).toInt(-1)
            : -1;
    const int resultDepth = params.contains(QStringLiteral("depth"))
            ? params.value(QStringLiteral("depth")).toInt(0)
            : 0;
    QJsonArray matches = collectQueryMatches(criteria, matchOptions, resultOptions, resultDepth,
                                             maxMatches, &truncated);

    const QSet<QString> fields = fieldsFromParams(params);
    QSet<QString> projectedFields = fields;
    if (!params.value(QStringLiteral("properties")).toArray().isEmpty()
            && !projectedFields.isEmpty()) {
        projectedFields.insert(QStringLiteral("properties"));
    }
    if (!projectedFields.isEmpty()) {
        QJsonArray projectedMatches;
        for (const QJsonValue &match : std::as_const(matches))
            projectedMatches.append(projectNode(match.toObject(), projectedFields));
        matches = projectedMatches;
    }

    QJsonArray diagnostics;
    if (matches.isEmpty()) {
        QJsonArray treeFields{
            QStringLiteral("nodeId"),
            QStringLiteral("qmlId"),
            QStringLiteral("objectName"),
            QStringLiteral("type"),
            QStringLiteral("typeAliases"),
            QStringLiteral("text"),
            QStringLiteral("visualPath"),
            QStringLiteral("sourceLocation"),
            QStringLiteral("selectors"),
            QStringLiteral("delegate"),
            QStringLiteral("frameworkInternal"),
            QStringLiteral("children"),
        };
        const QString selectorField = fieldForSelectorKind(kind);
        if (!selectorField.isEmpty() && !treeFields.contains(selectorField))
            treeFields.append(selectorField);
        const QJsonObject tree = getTree({
            { QStringLiteral("depth"), -1 },
            { QStringLiteral("includeInvisible"), includeInvisible },
            { QStringLiteral("includeSource"), kind == QLatin1String("sourceLocation") },
            { QStringLiteral("fields"), treeFields },
        });
        diagnostics.append(QQmlAgentDiagnostics::selectorNotFound(kind, value, tree));
    } else if (matches.size() > 1) {
        QJsonObject ambiguous{
            { QStringLiteral("id"), QStringLiteral("selector.ambiguous") },
            { QStringLiteral("severity"), QStringLiteral("warning") },
            { QStringLiteral("confidence"), 1.0 },
            { QStringLiteral("message"), QStringLiteral("Selector matched multiple nodes.") },
            { QStringLiteral("selector"), selectorText },
            { QStringLiteral("matchCount"), matches.size() },
            { QStringLiteral("stableSelectorHints"), stableSelectorHints(kind, matches.size()) },
        };
        const QJsonArray indexedSuggestions = globallyUniqueIndexedSelectorSuggestions(
                indexedSelectorSuggestions(matches, kind, value), matchOptions, resultOptions);
        if (!indexedSuggestions.isEmpty())
            ambiguous.insert(QStringLiteral("indexedSelectors"), indexedSuggestions);
        diagnostics.append(ambiguous);
    }

    QJsonObject result{
        { QStringLiteral("matches"), matches },
        { QStringLiteral("diagnostics"), diagnostics },
    };
    if (truncated) {
        result.insert(QStringLiteral("truncated"), true);
        result.insert(QStringLiteral("nextHints"), QJsonArray{ QJsonObject{
            { QStringLiteral("method"), QStringLiteral("UI.query") },
            { QStringLiteral("reason"), QStringLiteral("Use a more specific selector or lower maxNodes.") },
        } });
    }
    return result;
}

namespace {

struct WaitCondition
{
    QString selector;
    QString state;
    QString property;
    QString op;
    QJsonValue expected;
    bool valid = false;
    QJsonArray diagnostics;
};

struct WaitEvaluation
{
    bool satisfied = false;
    QString reason;
    int matchCount = 0;
    int nodeId = -1;
    QJsonValue actual;
    QJsonObject node;
    QJsonArray diagnostics;
};

static constexpr int DefaultWaitTimeoutMs = 1000;
static constexpr int MaxWaitTimeoutMs = 30000;

static QJsonObject waitDiagnostic(const QString &id, const QString &message)
{
    return {
        { QStringLiteral("id"), id },
        { QStringLiteral("severity"), QStringLiteral("error") },
        { QStringLiteral("confidence"), 1.0 },
        { QStringLiteral("message"), message },
    };
}

static WaitCondition waitConditionFromParams(const QJsonObject &params)
{
    WaitCondition condition;
    condition.selector = params.value(QStringLiteral("selector")).toString();
    if (condition.selector.isEmpty()) {
        condition.diagnostics.append(waitDiagnostic(
                QStringLiteral("wait.selector_missing"),
                QStringLiteral("UI.waitFor requires selector.")));
        return condition;
    }

    const QJsonObject until = params.value(QStringLiteral("until")).toObject();
    if (until.isEmpty()) {
        condition.diagnostics.append(waitDiagnostic(
                QStringLiteral("wait.until_missing"),
                QStringLiteral("UI.waitFor requires an until object.")));
        return condition;
    }

    condition.state = until.value(QStringLiteral("state")).toString();
    condition.property = until.value(QStringLiteral("property")).toString();
    condition.op = until.value(QStringLiteral("op")).toString(QStringLiteral("="));
    condition.expected = until.value(QStringLiteral("value"));

    if (!condition.property.isEmpty()) {
        if (condition.op == QLatin1String("=="))
            condition.op = QStringLiteral("=");
        static const QSet<QString> supportedOps{
            QStringLiteral("="),
            QStringLiteral("!="),
            QStringLiteral(">"),
            QStringLiteral(">="),
            QStringLiteral("<"),
            QStringLiteral("<="),
            QStringLiteral("contains"),
            QStringLiteral("startsWith"),
            QStringLiteral("endsWith"),
        };
        if (!supportedOps.contains(condition.op)) {
            condition.diagnostics.append(waitDiagnostic(
                    QStringLiteral("wait.unsupported_operator"),
                    QStringLiteral("Unsupported UI.waitFor property operator.")));
            return condition;
        }
        if (!until.contains(QStringLiteral("value"))) {
            condition.diagnostics.append(waitDiagnostic(
                    QStringLiteral("wait.value_missing"),
                    QStringLiteral("Property UI.waitFor requires value.")));
            return condition;
        }
        condition.valid = true;
        return condition;
    }

    if (condition.state == QLatin1String("found")
            || condition.state == QLatin1String("notFound")) {
        condition.valid = true;
        return condition;
    }

    condition.diagnostics.append(waitDiagnostic(
            QStringLiteral("wait.unsupported_until"),
            QStringLiteral("UI.waitFor supports until.state found/notFound or a property comparison.")));
    return condition;
}

static bool numericValue(const QJsonValue &value, double *number)
{
    if (value.isDouble()) {
        *number = value.toDouble();
        return true;
    }
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().toDouble(&ok);
        if (ok) {
            *number = parsed;
            return true;
        }
    }
    return false;
}

static bool jsonValuesEqual(const QJsonValue &left, const QJsonValue &right)
{
    double leftNumber = 0;
    double rightNumber = 0;
    if (numericValue(left, &leftNumber) && numericValue(right, &rightNumber))
        return qFuzzyCompare(leftNumber + 1.0, rightNumber + 1.0);
    if (left.isBool() || right.isBool())
        return left.toBool() == right.toBool();
    if (left.isNull() || right.isNull())
        return left.isNull() && right.isNull();
    return left.toString() == right.toString();
}

static bool compareJsonValues(const QJsonValue &actual, const QString &op,
                              const QJsonValue &expected, bool *supported)
{
    *supported = true;
    if (op == QLatin1String("="))
        return jsonValuesEqual(actual, expected);
    if (op == QLatin1String("!="))
        return !jsonValuesEqual(actual, expected);
    if (op == QLatin1String("contains")
            || op == QLatin1String("startsWith")
            || op == QLatin1String("endsWith")) {
        if (!actual.isString() || !expected.isString()) {
            *supported = false;
            return false;
        }
        const QString actualString = actual.toString();
        const QString expectedString = expected.toString();
        if (op == QLatin1String("contains"))
            return actualString.contains(expectedString);
        if (op == QLatin1String("startsWith"))
            return actualString.startsWith(expectedString);
        return actualString.endsWith(expectedString);
    }

    double actualNumber = 0;
    double expectedNumber = 0;
    if (!numericValue(actual, &actualNumber) || !numericValue(expected, &expectedNumber)) {
        *supported = false;
        return false;
    }

    if (op == QLatin1String(">"))
        return actualNumber > expectedNumber;
    if (op == QLatin1String(">="))
        return actualNumber >= expectedNumber;
    if (op == QLatin1String("<"))
        return actualNumber < expectedNumber;
    if (op == QLatin1String("<="))
        return actualNumber <= expectedNumber;

    *supported = false;
    return false;
}

static QJsonValue nodePropertyValue(const QJsonObject &node, const QString &property)
{
    const QJsonObject properties = node.value(QStringLiteral("properties")).toObject();
    if (properties.contains(property))
        return properties.value(property);
    if (node.contains(property))
        return node.value(property);
    return QJsonValue(QJsonValue::Undefined);
}

static WaitEvaluation evaluateWaitCondition(const WaitCondition &condition)
{
    QJsonObject queryParams{
        { QStringLiteral("selector"), condition.selector },
        { QStringLiteral("includeSource"), true },
        { QStringLiteral("maxNodes"), 20 },
    };
    if (!condition.property.isEmpty())
        queryParams.insert(QStringLiteral("properties"), QJsonArray{ condition.property });

    const QJsonObject queryResult = QQmlAgentUiTree::query(queryParams);
    const QJsonArray matches = queryResult.value(QStringLiteral("matches")).toArray();

    WaitEvaluation evaluation;
    evaluation.matchCount = matches.size();
    evaluation.diagnostics = queryResult.value(QStringLiteral("diagnostics")).toArray();

    if (condition.property.isEmpty()) {
        evaluation.satisfied = condition.state == QLatin1String("found")
                ? !matches.isEmpty()
                : matches.isEmpty();
        evaluation.reason = evaluation.satisfied
                ? QStringLiteral("predicate_satisfied")
                : QStringLiteral("state_not_satisfied");
        if (!matches.isEmpty()) {
            evaluation.node = matches.first().toObject();
            evaluation.nodeId = evaluation.node.value(QStringLiteral("nodeId")).toInt(-1);
        }
        if (condition.state == QLatin1String("notFound") && evaluation.satisfied)
            evaluation.diagnostics = {};
        return evaluation;
    }

    if (matches.isEmpty()) {
        evaluation.reason = QStringLiteral("target_not_found");
        return evaluation;
    }
    if (matches.size() > 1) {
        evaluation.reason = QStringLiteral("target_ambiguous");
        evaluation.diagnostics.append(waitDiagnostic(
                QStringLiteral("wait.target_ambiguous"),
                QStringLiteral("UI.waitFor property predicates require exactly one matched node.")));
        return evaluation;
    }

    evaluation.node = matches.first().toObject();
    evaluation.nodeId = evaluation.node.value(QStringLiteral("nodeId")).toInt(-1);
    evaluation.actual = nodePropertyValue(evaluation.node, condition.property);
    if (evaluation.actual.isUndefined()) {
        evaluation.reason = QStringLiteral("property_not_found");
        evaluation.diagnostics.append(waitDiagnostic(
                QStringLiteral("wait.property_not_found"),
                QStringLiteral("The matched node does not expose the requested property.")));
        return evaluation;
    }

    bool comparisonSupported = false;
    evaluation.satisfied = compareJsonValues(evaluation.actual, condition.op,
                                             condition.expected, &comparisonSupported);
    evaluation.reason = evaluation.satisfied
            ? QStringLiteral("predicate_satisfied")
            : QStringLiteral("property_not_satisfied");
    if (!comparisonSupported) {
        evaluation.reason = QStringLiteral("unsupported_comparison");
        evaluation.diagnostics.append(waitDiagnostic(
                QStringLiteral("wait.unsupported_comparison"),
                QStringLiteral("The requested comparison is not valid for the observed value types.")));
    }
    return evaluation;
}

static void waitForUiChange(const WaitEvaluation &evaluation, const QString &property,
                            int remainingMs, int *framesObserved)
{
    if (remainingMs <= 0)
        return;

    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);

    QList<QMetaObject::Connection> connections;
    const QWindowList windows = QGuiApplication::allWindows();
    for (QWindow *window : windows) {
        if (QQuickWindow *quickWindow = qobject_cast<QQuickWindow *>(window)) {
            connections.append(QObject::connect(quickWindow, &QQuickWindow::frameSwapped,
                                                &loop, [&]() {
                ++*framesObserved;
                loop.quit();
            }));
        }
    }

    if (!property.isEmpty() && evaluation.nodeId >= 0) {
        if (QObject *object = QQmlAgentUiTree::objectForNodeId(evaluation.nodeId)) {
            const QMetaObject *metaObject = object->metaObject();
            const int propertyIndex = metaObject->indexOfProperty(property.toUtf8().constData());
            if (propertyIndex >= 0) {
                const QMetaProperty metaProperty = metaObject->property(propertyIndex);
                if (metaProperty.hasNotifySignal()) {
                    const int quitIndex = loop.metaObject()->indexOfSlot("quit()");
                    if (quitIndex >= 0) {
                        connections.append(QObject::connect(
                                object, metaProperty.notifySignal(), &loop,
                                loop.metaObject()->method(quitIndex), Qt::SingleShotConnection));
                    }
                }
            }
        }
    }

    deadline.start(remainingMs);
    loop.exec();

    for (const QMetaObject::Connection &connection : std::as_const(connections))
        QObject::disconnect(connection);
}

}

QJsonObject QQmlAgentUiTree::waitFor(const QJsonObject &params)
{
    const WaitCondition condition = waitConditionFromParams(params);
    if (!condition.valid) {
        return {
            { QStringLiteral("ok"), false },
            { QStringLiteral("timedOut"), false },
            { QStringLiteral("reason"), QStringLiteral("invalid_until") },
            { QStringLiteral("diagnostics"), condition.diagnostics },
        };
    }

    const int requestedTimeoutMs = params.contains(QStringLiteral("timeoutMs"))
            ? params.value(QStringLiteral("timeoutMs")).toInt(DefaultWaitTimeoutMs)
            : params.value(QStringLiteral("until")).toObject()
                    .value(QStringLiteral("timeoutMs")).toInt(DefaultWaitTimeoutMs);
    const int timeoutMs = qBound(0, requestedTimeoutMs, MaxWaitTimeoutMs);

    QElapsedTimer elapsed;
    elapsed.start();
    int attempts = 0;
    int framesObserved = 0;
    WaitEvaluation evaluation;
    do {
        ++attempts;
        evaluation = evaluateWaitCondition(condition);
        if (evaluation.satisfied)
            break;

        const int remainingMs = timeoutMs - int(elapsed.elapsed());
        waitForUiChange(evaluation, condition.property, remainingMs, &framesObserved);
    } while (elapsed.elapsed() < timeoutMs);

    if (!evaluation.satisfied) {
        ++attempts;
        evaluation = evaluateWaitCondition(condition);
    }

    const bool timedOut = !evaluation.satisfied && elapsed.elapsed() >= timeoutMs;
    QJsonObject result{
        { QStringLiteral("ok"), evaluation.satisfied },
        { QStringLiteral("timedOut"), timedOut },
        { QStringLiteral("reason"), evaluation.reason },
        { QStringLiteral("selector"), condition.selector },
        { QStringLiteral("until"), params.value(QStringLiteral("until")).toObject() },
        { QStringLiteral("elapsedMs"), int(elapsed.elapsed()) },
        { QStringLiteral("timeoutMs"), timeoutMs },
        { QStringLiteral("attempts"), attempts },
        { QStringLiteral("framesObserved"), framesObserved },
        { QStringLiteral("matchCount"), evaluation.matchCount },
        { QStringLiteral("diagnostics"), evaluation.diagnostics },
    };
    if (!condition.property.isEmpty()) {
        result.insert(QStringLiteral("property"), condition.property);
        result.insert(QStringLiteral("actual"), evaluation.actual);
    }
    if (!evaluation.node.isEmpty())
        result.insert(QStringLiteral("node"), evaluation.node);
    if (!evaluation.satisfied) {
        result.insert(QStringLiteral("nextHints"), QJsonArray{
            QJsonObject{
                { QStringLiteral("method"), QStringLiteral("UI.query") },
                { QStringLiteral("params"), QJsonObject{
                    { QStringLiteral("selector"), condition.selector },
                    { QStringLiteral("includeSource"), true },
                    { QStringLiteral("properties"), condition.property.isEmpty()
                            ? QJsonArray()
                            : QJsonArray{ condition.property } },
                } },
                { QStringLiteral("reason"), QStringLiteral("Inspect the current matched node state.") },
            },
            QJsonObject{
                { QStringLiteral("method"), QStringLiteral("Diagnostics.analyzeNode") },
                { QStringLiteral("params"), QJsonObject{
                    { QStringLiteral("selector"), condition.selector },
                } },
                { QStringLiteral("reason"), QStringLiteral("Check whether the target is blocked, invisible, disabled, or offscreen.") },
            },
        });
    }
    return result;
}

QJsonObject QQmlAgentUiTree::describeNode(const QJsonObject &params)
{
    const int nodeId = params.value(QStringLiteral("nodeId")).toInt(-1);
    QObject *object = objectForNodeId(nodeId);
    if (!object)
        return { { QStringLiteral("node"), QJsonValue() } };

    int windowId = 0;
    if (QQuickItem *item = qobject_cast<QQuickItem *>(object)) {
        const QWindowList allWindows = QGuiApplication::allWindows();
        for (QWindow *window : allWindows) {
            if (qobject_cast<QQuickWindow *>(window))
                ++windowId;
            if (item->window() == window)
                break;
        }
    }

    return { { QStringLiteral("node"), nodeForObject(object, windowId, 0, true, true, {}) } };
}

QQmlAgentUiTree::NodeRef QQmlAgentUiTree::resolveNodeRef(const QJsonObject &params, bool includeSource,
                                                         const QJsonArray &properties)
{
    NodeRef ref;
    const bool hasNodeId = params.contains(QStringLiteral("nodeId"))
            && !params.value(QStringLiteral("nodeId")).isUndefined();
    const QString selectorText = params.value(QStringLiteral("selector")).toString();
    const bool hasSelector = !selectorText.isEmpty();

    if (hasNodeId == hasSelector) {
        ref.failureReason = QStringLiteral("invalid_noderef");
        ref.issues.append(nodeRefIssue(
                QStringLiteral("noderef.invalid"),
                QStringLiteral("Exactly one of nodeId or selector must be provided.")));
        return ref;
    }

    if (hasNodeId) {
        ref.nodeId = params.value(QStringLiteral("nodeId")).toInt(-1);
        ref.object = objectForNodeId(ref.nodeId);
        if (ref.object) {
            ref.node = describeNode({ { QStringLiteral("nodeId"), ref.nodeId } }).value(QStringLiteral("node")).toObject();
        } else {
            ref.failureReason = QStringLiteral("node_not_found");
            ref.issues.append(nodeRefIssue(
                    QStringLiteral("noderef.node_not_found"),
                    QStringLiteral("nodeId does not resolve to a live QML object.")));
        }
        return ref;
    }

    const QJsonObject queryResult = query({
        { QStringLiteral("selector"), selectorText },
        { QStringLiteral("includeInvisible"), true },
        { QStringLiteral("includeSource"), includeSource },
        { QStringLiteral("properties"), properties },
    });
    const QJsonArray matches = queryResult.value(QStringLiteral("matches")).toArray();
    if (matches.size() != 1) {
        ref.failureReason = matches.isEmpty()
                ? QStringLiteral("selector_not_found")
                : QStringLiteral("selector_ambiguous");
        ref.issues = queryResult.value(QStringLiteral("diagnostics")).toArray();
        return ref;
    }

    ref.node = matches.first().toObject();
    ref.nodeId = ref.node.value(QStringLiteral("nodeId")).toInt(-1);
    ref.object = objectForNodeId(ref.nodeId);
    if (!ref.object) {
        ref.failureReason = QStringLiteral("node_not_live");
        ref.issues.append(nodeRefIssue(
                QStringLiteral("noderef.node_not_live"),
                QStringLiteral("Selector matched a node snapshot, but the runtime object is no longer live.")));
    }
    return ref;
}

QJsonObject QQmlAgentUiTree::getBoxModel(const QJsonObject &params)
{
    const NodeRef ref = resolveNodeRef(params, false);
    QQuickItem *item = qobject_cast<QQuickItem *>(ref.object);
    if (!item || !item->window()) {
        QJsonObject result{
            { QStringLiteral("bbox"), QJsonValue() },
            { QStringLiteral("viewport"), QJsonValue() },
            { QStringLiteral("insideViewport"), false },
        };
        if (!ref.issues.isEmpty())
            result.insert(QStringLiteral("issues"), ref.issues);
        if (!ref.node.isEmpty())
            result.insert(QStringLiteral("node"), ref.node);
        return result;
    }

    return {
        { QStringLiteral("node"), ref.node },
        { QStringLiteral("bbox"), rectArray(itemBoxInWindow(item)) },
        { QStringLiteral("viewport"), rectArray(QRectF(QPointF(0, 0), item->window()->size())) },
        { QStringLiteral("insideViewport"), itemInsideViewport(item) },
        { QStringLiteral("viewportState"), itemViewportState(item) },
    };
}

QT_END_NAMESPACE
