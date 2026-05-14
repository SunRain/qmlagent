// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qqmlagentrender_p.h"

#include <QtCore/qbuffer.h>
#include <QtCore/qjsonarray.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qimage.h>
#include <QtGui/qwindow.h>
#include <QtQuick/qquickwindow.h>

QT_BEGIN_NAMESPACE

static QQuickWindow *quickWindowForId(int requestedWindowId, int *actualWindowId)
{
    int windowId = 0;
    QQuickWindow *firstWindow = nullptr;
    const QWindowList windows = QGuiApplication::allWindows();
    for (QWindow *window : windows) {
        QQuickWindow *quickWindow = qobject_cast<QQuickWindow *>(window);
        if (!quickWindow || !quickWindow->contentItem())
            continue;

        ++windowId;
        if (!firstWindow)
            firstWindow = quickWindow;
        if (requestedWindowId > 0 && requestedWindowId == windowId) {
            *actualWindowId = windowId;
            return quickWindow;
        }
    }

    if (requestedWindowId <= 0 && firstWindow) {
        *actualWindowId = 1;
        return firstWindow;
    }

    *actualWindowId = requestedWindowId;
    return nullptr;
}

static QJsonObject failure(const QString &reason, int windowId)
{
    return {
        { QStringLiteral("captured"), false },
        { QStringLiteral("reason"), reason },
        { QStringLiteral("windowId"), windowId },
    };
}

QJsonObject QQmlAgentRender::captureScreenshot(const QJsonObject &params)
{
    int windowId = -1;
    QQuickWindow *window = quickWindowForId(params.value(QStringLiteral("windowId")).toInt(-1),
                                           &windowId);
    if (!window)
        return failure(QStringLiteral("window_not_found"), windowId);

    QImage image = window->grabWindow();
    if (image.isNull())
        return failure(QStringLiteral("grab_failed"), windowId);

    const int originalWidth = image.width();
    const int originalHeight = image.height();
    const double devicePixelRatio = image.devicePixelRatio();

    if (params.contains(QStringLiteral("region"))) {
        if (!params.value(QStringLiteral("region")).isObject())
            return failure(QStringLiteral("invalid_region"), windowId);

        const QJsonObject region = params.value(QStringLiteral("region")).toObject();
        const double x = region.value(QStringLiteral("x")).toDouble();
        const double y = region.value(QStringLiteral("y")).toDouble();
        const double width = region.value(QStringLiteral("width")).toDouble();
        const double height = region.value(QStringLiteral("height")).toDouble();
        if (width <= 0 || height <= 0)
            return failure(QStringLiteral("invalid_region"), windowId);

        const QRect requestedRect(qRound(x * devicePixelRatio),
                                  qRound(y * devicePixelRatio),
                                  qRound(width * devicePixelRatio),
                                  qRound(height * devicePixelRatio));
        const QRect clippedRect = requestedRect.intersected(image.rect());
        if (clippedRect.isEmpty())
            return failure(QStringLiteral("region_outside_window"), windowId);

        image = image.copy(clippedRect);
        image.setDevicePixelRatio(devicePixelRatio);
    }

    double scale = 1.0;
    if (params.contains(QStringLiteral("scale"))) {
        scale = params.value(QStringLiteral("scale")).toDouble(1.0);
        if (scale <= 0.0 || scale > 1.0)
            return failure(QStringLiteral("invalid_scale"), windowId);
        if (scale < 1.0) {
            const QSize scaledSize(qMax(1, qRound(image.width() * scale)),
                                   qMax(1, qRound(image.height() * scale)));
            image = image.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            image.setDevicePixelRatio(devicePixelRatio * scale);
        }
    }

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return failure(QStringLiteral("encode_failed"), windowId);

    QJsonObject result{
        { QStringLiteral("captured"), true },
        { QStringLiteral("windowId"), windowId },
        { QStringLiteral("format"), QStringLiteral("png") },
        { QStringLiteral("encoding"), QStringLiteral("base64") },
        { QStringLiteral("width"), image.width() },
        { QStringLiteral("height"), image.height() },
        { QStringLiteral("devicePixelRatio"), image.devicePixelRatio() },
        { QStringLiteral("originalWidth"), originalWidth },
        { QStringLiteral("originalHeight"), originalHeight },
        { QStringLiteral("scale"), scale },
        { QStringLiteral("byteSize"), png.size() },
        { QStringLiteral("evidenceRole"), QStringLiteral("fallback-visual") },
        { QStringLiteral("primaryOracle"), false },
        { QStringLiteral("structuredFirst"), true },
    };
    if (params.contains(QStringLiteral("region")))
        result.insert(QStringLiteral("region"), params.value(QStringLiteral("region")).toObject());
    if (params.value(QStringLiteral("omitData")).toBool(false)) {
        result.insert(QStringLiteral("dataOmitted"), true);
        result.insert(QStringLiteral("nextHints"), QJsonArray{
            QJsonObject{
                { QStringLiteral("method"), QStringLiteral("Render.captureScreenshot") },
                { QStringLiteral("params"), QJsonObject{
                    { QStringLiteral("windowId"), windowId },
                    { QStringLiteral("omitData"), false },
                } },
                { QStringLiteral("reason"), QStringLiteral("Request PNG data only when visual fallback evidence is needed.") },
            },
        });
    } else {
        result.insert(QStringLiteral("data"), QString::fromLatin1(png.toBase64()));
    }
    return result;
}

QT_END_NAMESPACE
