// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef QQMLAGENTINPUTDRIVER_P_H
#define QQMLAGENTINPUTDRIVER_P_H

#include <QtCore/qnamespace.h>
#include <QtCore/qpoint.h>
#include <QtCore/qlist.h>
#include <QtCore/qstring.h>
#include <QtCore/qcoreevent.h>
#include <QtGui/qeventpoint.h>

QT_BEGIN_NAMESPACE

class QElapsedTimer;
class QObject;
class QQuickWindow;

class QQmlAgentInputDriver
{
public:
    struct TouchPoint
    {
        int id = -1;
        QEventPoint::State state = QEventPoint::State::Unknown;
        QPointF windowPoint;
        QPointF globalPoint;
    };

    static QString mode();
    static void click(QQuickWindow *window, const QPointF &windowPoint, QElapsedTimer *elapsed);
    static void mouse(QQuickWindow *window, const QPointF &windowPoint, QEvent::Type type,
                      Qt::MouseButton button, Qt::MouseButtons buttons,
                      Qt::KeyboardModifiers modifiers, QElapsedTimer *elapsed);
    static void wheel(QQuickWindow *window, const QPointF &windowPoint, const QPoint &pixelDelta,
                      const QPoint &angleDelta, Qt::KeyboardModifiers modifiers);
    static void key(QQuickWindow *window, QEvent::Type type, int key,
                    Qt::KeyboardModifiers modifiers, const QString &text);
    static void key(QObject *target, QEvent::Type type, int key,
                    Qt::KeyboardModifiers modifiers, const QString &text);
    static void inputText(QObject *target, const QString &text);
    static void touch(QQuickWindow *window, QEvent::Type type, const QList<TouchPoint> &points,
                      Qt::KeyboardModifiers modifiers);
};

QT_END_NAMESPACE

#endif // QQMLAGENTINPUTDRIVER_P_H
