// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include <QtCore/qcoreapplication.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qurl.h>
#include <QtGui/qguiapplication.h>
#include <QtQml/qqmlapplicationengine.h>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    QString qmlFile;
    const QStringList arguments = QCoreApplication::arguments();
    for (const QString &argument : arguments) {
        if (argument.endsWith(QLatin1String(".qml"))) {
            qmlFile = argument;
            break;
        }
    }

    if (qmlFile.isEmpty())
        return 2;

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl::fromLocalFile(qmlFile));

    return app.exec();
}
