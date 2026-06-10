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
#include <QtCore/qpointer.h>
#include <QtCore/qset.h>
#include <QtCore/qsemaphore.h>
#include <QtCore/qsharedpointer.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qthread.h>
#include <QtCore/quuid.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qwindow.h>
#include <QtQml/qjsengine.h>
#include <QtQml/qqmlengine.h>
#include <QtQuick/qquickitem.h>
#include <QtQuick/qquickwindow.h>

#include <atomic>
#include <utility>

QT_BEGIN_NAMESPACE

static constexpr int GuiThreadDispatchTimeoutMs = 5000;
static constexpr int MaxGuiDispatchExtraBudgetMs = 120000;
static constexpr int MaxUiWatcherDepth = 256;

// Long-running requests (UI.waitFor timeouts, Input.longPressNode holds,
// settle windows) legitimately occupy the GUI thread beyond the base dispatch
// timeout. Extend the dispatch deadline by the budget each implementation
// derives from its own request parameters so the debug thread does not report
// a false session.gui_thread_timeout while the request is still running.
static int guiDispatchTimeoutMsFor(const QString &method, const QJsonObject &params)
{
    int extraBudgetMs = 0;
    if (method == QLatin1String("UI.waitFor"))
        extraBudgetMs = QQmlAgentUiTree::waitForBudgetMs(params);
    else if (method == QLatin1String("UI.queryMany"))
        extraBudgetMs = QQmlAgentUiTree::queryManyBudgetMs(params);
    else if (method.startsWith(QLatin1String("Input.")))
        extraBudgetMs = QQmlAgentInput::dispatchBudgetMs(method, params);
    else if (method.startsWith(QLatin1String("Runtime.")))
        extraBudgetMs = QQmlAgentRuntime::dispatchBudgetMs(params);
    return GuiThreadDispatchTimeoutMs + qBound(0, extraBudgetMs, MaxGuiDispatchExtraBudgetMs);
}

static const QStringList &agentMethods()
{
    static const QStringList methods{
        QStringLiteral("Session.getInfo"),
        QStringLiteral("Session.configure"),
        QStringLiteral("Session.reset"),
        QStringLiteral("Log.enable"),
        QStringLiteral("Log.getEntries"),
        QStringLiteral("Log.clear"),
        QStringLiteral("UI.getTree"),
        QStringLiteral("UI.query"),
        QStringLiteral("UI.queryMany"),
        QStringLiteral("UI.waitFor"),
        QStringLiteral("UI.describeNode"),
        QStringLiteral("UI.getBoxModel"),
        QStringLiteral("UI.subscribe"),
        QStringLiteral("UI.unsubscribe"),
        QStringLiteral("Diagnostics.analyzeNode"),
        QStringLiteral("Diagnostics.analyzeTree"),
        QStringLiteral("Diagnostics.analyzeBinding"),
        QStringLiteral("Input.clickNode"),
        QStringLiteral("Input.longPressNode"),
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
    return methods;
}

static const QSet<QString> &agentMethodSet()
{
    static const QSet<QString> methods(agentMethods().cbegin(), agentMethods().cend());
    return methods;
}

static bool methodRequiresRuntimeMutation(const QString &method)
{
    return method == QLatin1String("Runtime.setProperty")
            || method == QLatin1String("Runtime.invokeMethod");
}

static QByteArray boundedResponse(const QJsonValue &id, const QString &method,
                                  const QJsonObject &result)
{
    const QByteArray response = QQmlAgentProtocol::response(id, result);
    if (response.size() <= QQmlAgentProtocol::MaxOutboundMessageBytes)
        return response;

    return QQmlAgentProtocol::error(
            id, -32001, QStringLiteral("QmlAgent response payload is too large"),
            {
                { QStringLiteral("method"), method },
                { QStringLiteral("actualBytes"), response.size() },
                { QStringLiteral("maxBytes"), QQmlAgentProtocol::MaxOutboundMessageBytes },
                { QStringLiteral("hints"), QJsonArray{
                    QStringLiteral("Narrow the selector or use UI.query instead of UI.getTree."),
                    QStringLiteral("Set depth, maxNodes, fields, properties, includeSource:false, or verbosity:\"summary\" where available."),
                    QStringLiteral("Request full evidence only after the bounded summary points to a specific follow-up."),
                } },
            });
}

static QJsonObject guiThreadTimeoutResult(int timeoutMs)
{
    return {
        { QStringLiteral("ok"), false },
        { QStringLiteral("timedOut"), true },
        { QStringLiteral("diagnostics"), QJsonArray{ QJsonObject{
            { QStringLiteral("id"), QStringLiteral("session.gui_thread_timeout") },
            { QStringLiteral("severity"), QStringLiteral("error") },
            { QStringLiteral("confidence"), 1.0 },
            { QStringLiteral("message"),
              QStringLiteral("Timed out waiting for the GUI thread to run a QmlAgent request.") },
            { QStringLiteral("timeoutMs"), timeoutMs },
            { QStringLiteral("hints"), QJsonArray{
                QStringLiteral("The target GUI thread may be blocked in modal work, long JavaScript, rendering, or native code."),
                QStringLiteral("Check Log.getEntries and target process state; relaunch the target if it is unresponsive."),
            } },
        } } },
    };
}

static QJsonObject serviceDestroyedResult()
{
    return {
        { QStringLiteral("ok"), false },
        { QStringLiteral("diagnostics"), QJsonArray{ QJsonObject{
            { QStringLiteral("id"), QStringLiteral("session.service_destroyed") },
            { QStringLiteral("severity"), QStringLiteral("error") },
            { QStringLiteral("confidence"), 1.0 },
            { QStringLiteral("message"),
              QStringLiteral("QmlAgent service was destroyed before the queued GUI-thread request could run.") },
        } } },
    };
}

template <typename Fn>
static QJsonObject runOnGuiThreadBlocking(Fn &&fn, int timeoutMs = GuiThreadDispatchTimeoutMs)
{
    QObject *application = QCoreApplication::instance();
    if (!application || QThread::currentThread() == application->thread())
        return fn();

    struct GuiCallState
    {
        QSemaphore done;
        QJsonObject result;
        std::atomic_bool cancelled = false;
        std::atomic_bool started = false;
    };

    const auto state = QSharedPointer<GuiCallState>::create();
    if (!QMetaObject::invokeMethod(application, [state, fn = std::forward<Fn>(fn)]() mutable {
            if (state->cancelled.load(std::memory_order_acquire)) {
                state->done.release();
                return;
            }
            state->started.store(true, std::memory_order_release);
            state->result = fn();
            state->done.release();
        }, Qt::QueuedConnection)) {
        QJsonObject result = guiThreadTimeoutResult(timeoutMs);
        result.insert(QStringLiteral("queued"), false);
        return result;
    }

    if (!state->done.tryAcquire(1, timeoutMs)) {
        const bool alreadyStarted = state->started.load(std::memory_order_acquire);
        state->cancelled.store(true, std::memory_order_release);
        QJsonObject result = guiThreadTimeoutResult(timeoutMs);
        result.insert(QStringLiteral("queued"), true);
        result.insert(QStringLiteral("cancelledIfNotStarted"), true);
        result.insert(QStringLiteral("alreadyStarted"), alreadyStarted);
        return result;
    }

    return state->result;
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

void QQmlAgentService::stateAboutToBeChanged(QQmlDebugService::State newState)
{
    if (newState == QQmlDebugService::Enabled)
        return;

    resetSessionState(QStringLiteral("debug-service-state-changed"));
}

void QQmlAgentService::resetSessionState(const QString &reason)
{
    m_uiSubscribed.store(false);
    m_uiEventPending.store(false);
    m_uiWatcherRefreshPending.store(false);
    m_runtimeMutationEnabled = false;
    clearUiWatchers();
    m_logs.reset();
    sendProtocolMessage(QQmlAgentProtocol::event(QStringLiteral("Session.reset"), {
        { QStringLiteral("reason"), reason },
        { QStringLiteral("subscriptionsInvalidated"), true },
    }));
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

    if (!agentMethodSet().contains(request.method)) {
        sendProtocolMessage(QQmlAgentProtocol::error(request.id, -32601,
                                                    QStringLiteral("Method not found")));
        return;
    }

    const QJsonObject result = dispatch(request.method, request.params);
    if (!request.id.isUndefined())
        sendProtocolMessage(boundedResponse(request.id, request.method, result));
}

void QQmlAgentService::sendProtocolMessage(const QByteArray &message)
{
    if (message.size() > QQmlAgentProtocol::MaxOutboundMessageBytes) {
        emit messageToClient(name(), QQmlAgentProtocol::event(
                QStringLiteral("Session.payloadDropped"),
                {
                    { QStringLiteral("actualBytes"), message.size() },
                    { QStringLiteral("maxBytes"), QQmlAgentProtocol::MaxOutboundMessageBytes },
                    { QStringLiteral("hint"),
                      QStringLiteral("Use projection, selectors, maxNodes, summary verbosity, or smaller screenshot scale/region.") },
                }));
        return;
    }

    emit messageToClient(name(), message);
}

void QQmlAgentService::sendEntryEvent(const QJsonObject &entry)
{
    sendProtocolMessage(QQmlAgentProtocol::event(QStringLiteral("Log.entryAdded"),
                                                 { { QStringLiteral("entry"), entry } }));
}

QJsonObject QQmlAgentService::dispatch(const QString &method, const QJsonObject &params)
{
    const QPointer<QQmlAgentService> self(this);
    const int dispatchTimeoutMs = guiDispatchTimeoutMsFor(method, params);

    if (method == QLatin1String("Session.getInfo"))
        return sessionInfo();
    if (method == QLatin1String("Session.configure"))
        return sessionConfigure(params);

    if (method == QLatin1String("Session.reset")) {
        const QString reason = params.value(QStringLiteral("reason")).toString(QStringLiteral("client-request"));
        resetSessionState(reason);
        return {
            { QStringLiteral("reset"), true },
            { QStringLiteral("reason"), reason },
            { QStringLiteral("subscriptionsInvalidated"), true },
        };
    }

    if (method == QLatin1String("Log.enable"))
        return m_logs.enable(params);
    if (method == QLatin1String("Log.getEntries"))
        return m_logs.entries(params);
    if (method == QLatin1String("Log.clear"))
        return m_logs.clear();

    if (method == QLatin1String("UI.getTree"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentUiTree::getTree(params); });
    if (method == QLatin1String("UI.query"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentUiTree::query(params); });
    if (method == QLatin1String("UI.queryMany"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentUiTree::queryMany(params); },
                                      dispatchTimeoutMs);
    if (method == QLatin1String("UI.waitFor"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentUiTree::waitFor(params); },
                                       dispatchTimeoutMs);
    if (method == QLatin1String("UI.describeNode"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentUiTree::describeNode(params); });
    if (method == QLatin1String("UI.getBoxModel"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentUiTree::getBoxModel(params); });
    if (method == QLatin1String("UI.subscribe"))
        return runOnGuiThreadBlocking([self, params]() {
            return self ? self->subscribeUi(params) : serviceDestroyedResult();
        });
    if (method == QLatin1String("UI.unsubscribe"))
        return runOnGuiThreadBlocking([self]() {
            return self ? self->unsubscribeUi() : serviceDestroyedResult();
        });
    if (method == QLatin1String("Diagnostics.analyzeNode"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentDiagnostics::analyzeNode(params); });
    if (method == QLatin1String("Diagnostics.analyzeBinding"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentDiagnostics::analyzeBinding(params); });
    if (method == QLatin1String("Diagnostics.analyzeTree")) {
        return runOnGuiThreadBlocking([self, params]() {
            return self ? QQmlAgentDiagnostics::analyzeTree(params, &self->m_logs)
                        : serviceDestroyedResult();
        });
    }
    if (method == QLatin1String("Input.clickNode"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentInput::clickNode(params); },
                                       dispatchTimeoutMs);
    if (method == QLatin1String("Input.longPressNode"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentInput::longPressNode(params); },
                                       dispatchTimeoutMs);
    if (method == QLatin1String("Input.wheel"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentInput::wheel(params); },
                                       dispatchTimeoutMs);
    if (method == QLatin1String("Input.focusNode"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentInput::focusNode(params); },
                                       dispatchTimeoutMs);
    if (method == QLatin1String("Input.dispatchMouseEvent"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentInput::dispatchMouseEvent(params); },
                                       dispatchTimeoutMs);
    if (method == QLatin1String("Input.dragNode"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentInput::dragNode(params); },
                                       dispatchTimeoutMs);
    if (method == QLatin1String("Input.dispatchTouchEvent"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentInput::dispatchTouchEvent(params); },
                                       dispatchTimeoutMs);
    if (method == QLatin1String("Input.dispatchKeyEvent"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentInput::dispatchKeyEvent(params); },
                                       dispatchTimeoutMs);
    if (method == QLatin1String("Input.typeText"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentInput::typeText(params); },
                                       dispatchTimeoutMs);
    if (method == QLatin1String("Render.captureScreenshot"))
        return runOnGuiThreadBlocking([params]() { return QQmlAgentRender::captureScreenshot(params); });
    if (method == QLatin1String("Runtime.setProperty")) {
        if (!m_runtimeMutationEnabled)
            return runtimeMutationDisabledResult();
        return runOnGuiThreadBlocking([params]() { return QQmlAgentRuntime::setProperty(params); },
                                       dispatchTimeoutMs);
    }
    if (method == QLatin1String("Runtime.invokeMethod")) {
        if (!m_runtimeMutationEnabled)
            return runtimeMutationDisabledResult();
        return runOnGuiThreadBlocking([params]() { return QQmlAgentRuntime::invokeMethod(params); },
                                       dispatchTimeoutMs);
    }
    if (method == QLatin1String("Source.resolveNode")) {
        return runOnGuiThreadBlocking([params]() {
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
    QJsonArray features;
    for (const QString &method : agentMethods()) {
        if (methodRequiresRuntimeMutation(method) && !m_runtimeMutationEnabled)
            continue;
        features.append(method);
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
            { QStringLiteral("payloadLimits"), QJsonObject{
                { QStringLiteral("maxInboundMessageBytes"), QQmlAgentProtocol::MaxInboundMessageBytes },
                { QStringLiteral("maxOutboundMessageBytes"), QQmlAgentProtocol::MaxOutboundMessageBytes },
                { QStringLiteral("overflowBehavior"),
                  QStringLiteral("oversized requests return JSON-RPC errors; oversized responses return a bounded JSON-RPC error with follow-up hints") },
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

void QQmlAgentService::watchUiObject(QObject *object, QSet<QObject *> *seen, int depth)
{
    if (!object || seen->contains(object))
        return;
    if (depth > MaxUiWatcherDepth) {
        scheduleUiChangedEvent(QStringLiteral("watcher-depth-limit"));
        return;
    }

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
        watchUiObject(child, seen, depth + 1);
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
