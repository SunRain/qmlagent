// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qqmlagentservice_p.h"

#include "qqmlagentdiagnostics_p.h"
#include "qqmlagentinput_p.h"
#include "qqmlagentprotocol_p.h"
#include "qqmlagentrender_p.h"
#include "qqmlagentruntime_p.h"
#include "qqmlagentsourceresolver_p.h"
#include "qqmlagentuitree_p.h"

#include <QtCore/qcoreapplication.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qset.h>
#include <QtCore/qthread.h>
#include <QtCore/quuid.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qwindow.h>
#include <QtQml/qjsengine.h>
#include <QtQml/qqmlengine.h>
#include <QtQuick/qquickitem.h>
#include <QtQuick/qquickwindow.h>

QT_BEGIN_NAMESPACE

template <typename Fn>
static auto runOnGuiThreadBlocking(Fn &&fn) -> decltype(fn())
{
    using Result = decltype(fn());
    QObject *application = QCoreApplication::instance();
    if (!application || QThread::currentThread() == application->thread())
        return fn();

    Result result;
    QMetaObject::invokeMethod(application, [&]() {
        result = fn();
    }, Qt::BlockingQueuedConnection);
    return result;
}

QQmlAgentService::QQmlAgentService(QObject *parent)
    : QQmlDebugService(serviceKey(), 1, parent),
      m_sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      m_logs(this)
{
    connect(&m_logs, &QQmlAgentLogCollector::entryAdded,
            this, &QQmlAgentService::sendEntryEvent, Qt::QueuedConnection);
}

QQmlAgentService::~QQmlAgentService()
{
    clearUiWatchers();
}

QString QQmlAgentService::serviceKey()
{
    return QStringLiteral("QmlAgent");
}

void QQmlAgentService::engineAdded(QJSEngine *engine)
{
    m_logs.attachEngine(qobject_cast<QQmlEngine *>(engine));
}

void QQmlAgentService::messageReceived(const QByteArray &message)
{
    const QQmlAgentProtocol::Request request = QQmlAgentProtocol::parseRequest(message);
    if (!request.valid) {
        sendProtocolMessage(QQmlAgentProtocol::error(request.id, request.errorCode,
                                                    request.errorMessage, request.errorData));
        return;
    }

    static const QSet<QString> knownMethods{
        QStringLiteral("Session.getInfo"),
        QStringLiteral("Session.configure"),
        QStringLiteral("Session.reset"),
        QStringLiteral("Log.enable"),
        QStringLiteral("Log.getEntries"),
        QStringLiteral("Log.clear"),
        QStringLiteral("UI.getTree"),
        QStringLiteral("UI.query"),
        QStringLiteral("UI.waitFor"),
        QStringLiteral("UI.describeNode"),
        QStringLiteral("UI.getBoxModel"),
        QStringLiteral("UI.subscribe"),
        QStringLiteral("UI.unsubscribe"),
        QStringLiteral("Diagnostics.analyzeNode"),
        QStringLiteral("Diagnostics.analyzeTree"),
        QStringLiteral("Diagnostics.analyzeBinding"),
        QStringLiteral("Input.clickNode"),
        QStringLiteral("Input.wheel"),
        QStringLiteral("Input.focusNode"),
        QStringLiteral("Input.dispatchMouseEvent"),
        QStringLiteral("Input.dragNode"),
        QStringLiteral("Input.dispatchTouchEvent"),
        QStringLiteral("Input.dispatchKeyEvent"),
        QStringLiteral("Input.typeText"),
        QStringLiteral("Render.captureScreenshot"),
        QStringLiteral("Runtime.setProperty"),
        QStringLiteral("Runtime.invokeMethod"),
        QStringLiteral("Source.resolveNode"),
    };
    if (!knownMethods.contains(request.method)) {
        sendProtocolMessage(QQmlAgentProtocol::error(request.id, -32601,
                                                    QStringLiteral("Method not found")));
        return;
    }

    const QJsonObject result = dispatch(request.method, request.params);
    if (!request.id.isUndefined())
        sendProtocolMessage(QQmlAgentProtocol::response(request.id, result));
}

void QQmlAgentService::sendProtocolMessage(const QByteArray &message)
{
    emit messageToClient(name(), message);
}

void QQmlAgentService::sendEntryEvent(const QJsonObject &entry)
{
    sendProtocolMessage(QQmlAgentProtocol::event(QStringLiteral("Log.entryAdded"),
                                                 { { QStringLiteral("entry"), entry } }));
}

QJsonObject QQmlAgentService::dispatch(const QString &method, const QJsonObject &params)
{
    if (method == QLatin1String("Session.getInfo"))
        return sessionInfo();
    if (method == QLatin1String("Session.configure"))
        return sessionConfigure(params);

    if (method == QLatin1String("Session.reset")) {
        m_logs.reset();
        m_runtimeMutationEnabled = false;
        return { { QStringLiteral("reset"), true } };
    }

    if (method == QLatin1String("Log.enable"))
        return m_logs.enable(params);
    if (method == QLatin1String("Log.getEntries"))
        return m_logs.entries(params);
    if (method == QLatin1String("Log.clear"))
        return m_logs.clear();

    if (method == QLatin1String("UI.getTree"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentUiTree::getTree(params); });
    if (method == QLatin1String("UI.query"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentUiTree::query(params); });
    if (method == QLatin1String("UI.waitFor"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentUiTree::waitFor(params); });
    if (method == QLatin1String("UI.describeNode"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentUiTree::describeNode(params); });
    if (method == QLatin1String("UI.getBoxModel"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentUiTree::getBoxModel(params); });
    if (method == QLatin1String("UI.subscribe"))
        return runOnGuiThreadBlocking([&]() { return subscribeUi(params); });
    if (method == QLatin1String("UI.unsubscribe"))
        return runOnGuiThreadBlocking([&]() { return unsubscribeUi(); });
    if (method == QLatin1String("Diagnostics.analyzeNode"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentDiagnostics::analyzeNode(params); });
    if (method == QLatin1String("Diagnostics.analyzeBinding"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentDiagnostics::analyzeBinding(params); });
    if (method == QLatin1String("Diagnostics.analyzeTree")) {
        return runOnGuiThreadBlocking([&]() {
            return QQmlAgentDiagnostics::analyzeTree(params, &m_logs);
        });
    }
    if (method == QLatin1String("Input.clickNode"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentInput::clickNode(params); });
    if (method == QLatin1String("Input.wheel"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentInput::wheel(params); });
    if (method == QLatin1String("Input.focusNode"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentInput::focusNode(params); });
    if (method == QLatin1String("Input.dispatchMouseEvent"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentInput::dispatchMouseEvent(params); });
    if (method == QLatin1String("Input.dragNode"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentInput::dragNode(params); });
    if (method == QLatin1String("Input.dispatchTouchEvent"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentInput::dispatchTouchEvent(params); });
    if (method == QLatin1String("Input.dispatchKeyEvent"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentInput::dispatchKeyEvent(params); });
    if (method == QLatin1String("Input.typeText"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentInput::typeText(params); });
    if (method == QLatin1String("Render.captureScreenshot"))
        return runOnGuiThreadBlocking([&]() { return QQmlAgentRender::captureScreenshot(params); });
    if (method == QLatin1String("Runtime.setProperty")) {
        if (!m_runtimeMutationEnabled)
            return runtimeMutationDisabledResult();
        return runOnGuiThreadBlocking([&]() { return QQmlAgentRuntime::setProperty(params); });
    }
    if (method == QLatin1String("Runtime.invokeMethod")) {
        if (!m_runtimeMutationEnabled)
            return runtimeMutationDisabledResult();
        return runOnGuiThreadBlocking([&]() { return QQmlAgentRuntime::invokeMethod(params); });
    }
    if (method == QLatin1String("Source.resolveNode")) {
        return runOnGuiThreadBlocking([&]() {
            const QQmlAgentUiTree::NodeRef ref = QQmlAgentUiTree::resolveNodeRef(params);
            const QJsonObject location = QQmlAgentSourceResolver::sourceLocationForObject(ref.object);
            QJsonObject result{
                { QStringLiteral("location"), location },
                { QStringLiteral("fallbackLocations"),
                  QQmlAgentSourceResolver::fallbackLocationsForObject(ref.object, location) },
            };
            if (!ref.node.isEmpty())
                result.insert(QStringLiteral("node"), ref.node);
            if (!ref.issues.isEmpty())
                result.insert(QStringLiteral("issues"), ref.issues);
            return result;
        });
    }

    return {};
}

QJsonObject QQmlAgentService::sessionInfo() const
{
    QJsonArray features{
        QStringLiteral("Session.getInfo"),
        QStringLiteral("Session.configure"),
        QStringLiteral("Session.reset"),
        QStringLiteral("UI.getTree"),
        QStringLiteral("UI.query"),
        QStringLiteral("UI.waitFor"),
        QStringLiteral("UI.describeNode"),
        QStringLiteral("UI.getBoxModel"),
        QStringLiteral("UI.subscribe"),
        QStringLiteral("UI.unsubscribe"),
        QStringLiteral("Input.clickNode"),
        QStringLiteral("Input.wheel"),
        QStringLiteral("Input.focusNode"),
        QStringLiteral("Input.dispatchMouseEvent"),
        QStringLiteral("Input.dragNode"),
        QStringLiteral("Input.dispatchTouchEvent"),
        QStringLiteral("Input.dispatchKeyEvent"),
        QStringLiteral("Input.typeText"),
        QStringLiteral("Render.captureScreenshot"),
        QStringLiteral("Log.enable"),
        QStringLiteral("Log.getEntries"),
        QStringLiteral("Log.clear"),
        QStringLiteral("Diagnostics.analyzeNode"),
        QStringLiteral("Diagnostics.analyzeTree"),
        QStringLiteral("Diagnostics.analyzeBinding"),
        QStringLiteral("Source.resolveNode"),
    };
    if (m_runtimeMutationEnabled) {
        features.append(QStringLiteral("Runtime.setProperty"));
        features.append(QStringLiteral("Runtime.invokeMethod"));
    }

    return {
        { QStringLiteral("service"), serviceKey() },
        { QStringLiteral("protocolVersion"), QStringLiteral("0.1") },
        { QStringLiteral("qtVersion"), QString::fromUtf8(qVersion()) },
        { QStringLiteral("sessionId"), m_sessionId },
        { QStringLiteral("processId"), QCoreApplication::applicationPid() },
        { QStringLiteral("features"), features },
        { QStringLiteral("capabilities"), QJsonObject{
            { QStringLiteral("runtimeMutation"), QJsonObject{
                { QStringLiteral("enabled"), m_runtimeMutationEnabled },
                { QStringLiteral("verificationRole"), QStringLiteral("setup-only") },
                { QStringLiteral("configureWith"), QStringLiteral("Session.configure runtimeMutation=true") },
            } },
            { QStringLiteral("renderScreenshot"), QJsonObject{
                { QStringLiteral("enabled"), true },
                { QStringLiteral("evidenceRole"), QStringLiteral("fallback-supporting") },
            } },
        } },
    };
}

QJsonObject QQmlAgentService::sessionConfigure(const QJsonObject &params)
{
    if (params.contains(QStringLiteral("runtimeMutation")))
        m_runtimeMutationEnabled = params.value(QStringLiteral("runtimeMutation")).toBool(false);
    return sessionInfo();
}

QJsonObject QQmlAgentService::runtimeMutationDisabledResult() const
{
    return {
        { QStringLiteral("ok"), false },
        { QStringLiteral("mode"), QStringLiteral("whitebox") },
        { QStringLiteral("verificationRole"), QStringLiteral("setup-only") },
        { QStringLiteral("diagnostics"), QJsonArray{ QJsonObject{
            { QStringLiteral("id"), QStringLiteral("runtime.capability_disabled") },
            { QStringLiteral("severity"), QStringLiteral("error") },
            { QStringLiteral("confidence"), 1.0 },
            { QStringLiteral("message"),
              QStringLiteral("Runtime mutation is disabled. Enable explicitly with Session.configure {\"runtimeMutation\":true}.") },
        } } },
    };
}

QJsonObject QQmlAgentService::subscribeUi(const QJsonObject &params)
{
    Q_UNUSED(params);

    m_uiSubscribed.store(true);
    installUiWatchers();
    return {
        { QStringLiteral("enabled"), true },
        { QStringLiteral("events"), QJsonArray{
            QStringLiteral("UI.treeChanged"),
        } },
        { QStringLiteral("mode"), QStringLiteral("coalesced-runtime-observation") },
    };
}

QJsonObject QQmlAgentService::unsubscribeUi()
{
    m_uiSubscribed.store(false);
    m_uiEventPending.store(false);
    m_uiWatcherRefreshPending.store(false);
    clearUiWatchers();
    return { { QStringLiteral("enabled"), false } };
}

void QQmlAgentService::installUiWatchers()
{
    clearUiWatchers();

    QSet<QObject *> seen;
    int windowId = 0;
    const QWindowList windows = QGuiApplication::allWindows();
    for (QWindow *window : windows) {
        QQuickWindow *quickWindow = qobject_cast<QQuickWindow *>(window);
        if (!quickWindow || !quickWindow->contentItem())
            continue;

        ++windowId;
        const int capturedWindowId = windowId;
        m_uiConnections.append(connect(quickWindow, &QWindow::widthChanged, this,
                                       [this, capturedWindowId]() {
            scheduleUiChangedEvent(QStringLiteral("window-geometry-changed"), capturedWindowId);
        }));
        m_uiConnections.append(connect(quickWindow, &QWindow::heightChanged, this,
                                       [this, capturedWindowId]() {
            scheduleUiChangedEvent(QStringLiteral("window-geometry-changed"), capturedWindowId);
        }));
        m_uiConnections.append(connect(quickWindow, &QWindow::visibleChanged, this,
                                       [this, capturedWindowId]() {
            scheduleUiChangedEvent(QStringLiteral("window-visible-changed"), capturedWindowId);
        }));
        watchUiObject(quickWindow->contentItem(), &seen);
    }
}

void QQmlAgentService::clearUiWatchers()
{
    for (const QMetaObject::Connection &connection : std::as_const(m_uiConnections))
        disconnect(connection);
    m_uiConnections.clear();
}

void QQmlAgentService::watchUiObject(QObject *object, QSet<QObject *> *seen)
{
    if (!object || seen->contains(object))
        return;

    seen->insert(object);
    m_uiConnections.append(connect(object, &QObject::objectNameChanged, this,
                                   [this]() {
        scheduleUiChangedEvent(QStringLiteral("object-name-changed"));
    }));

    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    if (!item)
        return;

    m_uiConnections.append(connect(item, &QQuickItem::xChanged, this,
                                   [this]() { scheduleUiChangedEvent(QStringLiteral("item-geometry-changed")); }));
    m_uiConnections.append(connect(item, &QQuickItem::yChanged, this,
                                   [this]() { scheduleUiChangedEvent(QStringLiteral("item-geometry-changed")); }));
    m_uiConnections.append(connect(item, &QQuickItem::widthChanged, this,
                                   [this]() { scheduleUiChangedEvent(QStringLiteral("item-geometry-changed")); }));
    m_uiConnections.append(connect(item, &QQuickItem::heightChanged, this,
                                   [this]() { scheduleUiChangedEvent(QStringLiteral("item-geometry-changed")); }));
    m_uiConnections.append(connect(item, &QQuickItem::visibleChanged, this,
                                   [this]() { scheduleUiChangedEvent(QStringLiteral("item-visible-changed")); }));
    m_uiConnections.append(connect(item, &QQuickItem::opacityChanged, this,
                                   [this]() { scheduleUiChangedEvent(QStringLiteral("item-opacity-changed")); }));
    m_uiConnections.append(connect(item, &QQuickItem::childrenChanged, this,
                                   [this]() {
        scheduleUiChangedEvent(QStringLiteral("item-children-changed"));
        scheduleUiWatcherRefresh();
    }));

    for (QQuickItem *child : item->childItems())
        watchUiObject(child, seen);
}

void QQmlAgentService::scheduleUiWatcherRefresh()
{
    if (!m_uiSubscribed.load())
        return;

    bool expected = false;
    if (!m_uiWatcherRefreshPending.compare_exchange_strong(expected, true))
        return;

    QMetaObject::invokeMethod(qApp, [this]() {
        m_uiWatcherRefreshPending.store(false);
        if (!m_uiSubscribed.load())
            return;
        installUiWatchers();
    }, Qt::QueuedConnection);
}

void QQmlAgentService::scheduleUiChangedEvent(const QString &reason, int windowId)
{
    if (!m_uiSubscribed.load())
        return;

    bool expected = false;
    if (!m_uiEventPending.compare_exchange_strong(expected, true))
        return;

    QMetaObject::invokeMethod(this, [this, reason, windowId]() {
        m_uiEventPending.store(false);
        if (!m_uiSubscribed.load())
            return;
        sendUiChangedEvent(reason, windowId);
    }, Qt::QueuedConnection);
}

void QQmlAgentService::sendUiChangedEvent(const QString &reason, int windowId)
{
    QJsonObject params{
        { QStringLiteral("reason"), reason },
        { QStringLiteral("sequence"), ++m_uiEventSequence },
    };
    if (windowId > 0)
        params.insert(QStringLiteral("windowId"), windowId);

    sendProtocolMessage(QQmlAgentProtocol::event(QStringLiteral("UI.treeChanged"), params));
}

QT_END_NAMESPACE

#include "moc_qqmlagentservice_p.cpp"
