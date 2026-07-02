// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qqmlagentactionability_p.h"

#include <QtCore/qjsonarray.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qobject.h>
#include <QtCore/qset.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qvector.h>
#include <QtQuick/qquickitem.h>
#include <QtQuick/qquickwindow.h>
#include <QtQuick/private/qquickmousearea_p.h>
#include <QtQuick/private/qquickmultipointtoucharea_p.h>
#include <QtQuick/private/qquickpointerhandler_p.h>
#include <QtQuickTemplates2/private/qquickcontrol_p.h>
#include <QtQuickTemplates2/private/qquickoverlay_p.h>
#include <QtQuickTemplates2/private/qquickpopup_p.h>

#include <algorithm>

QT_BEGIN_NAMESPACE

namespace {

static QRectF itemBoxInWindow(const QQuickItem *item)
{
    if (!item)
        return {};
    return item->mapRectToScene(QRectF(QPointF(0, 0), QSizeF(item->width(), item->height())));
}

static QJsonArray pointArray(const QPointF &point)
{
    return { point.x(), point.y() };
}

static QJsonArray rectArray(const QRectF &rect)
{
    return { rect.x(), rect.y(), rect.width(), rect.height() };
}

static QJsonObject viewportStateForPoint(const QQuickItem *item, const QPointF &actionPoint)
{
    if (!item || !item->window()) {
        return {
            { QStringLiteral("available"), false },
            { QStringLiteral("actionPointInside"), false },
            { QStringLiteral("fullyInside"), false },
            { QStringLiteral("partiallyInside"), false },
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
        { QStringLiteral("actionPoint"), pointArray(actionPoint) },
        { QStringLiteral("actionPointInside"), viewport.contains(actionPoint) },
        { QStringLiteral("fullyInside"), viewport.contains(bbox) },
        { QStringLiteral("partiallyInside"), !intersection.isEmpty() },
        { QStringLiteral("centerInside"), viewport.contains(bbox.center()) },
        { QStringLiteral("clippedByViewport"), !intersection.isEmpty() && !viewport.contains(bbox) },
    };
}

static QString objectLabel(const QObject *object)
{
    if (!object)
        return {};
    if (!object->objectName().isEmpty())
        return object->objectName();
    return QString::fromUtf8(object->metaObject()->className());
}

static QString itemDescription(const QQuickItem *item)
{
    if (!item)
        return {};

    QStringList parts;
    parts.append(QString::fromUtf8(item->metaObject()->className()));
    if (!item->objectName().isEmpty())
        parts.append(QStringLiteral("objectName=%1").arg(item->objectName()));
    return parts.join(QLatin1Char(' '));
}

static bool itemContainsScenePoint(QQuickItem *item, const QPointF &scenePoint)
{
    if (!item || !item->isVisible())
        return false;
    return item->contains(item->mapFromScene(scenePoint));
}

static QList<QQuickItem *> paintOrderChildren(QQuickItem *item)
{
    QList<QQuickItem *> children = item ? item->childItems() : QList<QQuickItem *>{};
    std::stable_sort(children.begin(), children.end(), [](QQuickItem *left, QQuickItem *right) {
        return left->z() < right->z();
    });
    return children;
}

static bool objectEnabled(const QObject *object)
{
    const int index = object ? object->metaObject()->indexOfProperty("enabled") : -1;
    return index < 0 || object->property("enabled").toBool();
}

static bool hasAcceptedButtons(const QObject *object)
{
    const int index = object ? object->metaObject()->indexOfProperty("acceptedButtons") : -1;
    if (index < 0)
        return false;

    bool ok = false;
    const int buttons = object->property("acceptedButtons").toInt(&ok);
    return ok && buttons != int(Qt::NoButton);
}

static bool isKnownInputItem(const QQuickItem *item)
{
    return qobject_cast<const QQuickMouseArea *>(item)
            || qobject_cast<const QQuickMultiPointTouchArea *>(item)
            || qobject_cast<const QQuickControl *>(item);
}

static bool hasPointerHandlerChild(const QObject *object)
{
    if (!object)
        return false;

    for (QObject *child : object->children()) {
        if (qobject_cast<QQuickPointerHandler *>(child) && objectEnabled(child)
            && hasAcceptedButtons(child)) {
            return true;
        }
    }
    return false;
}

static bool isPlausibleInputBlocker(QQuickItem *item)
{
    return item && objectEnabled(item) && item->isVisible() && item->opacity() > 0.01
            && (hasAcceptedButtons(item) || isKnownInputItem(item) || hasPointerHandlerChild(item));
}

static QQuickItem *topmostInputBlockerAt(QQuickItem *root, const QPointF &scenePoint,
                                         QQuickItem *target)
{
    if (!root || !target)
        return nullptr;

    const QList<QQuickItem *> children = paintOrderChildren(root);
    for (auto it = children.crbegin(), end = children.crend(); it != end; ++it) {
        QQuickItem *child = *it;
        if (!itemContainsScenePoint(child, scenePoint))
            continue;

        if (child == target || target->isAncestorOf(child))
            return nullptr;

        if (child->isAncestorOf(target))
            return topmostInputBlockerAt(child, scenePoint, target);

        if (QQuickItem *descendantBlocker = topmostInputBlockerAt(child, scenePoint, target))
            return descendantBlocker;

        if (isPlausibleInputBlocker(child)) {
            // A mouse-accepting surface that fully encloses the target AND is
            // attached to a container that owns the target is the control's
            // own click area, not an occluder: Text under an anchors.fill
            // MouseArea is the canonical custom-control pattern, and a real
            // user click at this point lands on that surface by design.
            // (F-018: agents click what they can see — the label — and the
            // label's interaction surface must not refuse.) Enclosure alone
            // is not proof of same-control ownership: a foreign overlay that
            // happens to cover the target still reads as occlusion.
            const QRectF childBox = itemBoxInWindow(child);
            const QRectF targetBox = itemBoxInWindow(target);
            if ((hasAcceptedButtons(child) || hasPointerHandlerChild(child))
                    && childBox.contains(targetBox)
                    && child->parentItem()
                    && child->parentItem()->isAncestorOf(target)) {
                return nullptr;
            }
            return child;
        }
    }

    return nullptr;
}

static QList<QQuickOverlay *> overlaysForWindow(QQuickWindow *window)
{
    QList<QQuickOverlay *> overlays;
    QQuickItem *contentItem = window ? window->contentItem() : nullptr;
    if (!contentItem)
        return overlays;

    QVector<QQuickItem *> stack{ contentItem };
    QSet<QQuickItem *> seen;
    while (!stack.isEmpty()) {
        QQuickItem *item = stack.takeLast();
        if (!item || seen.contains(item))
            continue;
        seen.insert(item);

        if (QQuickOverlay *overlay = qobject_cast<QQuickOverlay *>(item))
            overlays.append(overlay);
        const QList<QQuickItem *> children = item->childItems();
        for (QQuickItem *child : children)
            stack.append(child);
    }

    return overlays;
}

static QQuickPopup *modalPopupContainingItem(QQuickItem *item)
{
    QQuickWindow *window = item ? item->window() : nullptr;
    if (!window)
        return nullptr;

    for (QQuickOverlay *overlay : overlaysForWindow(window)) {
        const QList<QQuickItem *> children = overlay->childItems();
        for (auto it = children.crbegin(), end = children.crend(); it != end; ++it) {
            QQuickPopup *popup = qobject_cast<QQuickPopup *>((*it)->parent());
            if (!popup || !popup->isVisible() || !popup->isModal())
                continue;
            QQuickItem *popupItem = popup->popupItem();
            if (popupItem && (popupItem == item || popupItem->isAncestorOf(item)))
                return popup;
        }
    }

    return nullptr;
}

static QQuickPopup *modalPopupBlockingItem(QQuickItem *item)
{
    QQuickWindow *window = item ? item->window() : nullptr;
    if (!window)
        return nullptr;
    if (modalPopupContainingItem(item))
        return nullptr;

    for (QQuickOverlay *overlay : overlaysForWindow(window)) {
        const QList<QQuickItem *> children = overlay->childItems();
        for (auto it = children.crbegin(), end = children.crend(); it != end; ++it) {
            QQuickPopup *popup = qobject_cast<QQuickPopup *>((*it)->parent());
            if (!popup || !popup->isVisible() || !popup->isModal())
                continue;
            QQuickItem *popupItem = popup->popupItem();
            if (popupItem && (popupItem == item || popupItem->isAncestorOf(item)))
                continue;
            return popup;
        }
    }

    return nullptr;
}

static bool boolProperty(const QObject *object, const char *name, bool *value)
{
    const int index = object ? object->metaObject()->indexOfProperty(name) : -1;
    if (index < 0)
        return false;

    *value = object->property(name).toBool();
    return true;
}

static void appendReason(QJsonArray *reasons, const QString &id, const QJsonArray &evidence)
{
    reasons->append(QJsonObject{
        { QStringLiteral("id"), id },
        { QStringLiteral("evidence"), evidence },
    });
}

} // namespace

static QJsonArray reasonsForObject(QObject *object, const QPointF *scenePoint)
{
    QJsonArray reasons;
    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    if (!item) {
        appendReason(&reasons, QStringLiteral("not_qquickitem"),
                     { QStringLiteral("target is not a QQuickItem") });
        return reasons;
    }

    if (!item->isVisible())
        appendReason(&reasons, QStringLiteral("not_visible"),
                     { QStringLiteral("visible=false") });
    if (item->opacity() <= 0.01)
        appendReason(&reasons, QStringLiteral("opacity_zero"),
                     { QStringLiteral("opacity=%1").arg(item->opacity()) });
    if (item->width() <= 0.5 || item->height() <= 0.5) {
        appendReason(&reasons, QStringLiteral("zero_size"),
                     { QStringLiteral("width=%1").arg(item->width()),
                       QStringLiteral("height=%1").arg(item->height()) });
    }
    if (item->window()) {
        const QRectF bbox = itemBoxInWindow(item);
        const QPointF actionPoint = scenePoint ? *scenePoint : bbox.center();
        const QRectF viewport(QPointF(0, 0), item->window()->size());
        if (!viewport.contains(actionPoint)) {
            appendReason(&reasons,
                         scenePoint ? QStringLiteral("point_outside_viewport")
                                    : QStringLiteral("center_outside_viewport"),
                         { QStringLiteral("%1=[%2,%3]")
                                   .arg(scenePoint ? QStringLiteral("point")
                                                   : QStringLiteral("center"))
                                   .arg(actionPoint.x())
                                   .arg(actionPoint.y()),
                           QStringLiteral("viewport=[0,0,%1,%2]")
                                   .arg(viewport.width())
                                   .arg(viewport.height()) });
        } else if (QQuickPopup *popup = modalPopupBlockingItem(item)) {
            appendReason(&reasons, QStringLiteral("blocked_by_modal_popup"),
                         { QStringLiteral("%1=[%2,%3]")
                                   .arg(scenePoint ? QStringLiteral("point")
                                                   : QStringLiteral("center"))
                                   .arg(actionPoint.x())
                                   .arg(actionPoint.y()),
                           QStringLiteral("blockingPopup=%1").arg(objectLabel(popup)),
                           QStringLiteral("blockingPopupItem=%1").arg(itemDescription(popup->popupItem())),
                           QStringLiteral("target=%1").arg(itemDescription(item)),
                           QStringLiteral("method=quicktemplates-overlay-stack") });
        } else if (!modalPopupContainingItem(item)) {
            if (QQuickItem *blockingItem =
                        topmostInputBlockerAt(item->window()->contentItem(), actionPoint, item)) {
                appendReason(&reasons, QStringLiteral("blocked_by_item"),
                             { QStringLiteral("%1=[%2,%3]")
                                       .arg(scenePoint ? QStringLiteral("point")
                                                       : QStringLiteral("center"))
                                       .arg(actionPoint.x())
                                       .arg(actionPoint.y()),
                               QStringLiteral("blockingItem=%1").arg(itemDescription(blockingItem)),
                               QStringLiteral("target=%1").arg(itemDescription(item)),
                               QStringLiteral("method=scene-paint-order-center-hit-test") });
            }
        }
    }

    bool enabled = true;
    if (boolProperty(object, "enabled", &enabled) && !enabled) {
        appendReason(&reasons, QStringLiteral("disabled"),
                     { QStringLiteral("enabled=false") });
    } else {
        for (QQuickItem *ancestor = item->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
            bool ancestorEnabled = true;
            if (boolProperty(ancestor, "enabled", &ancestorEnabled) && !ancestorEnabled) {
                appendReason(&reasons, QStringLiteral("disabled_ancestor"),
                             { QStringLiteral("ancestorEnabled=false"),
                               QStringLiteral("ancestor=%1").arg(objectLabel(ancestor)) });
                break;
            }
        }
    }

    if (!item->window())
        appendReason(&reasons, QStringLiteral("no_window"),
                     { QStringLiteral("target has no QQuickWindow") });

    for (QQuickItem *ancestor = item->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
        if (ancestor->opacity() <= 0.01) {
            appendReason(&reasons, QStringLiteral("opacity_zero_ancestor"),
                         { QStringLiteral("ancestorOpacity=%1").arg(ancestor->opacity()),
                           QStringLiteral("ancestor=%1").arg(objectLabel(ancestor)) });
            break;
        }
    }

    return reasons;
}

QJsonArray QQmlAgentActionability::reasons(QObject *object)
{
    return reasonsForObject(object, nullptr);
}

QJsonArray QQmlAgentActionability::reasonsAtPoint(QObject *object, const QPointF &scenePoint)
{
    return reasonsForObject(object, &scenePoint);
}

QJsonObject QQmlAgentActionability::state(QObject *object)
{
    const QJsonArray reasonArray = reasons(object);
    QJsonObject result{
        { QStringLiteral("ok"), reasonArray.isEmpty() },
        { QStringLiteral("reasons"), reasonArray },
        { QStringLiteral("confidence"), reasonArray.isEmpty() ? 0.85 : 1.0 },
        { QStringLiteral("limitations"), QJsonArray{
            QStringLiteral("does not prove the target has an input handler"),
            QStringLiteral("generic blocker detection uses a center-point paint-order approximation"),
            QStringLiteral("Qt Quick Controls modal popup blocking is refined through the overlay stack"),
        } },
    };
    if (QQuickItem *item = qobject_cast<QQuickItem *>(object)) {
        const QRectF bbox = itemBoxInWindow(item);
        result.insert(QStringLiteral("viewport"), viewportStateForPoint(item, bbox.center()));
    }
    return result;
}

QT_END_NAMESPACE
