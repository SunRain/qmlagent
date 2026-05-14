// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qqmlagentinput_p.h"

#include "qqmlagentactionability_p.h"
#include "qqmlagentdiagnostics_p.h"
#include "qqmlagentinputdriver_p.h"
#include "qqmlagentuitree_p.h"

#include <QtCore/qelapsedtimer.h>
#include <QtCore/qeventloop.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qtimer.h>
#include <QtCore/qvector.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qkeysequence.h>
#include <QtGui/qwindow.h>
#include <QtQuick/qquickitem.h>
#include <QtQuick/qquickwindow.h>
#include <QtQuickTemplates2/private/qquickcontrol_p.h>

#include <algorithm>

QT_BEGIN_NAMESPACE

static constexpr int DefaultSettleTimeoutMs = 50;

static QRectF itemBoxInWindow(const QQuickItem *item)
{
    if (!item)
        return {};
    return QRectF(item->mapToScene(QPointF(0, 0)), QSizeF(item->width(), item->height()));
}

static QString actionabilityFailureMessage(const QString &reason)
{
    if (reason == QLatin1String("not_visible"))
        return QStringLiteral("Node is not visible.");
    if (reason == QLatin1String("disabled"))
        return QStringLiteral("Node is disabled.");
    if (reason == QLatin1String("disabled_ancestor"))
        return QStringLiteral("Node has a disabled ancestor.");
    if (reason == QLatin1String("opacity_zero"))
        return QStringLiteral("Node has near-zero opacity.");
    if (reason == QLatin1String("opacity_zero_ancestor"))
        return QStringLiteral("Node has an ancestor with near-zero opacity.");
    if (reason == QLatin1String("zero_size"))
        return QStringLiteral("Node has zero or near-zero size.");
    if (reason == QLatin1String("unknown_window") || reason == QLatin1String("no_window"))
        return QStringLiteral("Node is not attached to a QQuickWindow.");
    if (reason == QLatin1String("center_outside_viewport"))
        return QStringLiteral("Node center is outside the viewport.");
    if (reason == QLatin1String("blocked_by_item"))
        return QStringLiteral("Requested input point is covered by another visible item.");
    if (reason == QLatin1String("blocked_by_modal_popup"))
        return QStringLiteral("Node is blocked by a visible modal popup.");
    return {};
}

static QJsonObject failure(const QString &reason, int nodeId, const QJsonArray &evidence)
{
    const QString message = [reason]() {
        if (reason == QLatin1String("node_not_found"))
            return QStringLiteral("Node does not exist in this session.");
        if (reason == QLatin1String("not_qquickitem"))
            return QStringLiteral("Node is not a QQuickItem.");
        const QString actionabilityMessage = actionabilityFailureMessage(reason);
        if (!actionabilityMessage.isEmpty())
            return actionabilityMessage;
        return QStringLiteral("Input cannot be delivered to this node.");
    }();

    return {
        { QStringLiteral("delivered"), false },
        { QStringLiteral("reason"), reason },
        { QStringLiteral("diagnostics"), QJsonArray{ QJsonObject{
            { QStringLiteral("id"), QStringLiteral("input.not_clickable") },
            { QStringLiteral("severity"), QStringLiteral("error") },
            { QStringLiteral("confidence"), 1.0 },
            { QStringLiteral("nodeId"), nodeId },
            { QStringLiteral("message"), message },
            { QStringLiteral("evidence"), evidence },
        } } },
    };
}

static QJsonObject failureWithDiagnostics(const QString &reason, const QJsonArray &diagnostics)
{
    return {
        { QStringLiteral("delivered"), false },
        { QStringLiteral("reason"), reason },
        { QStringLiteral("diagnostics"), diagnostics },
    };
}

static QJsonObject inputFailureFromActionability(
        const QJsonArray &reasons, int nodeId,
        QJsonObject (*failureFactory)(const QString &, int, const QJsonArray &))
{
    if (reasons.isEmpty())
        return {};

    const QJsonObject reason = reasons.at(0).toObject();
    return failureFactory(reason.value(QStringLiteral("id")).toString(QStringLiteral("not_actionable")),
                          nodeId, reason.value(QStringLiteral("evidence")).toArray());
}

static QJsonObject wheelFailure(const QString &reason, int nodeId, const QJsonArray &evidence)
{
    const QString message = [reason]() {
        if (reason == QLatin1String("invalid_delta"))
            return QStringLiteral("Wheel input requires a non-zero pixelDelta or angleDelta.");
        if (reason == QLatin1String("node_not_found"))
            return QStringLiteral("Node does not exist in this session.");
        if (reason == QLatin1String("not_qquickitem"))
            return QStringLiteral("Node is not a QQuickItem.");
        const QString actionabilityMessage = actionabilityFailureMessage(reason);
        if (!actionabilityMessage.isEmpty())
            return actionabilityMessage;
        return QStringLiteral("Wheel input cannot be delivered to this node.");
    }();

    return {
        { QStringLiteral("delivered"), false },
        { QStringLiteral("reason"), reason },
        { QStringLiteral("diagnostics"), QJsonArray{ QJsonObject{
            { QStringLiteral("id"), QStringLiteral("input.not_wheelable") },
            { QStringLiteral("severity"), QStringLiteral("error") },
            { QStringLiteral("confidence"), 1.0 },
            { QStringLiteral("nodeId"), nodeId },
            { QStringLiteral("message"), message },
            { QStringLiteral("evidence"), evidence },
        } } },
    };
}

static QJsonObject keyFailure(const QString &reason, const QJsonArray &evidence)
{
    return {
        { QStringLiteral("delivered"), false },
        { QStringLiteral("reason"), reason },
        { QStringLiteral("diagnostics"), QJsonArray{ QJsonObject{
            { QStringLiteral("id"), QStringLiteral("input.not_deliverable") },
            { QStringLiteral("severity"), QStringLiteral("error") },
            { QStringLiteral("confidence"), 1.0 },
            { QStringLiteral("evidence"), evidence },
        } } },
    };
}

static QJsonObject mouseFailure(const QString &reason, int nodeId, const QJsonArray &evidence)
{
    const QString message = [reason]() {
        if (reason == QLatin1String("invalid_type"))
            return QStringLiteral("Mouse input type must be mousePress, mouseMove, or mouseRelease.");
        if (reason == QLatin1String("invalid_button"))
            return QStringLiteral("Mouse input uses an unsupported button name.");
        if (reason == QLatin1String("node_not_found"))
            return QStringLiteral("Node does not exist in this session.");
        if (reason == QLatin1String("not_qquickitem"))
            return QStringLiteral("Node is not a QQuickItem.");
        const QString actionabilityMessage = actionabilityFailureMessage(reason);
        if (!actionabilityMessage.isEmpty())
            return actionabilityMessage;
        if (reason == QLatin1String("point_outside_viewport"))
            return QStringLiteral("Mouse event point is outside the viewport.");
        return QStringLiteral("Mouse input cannot be delivered to this node.");
    }();

    return {
        { QStringLiteral("delivered"), false },
        { QStringLiteral("reason"), reason },
        { QStringLiteral("diagnostics"), QJsonArray{ QJsonObject{
            { QStringLiteral("id"), QStringLiteral("input.not_mouse_dispatchable") },
            { QStringLiteral("severity"), QStringLiteral("error") },
            { QStringLiteral("confidence"), 1.0 },
            { QStringLiteral("nodeId"), nodeId },
            { QStringLiteral("message"), message },
            { QStringLiteral("evidence"), evidence },
        } } },
    };
}

static QJsonObject dragFailure(const QString &reason, int nodeId, const QJsonArray &evidence)
{
    const QString message = [reason]() {
        if (reason == QLatin1String("invalid_points"))
            return QStringLiteral("Drag input requires a target point or delta.");
        if (reason == QLatin1String("invalid_button"))
            return QStringLiteral("Drag input uses an unsupported button name.");
        if (reason == QLatin1String("node_not_found"))
            return QStringLiteral("Node does not exist in this session.");
        if (reason == QLatin1String("not_qquickitem"))
            return QStringLiteral("Node is not a QQuickItem.");
        const QString actionabilityMessage = actionabilityFailureMessage(reason);
        if (!actionabilityMessage.isEmpty())
            return actionabilityMessage;
        if (reason == QLatin1String("point_outside_viewport"))
            return QStringLiteral("One or more drag points are outside the viewport.");
        return QStringLiteral("Drag input cannot be delivered to this node.");
    }();

    return {
        { QStringLiteral("delivered"), false },
        { QStringLiteral("reason"), reason },
        { QStringLiteral("diagnostics"), QJsonArray{ QJsonObject{
            { QStringLiteral("id"), QStringLiteral("input.not_draggable") },
            { QStringLiteral("severity"), QStringLiteral("error") },
            { QStringLiteral("confidence"), 1.0 },
            { QStringLiteral("nodeId"), nodeId },
            { QStringLiteral("message"), message },
            { QStringLiteral("evidence"), evidence },
        } } },
    };
}

static QJsonObject touchFailure(const QString &reason, int nodeId, const QJsonArray &evidence)
{
    const QString message = [reason]() {
        if (reason == QLatin1String("invalid_type"))
            return QStringLiteral("Touch input type must be touchBegin, touchUpdate, touchEnd, or touchCancel.");
        if (reason == QLatin1String("invalid_points"))
            return QStringLiteral("Touch input requires one to sixteen valid touch points.");
        if (reason == QLatin1String("invalid_state"))
            return QStringLiteral("Touch point state must be pressed, updated, stationary, or released.");
        if (reason == QLatin1String("node_not_found"))
            return QStringLiteral("Node does not exist in this session.");
        if (reason == QLatin1String("not_qquickitem"))
            return QStringLiteral("Node is not a QQuickItem.");
        const QString actionabilityMessage = actionabilityFailureMessage(reason);
        if (!actionabilityMessage.isEmpty())
            return actionabilityMessage;
        if (reason == QLatin1String("point_outside_viewport"))
            return QStringLiteral("One or more touch points are outside the viewport.");
        return QStringLiteral("Touch input cannot be delivered to this node.");
    }();

    return {
        { QStringLiteral("delivered"), false },
        { QStringLiteral("reason"), reason },
        { QStringLiteral("diagnostics"), QJsonArray{ QJsonObject{
            { QStringLiteral("id"), QStringLiteral("input.not_touch_dispatchable") },
            { QStringLiteral("severity"), QStringLiteral("error") },
            { QStringLiteral("confidence"), 1.0 },
            { QStringLiteral("nodeId"), nodeId },
            { QStringLiteral("message"), message },
            { QStringLiteral("evidence"), evidence },
        } } },
    };
}

static QJsonObject focusFailure(const QString &reason, int nodeId, const QJsonArray &evidence)
{
    QString message = actionabilityFailureMessage(reason);
    if (message.isEmpty())
        message = QStringLiteral("Node cannot be focused for keyboard input.");

    return {
        { QStringLiteral("focused"), false },
        { QStringLiteral("reason"), reason },
        { QStringLiteral("diagnostics"), QJsonArray{ QJsonObject{
            { QStringLiteral("id"), QStringLiteral("input.not_focusable") },
            { QStringLiteral("severity"), QStringLiteral("error") },
            { QStringLiteral("confidence"), 1.0 },
            { QStringLiteral("nodeId"), nodeId },
            { QStringLiteral("message"), message },
            { QStringLiteral("evidence"), evidence },
        } } },
    };
}

static QJsonObject focusFailureWithDiagnostics(const QString &reason, const QJsonArray &diagnostics)
{
    return {
        { QStringLiteral("focused"), false },
        { QStringLiteral("reason"), reason },
        { QStringLiteral("diagnostics"), diagnostics },
    };
}

static bool hasNodeReference(const QJsonObject &params)
{
    return (params.contains(QStringLiteral("nodeId"))
            && !params.value(QStringLiteral("nodeId")).isUndefined())
            || !params.value(QStringLiteral("selector")).toString().isEmpty();
}

static bool boolProperty(const QObject *object, const char *name, bool *value)
{
    const int index = object ? object->metaObject()->indexOfProperty(name) : -1;
    if (index < 0)
        return false;

    *value = object->property(name).toBool();
    return true;
}

static bool hasProperty(const QObject *object, const char *name)
{
    return object && object->metaObject()->indexOfProperty(name) >= 0;
}

static bool isEditableTextObject(const QObject *object)
{
    if (!hasProperty(object, "text"))
        return false;
    if (hasProperty(object, "readOnly") || hasProperty(object, "cursorPosition"))
        return true;
    return false;
}

static QQuickItem *keyboardFocusItemFor(QQuickItem *item)
{
    QQuickControl *control = qobject_cast<QQuickControl *>(item);
    QQuickItem *contentItem = control ? control->contentItem() : nullptr;
    if (contentItem && contentItem != item && isEditableTextObject(contentItem))
        return contentItem;
    return item;
}

static bool itemContainsOrIs(QQuickItem *ancestor, QQuickItem *item)
{
    return ancestor && item && (ancestor == item || ancestor->isAncestorOf(item));
}

static QJsonObject notEditableFailure(int nodeId, const QJsonObject &node)
{
    QJsonObject result{
        { QStringLiteral("delivered"), false },
        { QStringLiteral("reason"), QStringLiteral("not_editable") },
        { QStringLiteral("diagnostics"), QJsonArray{ QJsonObject{
            { QStringLiteral("id"), QStringLiteral("input.not_editable") },
            { QStringLiteral("severity"), QStringLiteral("error") },
            { QStringLiteral("confidence"), 1.0 },
            { QStringLiteral("nodeId"), nodeId },
            { QStringLiteral("message"), QStringLiteral("Text input is read-only.") },
            { QStringLiteral("evidence"), QJsonArray{ QStringLiteral("readOnly=true") } },
        } } },
    };
    if (!node.isEmpty())
        result.insert(QStringLiteral("node"), node);
    return result;
}

static QQuickWindow *targetQuickWindow()
{
    if (QWindow *focusWindow = QGuiApplication::focusWindow()) {
        if (QQuickWindow *quickWindow = qobject_cast<QQuickWindow *>(focusWindow)) {
            if (quickWindow->contentItem() && quickWindow->width() > 0 && quickWindow->height() > 0)
                return quickWindow;
        }
    }

    QQuickWindow *found = nullptr;
    const QWindowList windows = QGuiApplication::allWindows();
    for (QWindow *window : windows) {
        QQuickWindow *quickWindow = qobject_cast<QQuickWindow *>(window);
        if (!quickWindow || !quickWindow->contentItem()
                || quickWindow->width() <= 0 || quickWindow->height() <= 0) {
            continue;
        }
        if (found)
            return nullptr;
        found = quickWindow;
    }

    return found;
}

template <typename Fn>
static QJsonObject runInputAndSettle(QQuickWindow *window, const QJsonObject &params, Fn &&fn)
{
    int framesAfterAction = 0;
    QElapsedTimer elapsed;
    elapsed.start();
    QEventLoop settleLoop;
    const QJsonObject settle = params.value(QStringLiteral("settle")).toObject();
    const int settleTimeoutMs = settle.value(QStringLiteral("timeoutMs")).toInt(DefaultSettleTimeoutMs);

    QObject::connect(window, &QQuickWindow::frameSwapped, &settleLoop, [&]() {
        ++framesAfterAction;
        settleLoop.quit();
    });

    fn(&elapsed);

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &settleLoop, &QEventLoop::quit);
    timeout.start(settleTimeoutMs);
    settleLoop.exec(QEventLoop::ExcludeUserInputEvents);

    return {
        { QStringLiteral("strategy"), QStringLiteral("frameSwappedOrTimeout") },
        { QStringLiteral("framesAfterAction"), framesAfterAction },
        { QStringLiteral("elapsedMs"), int(elapsed.elapsed()) },
        { QStringLiteral("timedOut"), framesAfterAction == 0 },
    };
}

static Qt::KeyboardModifiers modifiersFromParams(const QJsonObject &params)
{
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    const QJsonArray modifierArray = params.value(QStringLiteral("modifiers")).toArray();
    for (const QJsonValue &modifierValue : modifierArray) {
        const QString modifier = modifierValue.toString().toLower();
        if (modifier == QLatin1String("shift"))
            modifiers |= Qt::ShiftModifier;
        else if (modifier == QLatin1String("control") || modifier == QLatin1String("ctrl"))
            modifiers |= Qt::ControlModifier;
        else if (modifier == QLatin1String("alt"))
            modifiers |= Qt::AltModifier;
        else if (modifier == QLatin1String("meta"))
            modifiers |= Qt::MetaModifier;
    }
    return modifiers;
}

static bool keyFromParams(const QJsonObject &params, int *key,
                          Qt::KeyboardModifiers *modifiers, QString *text)
{
    *modifiers = modifiersFromParams(params);
    *text = params.value(QStringLiteral("text")).toString();

    const int keyCode = params.value(QStringLiteral("keyCode")).toInt(0);
    if (keyCode > 0) {
        *key = keyCode;
        return true;
    }

    const QString keyText = params.value(QStringLiteral("key")).toString();
    if (keyText.isEmpty())
        return false;

    const QKeySequence sequence = QKeySequence::fromString(keyText, QKeySequence::PortableText);
    if (sequence.isEmpty())
        return false;

    const QKeyCombination combination = sequence[0];
    *key = combination.key();
    *modifiers |= combination.keyboardModifiers();
    if (text->isEmpty() && keyText.size() == 1)
        *text = keyText;
    return *key > 0;
}

static bool appendNextTextUnit(const QString &text, qsizetype *index, QString *unit)
{
    if (*index >= text.size())
        return false;

    const QChar current = text.at(*index);
    if (current.isHighSurrogate() && *index + 1 < text.size()
            && text.at(*index + 1).isLowSurrogate()) {
        *unit = QString(current) + text.at(*index + 1);
        *index += 2;
        return true;
    }

    *unit = QString(current);
    ++*index;
    return true;
}

static int keyForTextUnit(const QString &unit)
{
    if (unit == QLatin1String("\n") || unit == QLatin1String("\r"))
        return Qt::Key_Return;
    if (unit == QLatin1String("\t"))
        return Qt::Key_Tab;
    if (unit == QLatin1String(" "))
        return Qt::Key_Space;

    const QKeySequence sequence = QKeySequence::fromString(unit, QKeySequence::PortableText);
    if (!sequence.isEmpty() && sequence[0].key() > 0)
        return sequence[0].key();

    return Qt::Key_unknown;
}

static QPoint pointFromParams(const QJsonObject &params, const QString &name)
{
    const QJsonValue value = params.value(name);
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.size() >= 2)
            return QPoint(array.at(0).toInt(), array.at(1).toInt());
        return {};
    }

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        return QPoint(object.value(QStringLiteral("x")).toInt(),
                      object.value(QStringLiteral("y")).toInt());
    }

    return {};
}

static bool hasPointParam(const QJsonObject &params, const QString &name)
{
    const QJsonValue value = params.value(name);
    if (value.isArray())
        return value.toArray().size() >= 2;
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        return object.contains(QStringLiteral("x")) && object.contains(QStringLiteral("y"));
    }
    return false;
}

static QPointF pointFFromParams(const QJsonObject &params, const QString &name)
{
    const QJsonValue value = params.value(name);
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.size() >= 2)
            return QPointF(array.at(0).toDouble(), array.at(1).toDouble());
        return {};
    }

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        return QPointF(object.value(QStringLiteral("x")).toDouble(),
                       object.value(QStringLiteral("y")).toDouble());
    }

    return {};
}

static bool pointFFromValue(const QJsonValue &value, QPointF *point)
{
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.size() < 2)
            return false;
        *point = QPointF(array.at(0).toDouble(), array.at(1).toDouble());
        return true;
    }

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (!object.contains(QStringLiteral("x")) || !object.contains(QStringLiteral("y")))
            return false;
        *point = QPointF(object.value(QStringLiteral("x")).toDouble(),
                         object.value(QStringLiteral("y")).toDouble());
        return true;
    }

    return false;
}

static Qt::MouseButton mouseButtonFromString(const QString &name, bool *ok)
{
    const QString normalized = name.toLower();
    *ok = true;
    if (normalized.isEmpty() || normalized == QLatin1String("left"))
        return Qt::LeftButton;
    if (normalized == QLatin1String("right"))
        return Qt::RightButton;
    if (normalized == QLatin1String("middle"))
        return Qt::MiddleButton;
    if (normalized == QLatin1String("back"))
        return Qt::BackButton;
    if (normalized == QLatin1String("forward"))
        return Qt::ForwardButton;
    if (normalized == QLatin1String("none"))
        return Qt::NoButton;
    *ok = false;
    return Qt::NoButton;
}

static Qt::MouseButtons mouseButtonsFromParams(
        const QJsonObject &params, Qt::MouseButtons defaultButtons, bool *ok)
{
    *ok = true;
    if (!params.contains(QStringLiteral("buttons")))
        return defaultButtons;

    Qt::MouseButtons buttons = Qt::NoButton;
    const QJsonArray buttonArray = params.value(QStringLiteral("buttons")).toArray();
    for (const QJsonValue &buttonValue : buttonArray) {
        bool buttonOk = false;
        const Qt::MouseButton button = mouseButtonFromString(buttonValue.toString(), &buttonOk);
        if (!buttonOk) {
            *ok = false;
            return Qt::NoButton;
        }
        buttons |= button;
    }
    return buttons;
}

static QJsonArray pointArray(const QPointF &point)
{
    return { point.x(), point.y() };
}

static bool touchStateFromString(const QString &stateName, QEventPoint::State *state)
{
    const QString normalized = stateName.toLower();
    if (normalized == QLatin1String("pressed")) {
        *state = QEventPoint::State::Pressed;
        return true;
    }
    if (normalized == QLatin1String("updated") || normalized == QLatin1String("moved")) {
        *state = QEventPoint::State::Updated;
        return true;
    }
    if (normalized == QLatin1String("stationary")) {
        *state = QEventPoint::State::Stationary;
        return true;
    }
    if (normalized == QLatin1String("released")) {
        *state = QEventPoint::State::Released;
        return true;
    }
    return false;
}

static QEventPoint::State defaultTouchPointState(QEvent::Type type)
{
    if (type == QEvent::TouchBegin)
        return QEventPoint::State::Pressed;
    if (type == QEvent::TouchEnd)
        return QEventPoint::State::Released;
    if (type == QEvent::TouchUpdate)
        return QEventPoint::State::Updated;
    return QEventPoint::State::Stationary;
}

QJsonObject QQmlAgentInput::clickNode(const QJsonObject &params)
{
    const QQmlAgentUiTree::NodeRef ref = QQmlAgentUiTree::resolveNodeRef(params);
    if (!ref.issues.isEmpty())
        return failureWithDiagnostics(ref.failureReason, ref.issues);

    const int nodeId = ref.nodeId;
    QObject *object = ref.object;
    if (!object)
        return failure(QStringLiteral("node_not_found"), nodeId, { QStringLiteral("node_not_found") });
    const QJsonArray actionabilityReasons = QQmlAgentActionability::reasons(object);
    if (!actionabilityReasons.isEmpty())
        return inputFailureFromActionability(actionabilityReasons, nodeId, failure);

    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    if (!item)
        return failure(QStringLiteral("not_qquickitem"), nodeId, { QStringLiteral("not_qquickitem") });

    QQuickWindow *window = item->window();
    if (!window)
        return failure(QStringLiteral("unknown_window"), nodeId, { QStringLiteral("window=null") });

    const QRectF bbox = itemBoxInWindow(item);
    const QPointF center = bbox.center();
    const QJsonObject settle = runInputAndSettle(window, params, [&](QElapsedTimer *elapsed) {
        QQmlAgentInputDriver::click(window, center, elapsed);
    });

    return {
        { QStringLiteral("delivered"), true },
        { QStringLiteral("node"), ref.node },
        { QStringLiteral("point"), QJsonArray{ center.x(), center.y() } },
        { QStringLiteral("mode"), QQmlAgentInputDriver::mode() },
        { QStringLiteral("settle"), settle },
    };
}

QJsonObject QQmlAgentInput::dragNode(const QJsonObject &params)
{
    const bool hasTo = hasPointParam(params, QStringLiteral("to"));
    const bool hasDelta = hasPointParam(params, QStringLiteral("delta"));
    if (hasTo == hasDelta) {
        return dragFailure(QStringLiteral("invalid_points"), -1,
                           { QStringLiteral("provide exactly one of to or delta") });
    }

    bool buttonOk = false;
    const Qt::MouseButton button = mouseButtonFromString(
            params.value(QStringLiteral("button")).toString(QStringLiteral("left")), &buttonOk);
    if (!buttonOk || button == Qt::NoButton)
        return dragFailure(QStringLiteral("invalid_button"), -1,
                           { QStringLiteral("button must be left, right, middle, back, or forward") });

    const QQmlAgentUiTree::NodeRef ref = QQmlAgentUiTree::resolveNodeRef(params);
    if (!ref.issues.isEmpty())
        return failureWithDiagnostics(ref.failureReason, ref.issues);

    const int nodeId = ref.nodeId;
    QObject *object = ref.object;
    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    if (!object)
        return dragFailure(QStringLiteral("node_not_found"), nodeId, { QStringLiteral("node_not_found") });
    if (!item)
        return dragFailure(QStringLiteral("not_qquickitem"), nodeId, { QStringLiteral("not_qquickitem") });

    QQuickWindow *window = item->window();
    if (!window)
        return dragFailure(QStringLiteral("unknown_window"), nodeId, { QStringLiteral("window=null") });

    const QPointF from = hasPointParam(params, QStringLiteral("from"))
            ? pointFFromParams(params, QStringLiteral("from"))
            : QPointF(item->width() / 2, item->height() / 2);
    const QPointF to = hasTo ? pointFFromParams(params, QStringLiteral("to"))
                             : from + pointFFromParams(params, QStringLiteral("delta"));
    const int steps = std::clamp(params.value(QStringLiteral("steps")).toInt(2), 2, 32);

    QJsonArray itemPoints;
    QJsonArray windowPoints;
    QVector<QPointF> path;
    path.reserve(steps + 1);
    const QRectF viewport(QPointF(0, 0), window->size());
    for (int i = 0; i <= steps; ++i) {
        const qreal progress = qreal(i) / qreal(steps);
        const QPointF itemPoint = from + (to - from) * progress;
        const QPointF windowPoint = item->mapToScene(itemPoint);
        if (!viewport.contains(windowPoint)) {
            return dragFailure(QStringLiteral("point_outside_viewport"), nodeId,
                               { QStringLiteral("point=[%1,%2]").arg(windowPoint.x()).arg(windowPoint.y()),
                                 QStringLiteral("viewport=[0,0,%1,%2]").arg(viewport.width()).arg(viewport.height()) });
        }
        const QJsonArray actionabilityReasons =
                QQmlAgentActionability::reasonsAtPoint(object, windowPoint);
        if (!actionabilityReasons.isEmpty())
            return inputFailureFromActionability(actionabilityReasons, nodeId, dragFailure);
        itemPoints.append(pointArray(itemPoint));
        windowPoints.append(pointArray(windowPoint));
        path.append(windowPoint);
    }

    const Qt::KeyboardModifiers modifiers = modifiersFromParams(params);
    const QJsonObject settle = runInputAndSettle(window, params, [&](QElapsedTimer *elapsed) {
        QQmlAgentInputDriver::mouse(window, path.first(), QEvent::MouseButtonPress, button, button,
                                    modifiers, elapsed);
        for (int i = 1; i < path.size(); ++i) {
            QQmlAgentInputDriver::mouse(window, path.at(i), QEvent::MouseMove, Qt::NoButton,
                                        button, modifiers, elapsed);
        }
        QQmlAgentInputDriver::mouse(window, path.last(), QEvent::MouseButtonRelease, button,
                                    Qt::NoButton, modifiers, elapsed);
    });

    return {
        { QStringLiteral("delivered"), true },
        { QStringLiteral("node"), ref.node },
        { QStringLiteral("button"), params.value(QStringLiteral("button")).toString(QStringLiteral("left")) },
        { QStringLiteral("from"), pointArray(from) },
        { QStringLiteral("to"), pointArray(to) },
        { QStringLiteral("steps"), steps },
        { QStringLiteral("itemPoints"), itemPoints },
        { QStringLiteral("windowPoints"), windowPoints },
        { QStringLiteral("eventsSent"), steps + 2 },
        { QStringLiteral("mode"), QQmlAgentInputDriver::mode() },
        { QStringLiteral("settle"), settle },
    };
}

QJsonObject QQmlAgentInput::dispatchMouseEvent(const QJsonObject &params)
{
    const QString typeName = params.value(QStringLiteral("type")).toString();
    QEvent::Type eventType = QEvent::None;
    Qt::MouseButtons defaultButtons = Qt::NoButton;
    if (typeName == QLatin1String("mousePress")) {
        eventType = QEvent::MouseButtonPress;
        defaultButtons = Qt::LeftButton;
    } else if (typeName == QLatin1String("mouseMove")) {
        eventType = QEvent::MouseMove;
        defaultButtons = Qt::LeftButton;
    } else if (typeName == QLatin1String("mouseRelease")) {
        eventType = QEvent::MouseButtonRelease;
    } else {
        return mouseFailure(QStringLiteral("invalid_type"), -1,
                            { QStringLiteral("type must be mousePress, mouseMove, or mouseRelease") });
    }

    bool buttonOk = false;
    Qt::MouseButton button = mouseButtonFromString(
            params.value(QStringLiteral("button")).toString(QStringLiteral("left")), &buttonOk);
    if (!buttonOk)
        return mouseFailure(QStringLiteral("invalid_button"), -1,
                            { QStringLiteral("button must be left, right, middle, back, forward, or none") });
    if (eventType == QEvent::MouseMove)
        button = Qt::NoButton;

    bool buttonsOk = false;
    const Qt::MouseButtons buttons = mouseButtonsFromParams(params, defaultButtons, &buttonsOk);
    if (!buttonsOk)
        return mouseFailure(QStringLiteral("invalid_button"), -1,
                            { QStringLiteral("buttons must contain left, right, middle, back, forward, or none") });

    const QQmlAgentUiTree::NodeRef ref = QQmlAgentUiTree::resolveNodeRef(params);
    if (!ref.issues.isEmpty())
        return failureWithDiagnostics(ref.failureReason, ref.issues);

    const int nodeId = ref.nodeId;
    QObject *object = ref.object;
    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    if (!object)
        return mouseFailure(QStringLiteral("node_not_found"), nodeId, { QStringLiteral("node_not_found") });
    if (!item)
        return mouseFailure(QStringLiteral("not_qquickitem"), nodeId, { QStringLiteral("not_qquickitem") });

    QQuickWindow *window = item->window();
    if (!window)
        return mouseFailure(QStringLiteral("unknown_window"), nodeId, { QStringLiteral("window=null") });

    const QPointF itemPoint = hasPointParam(params, QStringLiteral("point"))
            ? pointFFromParams(params, QStringLiteral("point"))
            : QPointF(item->width() / 2, item->height() / 2);
    const QPointF windowPoint = item->mapToScene(itemPoint);
    const QRectF viewport(QPointF(0, 0), window->size());
    if (!viewport.contains(windowPoint)) {
        return mouseFailure(QStringLiteral("point_outside_viewport"), nodeId,
                            { QStringLiteral("point=[%1,%2]").arg(windowPoint.x()).arg(windowPoint.y()),
                              QStringLiteral("viewport=[0,0,%1,%2]").arg(viewport.width()).arg(viewport.height()) });
    }
    const QJsonArray actionabilityReasons =
            QQmlAgentActionability::reasonsAtPoint(object, windowPoint);
    if (!actionabilityReasons.isEmpty())
        return inputFailureFromActionability(actionabilityReasons, nodeId, mouseFailure);

    const Qt::KeyboardModifiers modifiers = modifiersFromParams(params);
    const QJsonObject settle = runInputAndSettle(window, params, [&](QElapsedTimer *elapsed) {
        QQmlAgentInputDriver::mouse(window, windowPoint, eventType, button, buttons, modifiers,
                                    elapsed);
    });

    return {
        { QStringLiteral("delivered"), true },
        { QStringLiteral("node"), ref.node },
        { QStringLiteral("type"), typeName },
        { QStringLiteral("point"), QJsonArray{ windowPoint.x(), windowPoint.y() } },
        { QStringLiteral("itemPoint"), QJsonArray{ itemPoint.x(), itemPoint.y() } },
        { QStringLiteral("mode"), QQmlAgentInputDriver::mode() },
        { QStringLiteral("settle"), settle },
    };
}

QJsonObject QQmlAgentInput::dispatchTouchEvent(const QJsonObject &params)
{
    const QString typeName = params.value(QStringLiteral("type")).toString();
    QEvent::Type eventType = QEvent::None;
    if (typeName == QLatin1String("touchBegin"))
        eventType = QEvent::TouchBegin;
    else if (typeName == QLatin1String("touchUpdate"))
        eventType = QEvent::TouchUpdate;
    else if (typeName == QLatin1String("touchEnd"))
        eventType = QEvent::TouchEnd;
    else if (typeName == QLatin1String("touchCancel"))
        eventType = QEvent::TouchCancel;
    else
        return touchFailure(QStringLiteral("invalid_type"), -1,
                            { QStringLiteral("type must be touchBegin, touchUpdate, touchEnd, or touchCancel") });

    const QJsonArray pointParams = params.value(QStringLiteral("points")).toArray();
    if (eventType != QEvent::TouchCancel && (pointParams.isEmpty() || pointParams.size() > 16))
        return touchFailure(QStringLiteral("invalid_points"), -1,
                            { QStringLiteral("points must contain 1..16 entries") });

    const QQmlAgentUiTree::NodeRef ref = QQmlAgentUiTree::resolveNodeRef(params);
    if (!ref.issues.isEmpty())
        return failureWithDiagnostics(ref.failureReason, ref.issues);

    const int nodeId = ref.nodeId;
    QObject *object = ref.object;
    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    if (!object)
        return touchFailure(QStringLiteral("node_not_found"), nodeId, { QStringLiteral("node_not_found") });
    if (!item)
        return touchFailure(QStringLiteral("not_qquickitem"), nodeId, { QStringLiteral("not_qquickitem") });

    QQuickWindow *window = item->window();
    if (!window)
        return touchFailure(QStringLiteral("unknown_window"), nodeId, { QStringLiteral("window=null") });

    const QRectF viewport(QPointF(0, 0), window->size());
    QList<QQmlAgentInputDriver::TouchPoint> eventPoints;
    QJsonArray points;
    for (const QJsonValue &pointValue : pointParams) {
        if (!pointValue.isObject())
            return touchFailure(QStringLiteral("invalid_points"), nodeId,
                                { QStringLiteral("each point must be an object") });

        const QJsonObject pointObject = pointValue.toObject();
        const int pointId = pointObject.value(QStringLiteral("id")).toInt(-1);
        if (pointId < 0)
            return touchFailure(QStringLiteral("invalid_points"), nodeId,
                                { QStringLiteral("each point requires id >= 0") });

        QPointF itemPoint;
        if (!pointFFromValue(pointObject.value(QStringLiteral("point")), &itemPoint)) {
            return touchFailure(QStringLiteral("invalid_points"), nodeId,
                                { QStringLiteral("each point requires point:[x,y]") });
        }

        QEventPoint::State state = defaultTouchPointState(eventType);
        const QString stateName = pointObject.value(QStringLiteral("state")).toString();
        if (!stateName.isEmpty() && !touchStateFromString(stateName, &state))
            return touchFailure(QStringLiteral("invalid_state"), nodeId,
                                { QStringLiteral("state must be pressed, updated, stationary, or released") });

        const QPointF windowPoint = item->mapToScene(itemPoint);
        if (!viewport.contains(windowPoint)) {
            return touchFailure(QStringLiteral("point_outside_viewport"), nodeId,
                                { QStringLiteral("point=[%1,%2]").arg(windowPoint.x()).arg(windowPoint.y()),
                                  QStringLiteral("viewport=[0,0,%1,%2]").arg(viewport.width()).arg(viewport.height()) });
        }
        const QJsonArray actionabilityReasons =
                QQmlAgentActionability::reasonsAtPoint(object, windowPoint);
        if (!actionabilityReasons.isEmpty())
            return inputFailureFromActionability(actionabilityReasons, nodeId, touchFailure);

        const QPointF globalPoint = window->mapToGlobal(windowPoint.toPoint());
        eventPoints.append(QQmlAgentInputDriver::TouchPoint{
            pointId,
            state,
            windowPoint,
            globalPoint,
        });
        points.append(QJsonObject{
            { QStringLiteral("id"), pointId },
            { QStringLiteral("state"), stateName.isEmpty()
                    ? (state == QEventPoint::State::Pressed ? QStringLiteral("pressed")
                       : state == QEventPoint::State::Updated ? QStringLiteral("updated")
                       : state == QEventPoint::State::Released ? QStringLiteral("released")
                       : QStringLiteral("stationary"))
                    : stateName },
            { QStringLiteral("itemPoint"), pointArray(itemPoint) },
            { QStringLiteral("windowPoint"), pointArray(windowPoint) },
        });
    }

    const Qt::KeyboardModifiers modifiers = modifiersFromParams(params);
    const QJsonObject settle = runInputAndSettle(window, params, [&](QElapsedTimer *) {
        QQmlAgentInputDriver::touch(window, eventType, eventPoints, modifiers);
    });

    return {
        { QStringLiteral("delivered"), true },
        { QStringLiteral("node"), ref.node },
        { QStringLiteral("type"), typeName },
        { QStringLiteral("points"), points },
        { QStringLiteral("mode"), QQmlAgentInputDriver::mode() },
        { QStringLiteral("settle"), settle },
    };
}

QJsonObject QQmlAgentInput::wheel(const QJsonObject &params)
{
    const QPoint pixelDelta = pointFromParams(params, QStringLiteral("pixelDelta"));
    QPoint angleDelta = pointFromParams(params, QStringLiteral("angleDelta"));
    if (params.contains(QStringLiteral("deltaX")) || params.contains(QStringLiteral("deltaY"))) {
        angleDelta = QPoint(params.value(QStringLiteral("deltaX")).toInt(0),
                            params.value(QStringLiteral("deltaY")).toInt(0));
    }
    if (pixelDelta.isNull() && angleDelta.isNull())
        return wheelFailure(QStringLiteral("invalid_delta"), -1,
                            { QStringLiteral("provide pixelDelta, angleDelta, deltaX, or deltaY") });

    const QQmlAgentUiTree::NodeRef ref = QQmlAgentUiTree::resolveNodeRef(params);
    if (!ref.issues.isEmpty())
        return failureWithDiagnostics(ref.failureReason, ref.issues);

    const int nodeId = ref.nodeId;
    QObject *object = ref.object;
    if (!object)
        return wheelFailure(QStringLiteral("node_not_found"), nodeId, { QStringLiteral("node_not_found") });
    const QJsonArray actionabilityReasons = QQmlAgentActionability::reasons(object);
    if (!actionabilityReasons.isEmpty())
        return inputFailureFromActionability(actionabilityReasons, nodeId, wheelFailure);

    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    if (!item)
        return wheelFailure(QStringLiteral("not_qquickitem"), nodeId, { QStringLiteral("not_qquickitem") });

    QQuickWindow *window = item->window();
    if (!window)
        return wheelFailure(QStringLiteral("unknown_window"), nodeId, { QStringLiteral("window=null") });

    const QRectF bbox = itemBoxInWindow(item);
    const QPointF center = bbox.center();
    const QRectF viewport(QPointF(0, 0), window->size());
    if (!viewport.contains(center)) {
        return wheelFailure(QStringLiteral("center_outside_viewport"), nodeId,
                            { QStringLiteral("bbox=[%1,%2,%3,%4]").arg(bbox.x()).arg(bbox.y()).arg(bbox.width()).arg(bbox.height()),
                              QStringLiteral("center=[%1,%2]").arg(center.x()).arg(center.y()),
                              QStringLiteral("viewport=[0,0,%1,%2]").arg(viewport.width()).arg(viewport.height()) });
    }

    const Qt::KeyboardModifiers modifiers = modifiersFromParams(params);
    const QJsonObject settle = runInputAndSettle(window, params, [&](QElapsedTimer *) {
        QQmlAgentInputDriver::wheel(window, center, pixelDelta, angleDelta, modifiers);
    });

    return {
        { QStringLiteral("delivered"), true },
        { QStringLiteral("node"), ref.node },
        { QStringLiteral("point"), QJsonArray{ center.x(), center.y() } },
        { QStringLiteral("pixelDelta"), QJsonArray{ pixelDelta.x(), pixelDelta.y() } },
        { QStringLiteral("angleDelta"), QJsonArray{ angleDelta.x(), angleDelta.y() } },
        { QStringLiteral("mode"), QQmlAgentInputDriver::mode() },
        { QStringLiteral("settle"), settle },
    };
}

QJsonObject QQmlAgentInput::typeText(const QJsonObject &params)
{
    const QString text = params.value(QStringLiteral("text")).toString();
    const bool replaceExisting = params.value(QStringLiteral("replaceExisting")).toBool(false);
    if (text.isEmpty() && !replaceExisting)
        return keyFailure(QStringLiteral("invalid_text"),
                          { QStringLiteral("text must be a non-empty string") });

    const bool hasNodeId = params.contains(QStringLiteral("nodeId"))
            && !params.value(QStringLiteral("nodeId")).isUndefined();
    const bool hasSelector = !params.value(QStringLiteral("selector")).toString().isEmpty();
    QJsonObject focusResult;
    QQuickWindow *inputWindow = nullptr;
    if (hasNodeId || hasSelector) {
        const QQmlAgentUiTree::NodeRef ref = QQmlAgentUiTree::resolveNodeRef(params);
        if (!ref.issues.isEmpty())
            return failureWithDiagnostics(ref.failureReason, ref.issues);

        QJsonObject focusParams{
            { QStringLiteral("settle"), params.value(QStringLiteral("focusSettle")).toObject() },
        };
        if (hasNodeId)
            focusParams.insert(QStringLiteral("nodeId"), ref.nodeId);
        else
            focusParams.insert(QStringLiteral("selector"),
                               params.value(QStringLiteral("selector")).toString());

        auto focusFailureHints = [&](const QQmlAgentUiTree::NodeRef &targetRef) {
            QJsonArray hints{
                QJsonObject{
                    { QStringLiteral("method"), QStringLiteral("Input.focusNode") },
                    { QStringLiteral("params"), QJsonObject{
                        { QStringLiteral("nodeId"), targetRef.nodeId },
                    } },
                    { QStringLiteral("reason"), QStringLiteral("Focus this text target explicitly, then retry Input.typeText without relying on click-to-focus.") },
                },
            };
            if (hasSelector) {
                hints.append(QJsonObject{
                    { QStringLiteral("method"), QStringLiteral("Input.focusNode") },
                    { QStringLiteral("params"), QJsonObject{
                        { QStringLiteral("selector"),
                          params.value(QStringLiteral("selector")).toString() },
                    } },
                    { QStringLiteral("reason"), QStringLiteral("Selector-first focus retry for agents that avoid session-local node ids across relaunches.") },
                });
            }
            return hints;
        };

        focusResult = clickNode(focusParams);
        if (!focusResult.value(QStringLiteral("delivered")).toBool()) {
            QJsonObject result = keyFailure(QStringLiteral("focus_failed"),
                                            { QStringLiteral("Input.clickNode did not focus target") });
            result.insert(QStringLiteral("focus"), focusResult);
            result.insert(QStringLiteral("nextHints"), focusFailureHints(ref));
            return result;
        }

        bool readOnly = false;
        if (boolProperty(ref.object, "readOnly", &readOnly) && readOnly)
            return notEditableFailure(ref.nodeId, ref.node);

        QQuickItem *targetItem = qobject_cast<QQuickItem *>(ref.object);
        inputWindow = targetItem ? targetItem->window() : nullptr;
        QQuickItem *activeFocusItem = inputWindow ? inputWindow->activeFocusItem() : nullptr;
        if (!itemContainsOrIs(targetItem, activeFocusItem)) {
            QJsonObject result = keyFailure(QStringLiteral("focus_failed"),
                                            { QStringLiteral("target did not receive active focus after click"),
                                              QStringLiteral("activeFocusItem=%1")
                                                      .arg(activeFocusItem
                                                           ? QString::fromUtf8(activeFocusItem->metaObject()->className())
                                                           : QStringLiteral("<none>")) });
            result.insert(QStringLiteral("focus"), focusResult);
            result.insert(QStringLiteral("nextHints"), focusFailureHints(ref));
            return result;
        }
    }

    QQuickWindow *window = inputWindow ? inputWindow : targetQuickWindow();
    if (!window) {
        return keyFailure(QStringLiteral("unknown_window"),
                          { QStringLiteral("no focused or unique nonzero QQuickWindow with contentItem") });
    }

    int unitsSent = 0;
    QJsonObject replacement;
    const QJsonObject settle = runInputAndSettle(window, params, [&](QElapsedTimer *) {
        if (replaceExisting) {
            QQuickItem *activeFocusItem = window->activeFocusItem();
            const bool selected = activeFocusItem
                    && QMetaObject::invokeMethod(activeFocusItem, "selectAll",
                                                 Qt::DirectConnection);
            replacement = {
                { QStringLiteral("requested"), true },
                { QStringLiteral("mode"), QStringLiteral("selectAll-then-type") },
                { QStringLiteral("selectionMethod"), QStringLiteral("Q_INVOKABLE selectAll") },
                { QStringLiteral("selectionApplied"), selected },
            };
            if (!selected)
                return;
            if (text.isEmpty()) {
                QQmlAgentInputDriver::key(window, QEvent::KeyPress, Qt::Key_Backspace,
                                          Qt::NoModifier, {});
                QQmlAgentInputDriver::key(window, QEvent::KeyRelease, Qt::Key_Backspace,
                                          Qt::NoModifier, {});
                return;
            }
        }
        QString unit;
        qsizetype index = 0;
        while (appendNextTextUnit(text, &index, &unit)) {
            const int key = keyForTextUnit(unit);
            QQmlAgentInputDriver::key(window, QEvent::KeyPress, key, Qt::NoModifier, unit);
            QQmlAgentInputDriver::key(window, QEvent::KeyRelease, key, Qt::NoModifier, unit);
            ++unitsSent;
        }
    });

    QJsonObject result{
        { QStringLiteral("delivered"), !replaceExisting
                  || replacement.value(QStringLiteral("selectionApplied")).toBool(false) },
        { QStringLiteral("text"), text },
        { QStringLiteral("unitsSent"), unitsSent },
        { QStringLiteral("mode"), QQmlAgentInputDriver::mode() },
        { QStringLiteral("settle"), settle },
    };
    if (replaceExisting) {
        result.insert(QStringLiteral("replaceExisting"), replacement);
        if (!replacement.value(QStringLiteral("selectionApplied")).toBool(false)) {
            result.insert(QStringLiteral("reason"), QStringLiteral("replace_selection_failed"));
            result.insert(QStringLiteral("diagnostics"), QJsonArray{ QJsonObject{
                { QStringLiteral("id"), QStringLiteral("input.replace_selection_failed") },
                { QStringLiteral("severity"), QStringLiteral("error") },
                { QStringLiteral("confidence"), 1.0 },
                { QStringLiteral("message"),
                  QStringLiteral("Focused text item does not expose selectAll().") },
            } });
        }
    }
    if (!focusResult.isEmpty())
        result.insert(QStringLiteral("focus"), focusResult);
    return result;
}

QJsonObject QQmlAgentInput::focusNode(const QJsonObject &params)
{
    const QQmlAgentUiTree::NodeRef ref = QQmlAgentUiTree::resolveNodeRef(params);
    if (!ref.issues.isEmpty())
        return focusFailureWithDiagnostics(ref.failureReason, ref.issues);

    const int nodeId = ref.nodeId;
    QObject *object = ref.object;
    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    if (!object)
        return focusFailure(QStringLiteral("node_not_found"), nodeId, { QStringLiteral("node_not_found") });
    const QJsonArray actionabilityReasons = QQmlAgentActionability::reasons(object);
    if (!actionabilityReasons.isEmpty())
        return inputFailureFromActionability(actionabilityReasons, nodeId, focusFailure);

    if (!item)
        return focusFailure(QStringLiteral("not_qquickitem"), nodeId, { QStringLiteral("not_qquickitem") });

    QQuickItem *focusItem = keyboardFocusItemFor(item);
    focusItem->forceActiveFocus(Qt::OtherFocusReason);
    if (!focusItem->hasActiveFocus())
        return focusFailure(QStringLiteral("focus_rejected"), nodeId,
                            { QStringLiteral("hasActiveFocus=false") });

    QJsonObject result{
        { QStringLiteral("focused"), focusItem->hasActiveFocus() },
        { QStringLiteral("node"), ref.node },
        { QStringLiteral("mode"), QStringLiteral("qquickitem-force-active-focus") },
        { QStringLiteral("activeFocus"), focusItem->hasActiveFocus() },
    };
    if (focusItem != item) {
        result.insert(QStringLiteral("focusTarget"), QJsonObject{
            { QStringLiteral("type"), QString::fromUtf8(focusItem->metaObject()->className()) },
            { QStringLiteral("objectName"), focusItem->objectName() },
            { QStringLiteral("reason"), QStringLiteral("editable-control-content-item") },
        });
    }
    return result;
}

QJsonObject QQmlAgentInput::dispatchKeyEvent(const QJsonObject &params)
{
    int key = 0;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    QString text;
    if (!keyFromParams(params, &key, &modifiers, &text)) {
        return keyFailure(QStringLiteral("invalid_key"),
                          { QStringLiteral("provide keyCode or a QKeySequence-compatible key") });
    }

    const QString type = params.value(QStringLiteral("type")).toString(QStringLiteral("keyClick"));
    if (type != QLatin1String("keyClick") && type != QLatin1String("keyPress")
            && type != QLatin1String("keyRelease")) {
        return keyFailure(QStringLiteral("invalid_type"),
                          { QStringLiteral("type must be keyClick, keyPress, or keyRelease") });
    }

    QJsonObject focusResult;
    QQuickWindow *window = nullptr;
    if (hasNodeReference(params)) {
        const QQmlAgentUiTree::NodeRef ref = QQmlAgentUiTree::resolveNodeRef(params);
        if (!ref.issues.isEmpty())
            return failureWithDiagnostics(ref.failureReason, ref.issues);

        focusResult = focusNode(params);
        if (!focusResult.value(QStringLiteral("focused")).toBool()) {
            QJsonObject result = keyFailure(QStringLiteral("focus_failed"),
                                            { QStringLiteral("Input.focusNode did not focus target") });
            result.insert(QStringLiteral("focus"), focusResult);
            return result;
        }

        QQuickItem *item = qobject_cast<QQuickItem *>(ref.object);
        if (item)
            window = item->window();
    }

    if (!window)
        window = targetQuickWindow();
    if (!window) {
        return keyFailure(QStringLiteral("unknown_window"),
                          { QStringLiteral("no focused or unique nonzero QQuickWindow with contentItem") });
    }

    const QJsonObject settle = runInputAndSettle(window, params, [&](QElapsedTimer *) {
        if (type == QLatin1String("keyClick") || type == QLatin1String("keyPress"))
            QQmlAgentInputDriver::key(window, QEvent::KeyPress, key, modifiers, text);
        if (type == QLatin1String("keyClick") || type == QLatin1String("keyRelease"))
            QQmlAgentInputDriver::key(window, QEvent::KeyRelease, key, modifiers, text);
    });

    QJsonObject result{
        { QStringLiteral("delivered"), true },
        { QStringLiteral("keyCode"), key },
        { QStringLiteral("text"), text },
        { QStringLiteral("type"), type },
        { QStringLiteral("mode"), QQmlAgentInputDriver::mode() },
        { QStringLiteral("settle"), settle },
    };
    if (!focusResult.isEmpty())
        result.insert(QStringLiteral("focus"), focusResult);
    return result;
}

QT_END_NAMESPACE
