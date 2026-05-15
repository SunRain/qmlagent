// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qqmlagentinputdriver_p.h"

#include <QtCore/qelapsedtimer.h>
#include <QtGui/qevent.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qpointingdevice.h>
#include <QtGui/qscreen.h>
#include <QtGui/qwindow.h>
#include <qpa/qwindowsysteminterface.h>
#include <QtQuick/qquickwindow.h>

QT_BEGIN_NAMESPACE

QString QQmlAgentInputDriver::mode()
{
    return QStringLiteral("synthetic-qt-event");
}

static ulong nextInputTimestamp()
{
    static QElapsedTimer timer;
    if (!timer.isValid())
        timer.start();
    return ulong(timer.elapsed());
}

void QQmlAgentInputDriver::click(
        QQuickWindow *window, const QPointF &windowPoint, QElapsedTimer *elapsed)
{
    Q_ASSERT(window);
    Q_ASSERT(elapsed);

    mouse(window, windowPoint, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton,
          Qt::NoModifier, elapsed);
    mouse(window, windowPoint, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
          Qt::NoModifier, elapsed);
}

void QQmlAgentInputDriver::mouse(QQuickWindow *window, const QPointF &windowPoint,
                                 QEvent::Type type, Qt::MouseButton button,
                                 Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers,
                                 QElapsedTimer *elapsed)
{
    Q_ASSERT(window);
    Q_ASSERT(elapsed);

    const QPointF global = window->mapToGlobal(windowPoint.toPoint());
    QMouseEvent event(type, windowPoint, windowPoint, global, button, buttons, modifiers,
                      Qt::MouseEventSynthesizedByApplication);
    event.setAccepted(false);
    event.setTimestamp(nextInputTimestamp());
    QGuiApplication::sendEvent(window, &event);
}

void QQmlAgentInputDriver::wheel(QQuickWindow *window, const QPointF &windowPoint,
                                 const QPoint &pixelDelta, const QPoint &angleDelta,
                                 Qt::KeyboardModifiers modifiers)
{
    Q_ASSERT(window);
    const QPoint global = window->mapToGlobal(windowPoint.toPoint());
    QWheelEvent event(windowPoint, global, pixelDelta, angleDelta, Qt::NoButton, modifiers,
                      Qt::ScrollUpdate, false);
    event.setAccepted(false);
    event.setTimestamp(nextInputTimestamp());
    QGuiApplication::sendEvent(window, &event);
}

void QQmlAgentInputDriver::key(
        QQuickWindow *window, QEvent::Type type, int keyCode, Qt::KeyboardModifiers modifiers,
        const QString &text)
{
    Q_ASSERT(window);
    QQmlAgentInputDriver::key(static_cast<QObject *>(window), type, keyCode, modifiers, text);
}

void QQmlAgentInputDriver::key(
        QObject *target, QEvent::Type type, int keyCode, Qt::KeyboardModifiers modifiers,
        const QString &text)
{
    Q_ASSERT(target);
    QKeyEvent event(type, keyCode, modifiers, text, false, 1);
    QGuiApplication::sendEvent(target, &event);
}

void QQmlAgentInputDriver::inputText(QObject *target, const QString &text)
{
    Q_ASSERT(target);
    QInputMethodEvent event;
    event.setCommitString(text);
    QGuiApplication::sendEvent(target, &event);
}

static const QPointingDevice *qmlAgentTouchDevice()
{
    static const QPointingDevice *device = []() {
        auto *touchscreen = new QPointingDevice(
                QStringLiteral("QmlAgent synthetic touchscreen"), 0,
                QInputDevice::DeviceType::TouchScreen, QPointingDevice::PointerType::Finger,
                QInputDevice::Capability::Position, 16, 0, QString(), QPointingDeviceUniqueId(),
                qApp);
        QWindowSystemInterface::registerInputDevice(touchscreen);
        return touchscreen;
    }();
    return device;
}

void QQmlAgentInputDriver::touch(
        QQuickWindow *window, QEvent::Type type, const QList<TouchPoint> &points,
        Qt::KeyboardModifiers modifiers)
{
    Q_ASSERT(window);
    const QPointingDevice *device = qmlAgentTouchDevice();
    const ulong timestamp = nextInputTimestamp();

    if (type == QEvent::TouchCancel) {
        QWindowSystemInterface::handleTouchCancelEvent<
                QWindowSystemInterface::SynchronousDelivery>(window, timestamp, device, modifiers);
        return;
    }

    const QRect screenGeometry = window->screen() ? window->screen()->geometry()
                                                  : QRect(QPoint(0, 0), window->size());
    const QSizeF screenSize = screenGeometry.size();
    QList<QWindowSystemInterface::TouchPoint> touchPoints;
    touchPoints.reserve(points.size());
    for (const TouchPoint &point : points) {
        QWindowSystemInterface::TouchPoint touchPoint;
        touchPoint.id = point.id;
        touchPoint.state = point.state;
        touchPoint.pressure = (point.state == QEventPoint::State::Pressed
                               || point.state == QEventPoint::State::Updated)
                ? 1.0
                : 0.0;
        touchPoint.area = QRectF(point.globalPoint - QPointF(1, 1), QSizeF(2, 2));
        if (!screenSize.isEmpty()) {
            touchPoint.normalPosition = QPointF(
                    (point.globalPoint.x() - screenGeometry.x()) / screenSize.width(),
                    (point.globalPoint.y() - screenGeometry.y()) / screenSize.height());
        }
        touchPoint.rawPositions.append(point.globalPoint);
        touchPoints.append(touchPoint);
    }

    QWindowSystemInterface::handleTouchEvent<QWindowSystemInterface::SynchronousDelivery>(
            window, timestamp, device, touchPoints, modifiers);
}

QT_END_NAMESPACE
