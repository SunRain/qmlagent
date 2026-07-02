// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include <QtCore/qurl.h>
#include <QtGui/qcolor.h>
#include <QtGui/qguiapplication.h>
#include <QtQuick/qquickitem.h>
#include <QtQml/qqmlapplicationengine.h>
#include <QtQml/qqmlcomponent.h>
#include <QtQml/qqml.h>

class ValueTypeItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QColor agentColor READ agentColor CONSTANT)
    Q_PROPERTY(QColor agentTransparentColor READ agentTransparentColor CONSTANT)
    Q_PROPERTY(QPointF agentPoint READ agentPoint CONSTANT)
    Q_PROPERTY(QSizeF agentSize READ agentSize CONSTANT)
    Q_PROPERTY(QRectF agentRect READ agentRect CONSTANT)

public:
    using QQuickItem::QQuickItem;

    QColor agentColor() const { return QColor(QStringLiteral("#336699")); }
    QColor agentTransparentColor() const { return QColor::fromRgb(0x33, 0x66, 0x99, 0x80); }
    QPointF agentPoint() const { return QPointF(2, 3); }
    QSizeF agentSize() const { return QSizeF(10, 11); }
    QRectF agentRect() const { return QRectF(1, 2, 3, 4); }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    qmlRegisterType<ValueTypeItem>("QmlAgentSmoke.Testing", 1, 0, "ValueTypeItem");

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("QmlAgentSmoke", "Smoke");

    return app.exec();
}

#include "main.moc"
