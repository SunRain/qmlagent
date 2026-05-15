// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "qmlagentmcpprotocol.h"

#include <QtCore/qjsondocument.h>

namespace QmlAgentMcp {

static QJsonObject schema(const QJsonObject &properties, const QJsonArray &required = {})
{
    return {
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("properties"), properties },
        { QStringLiteral("required"), required },
        { QStringLiteral("additionalProperties"), false },
    };
}

static QJsonObject nodeRefSchema()
{
    return {
        { QStringLiteral("selector"), QJsonObject{
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("QmlAgent selector, for example id=\"saveButton\". Repeated delegates may use id/objectName plus index, for example id=\"boxRect\" index=0, when UI.query exposes delegate.index metadata.") },
        } },
        { QStringLiteral("nodeId"), QJsonObject{
            { QStringLiteral("type"), QStringLiteral("integer") },
            { QStringLiteral("description"), QStringLiteral("Session-local node id. Prefer selector when possible.") },
        } },
    };
}

static QJsonObject withNodeRef(QJsonObject properties)
{
    const QJsonObject nodeRef = nodeRefSchema();
    for (auto it = nodeRef.constBegin(), end = nodeRef.constEnd(); it != end; ++it)
        properties.insert(it.key(), it.value());
    return properties;
}

static QJsonObject waitUntilSchema()
{
    return {
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("description"),
          QStringLiteral("Predicate to wait for: state:\"found\"/\"notFound\", or property/op/value such as {\"property\":\"visible\",\"op\":\"=\",\"value\":true}. String properties also support contains, startsWith, and endsWith.") },
        { QStringLiteral("properties"), QJsonObject{
            { QStringLiteral("state"), QJsonObject{
                { QStringLiteral("type"), QStringLiteral("string") },
                { QStringLiteral("enum"), QJsonArray{
                    QStringLiteral("found"),
                    QStringLiteral("notFound"),
                } },
            } },
            { QStringLiteral("property"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
            { QStringLiteral("op"), QJsonObject{
                { QStringLiteral("type"), QStringLiteral("string") },
                { QStringLiteral("enum"), QJsonArray{
                    QStringLiteral("="),
                    QStringLiteral("=="),
                    QStringLiteral("!="),
                    QStringLiteral(">"),
                    QStringLiteral(">="),
                    QStringLiteral("<"),
                    QStringLiteral("<="),
                    QStringLiteral("contains"),
                    QStringLiteral("startsWith"),
                    QStringLiteral("endsWith"),
                } },
            } },
            { QStringLiteral("value"), QJsonObject{
                { QStringLiteral("description"), QStringLiteral("Expected primitive JSON value for property comparisons.") },
            } },
            { QStringLiteral("timeoutMs"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
        } },
        { QStringLiteral("additionalProperties"), false },
    };
}

static QJsonObject tool(const QString &name, const QString &description,
                        const QJsonObject &inputSchema)
{
    return {
        { QStringLiteral("name"), name },
        { QStringLiteral("description"), description },
        { QStringLiteral("inputSchema"), inputSchema },
    };
}

QJsonArray toolList()
{
    const QJsonObject stringArray{
        { QStringLiteral("type"), QStringLiteral("array") },
        { QStringLiteral("items"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
    };

    return {
        tool(QStringLiteral("qmlagent.connect_tcp"),
             QStringLiteral("Attach this MCP server directly to a manually launched Qt app using -qmljsdebugger=port:<port>,host:<host>,services:QmlAgent. Default host/port is 127.0.0.1:3768; use that fixed port unless the handoff explicitly says otherwise. Do not scan or pre-bind ports. If the app was started with qmlagent-launcher preview <Main.qml> or qmlagent-launcher app <executable>, request/response tools auto-route through the launcher and this attach step is not needed. Use direct attach for streamed subscriptions such as qmlagent.ui_subscribe and qmlagent.log_enable."),
             schema({
                 { QStringLiteral("host"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("description"), QStringLiteral("TCP host. Defaults to 127.0.0.1.") },
                 } },
                 { QStringLiteral("port"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("integer") },
                     { QStringLiteral("description"), QStringLiteral("TCP port. Defaults to 3768.") },
                 } },
                 { QStringLiteral("timeoutMs"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("integer") },
                     { QStringLiteral("description"), QStringLiteral("Connection timeout in milliseconds.") },
                 } },
             })),
        tool(QStringLiteral("qmlagent.connect_local_socket"),
             QStringLiteral("Attach this MCP server directly to a manually launched Qt app using -qmljsdebugger=file:<path>,services:QmlAgent. If the app was started with qmlagent-launcher preview <Main.qml> or qmlagent-launcher app <executable>, request/response tools auto-route through the launcher and this attach step is not needed. Use direct attach for streamed subscriptions such as qmlagent.ui_subscribe and qmlagent.log_enable."),
             schema({
                 { QStringLiteral("path"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("description"), QStringLiteral("Local socket path or name passed to -qmljsdebugger=file:<path>.") },
                 } },
                 { QStringLiteral("timeoutMs"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("integer") },
                     { QStringLiteral("description"), QStringLiteral("Connection timeout in milliseconds.") },
                 } },
             }, { QStringLiteral("path") })),
        tool(QStringLiteral("qmlagent.disconnect"),
             QStringLiteral("Detach from the current QmlAgent target. Use before relaunching or switching target processes."),
             schema({})),
        tool(QStringLiteral("qmlagent.target_status"),
             QStringLiteral("Start here. Return whether this MCP server is directly attached and whether a qmlagent-launcher gateway is available for automatic request/response routing. If a single qmlagent-launcher session exists, qmlagent.ui_query, qmlagent.input_click, qmlagent.ui_wait_for, qmlagent.preview_reload, and workflow tools work without qmlagent.connect_tcp/connect_local_socket. Direct attach is still required for streamed subscriptions."),
             schema({})),
        tool(QStringLiteral("qmlagent.launcher_status"),
             QStringLiteral("Discover qmlagent-launcher sessions in the current workspace. If exactly one live session exists, target-backed request/response MCP tools auto-route through it. The result names the exact launch form and whether qmlagent.preview_reload is supported."),
             schema({})),
        tool(QStringLiteral("qmlagent.preview_reload"),
             QStringLiteral("Reload the root QML file for a session started exactly with qmlagent-launcher preview <Main.qml>. This does not work for qmlagent-launcher app <executable> sessions; those must rebuild/relaunch unless the app owns its own reload boundary."),
             schema({
                 { QStringLiteral("timeoutMs"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("integer") },
                     { QStringLiteral("description"), QStringLiteral("Reload timeout in milliseconds. Verify the new state separately with qmlagent.ui_wait_for or qmlagent.ui_query.") },
                 } },
             })),
        tool(QStringLiteral("qmlagent.launcher_stop"),
             QStringLiteral("Stop the current qmlagent-launcher session discovered in the workspace. Use this to close a preview or application session without manual process killing."),
             schema({})),
        tool(QStringLiteral("qmlagent.ui_get_tree"),
             QStringLiteral("Get a projected Qt Quick UI tree. Keep fields/maxNodes bounded unless full evidence is required. If selector is supplied without depth, the adapter searches the full tree and returns the matched branch."),
             schema({
                 { QStringLiteral("depth"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
                 { QStringLiteral("includeInvisible"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } },
                 { QStringLiteral("includeSource"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } },
                 { QStringLiteral("fields"), stringArray },
                 { QStringLiteral("properties"), stringArray },
                 { QStringLiteral("selector"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("maxNodes"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
                 { QStringLiteral("collapseRepeated"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } },
             })),
        tool(QStringLiteral("qmlagent.ui_query"),
             QStringLiteral("Query nodes by stable QmlAgent selector. Defaults to verbosity=\"summary\" to protect agent context; ask for verbosity=\"full\" only when omittedFields/nextHints say deeper node evidence is needed. Prefer selector over nodeId across restarts. For repeated delegates, try id/objectName plus index such as id=\"boxRect\" index=0 before adding objectName or using session-local nodeId. Requested properties are returned even when fields is projected. If a single qmlagent-launcher session exists, this routes through the launcher automatically; no direct attach is needed. Use this to find selectors before qmlagent.input_drag, qmlagent.input_wheel, qmlagent.ui_wait_for, or qmlagent.workflow_click_and_wait."),
             schema({
                 { QStringLiteral("selector"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("includeSource"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } },
                 { QStringLiteral("fields"), stringArray },
                 { QStringLiteral("properties"), stringArray },
                 { QStringLiteral("maxNodes"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
                 { QStringLiteral("verbosity"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("default"), QStringLiteral("summary") },
                     { QStringLiteral("enum"), QJsonArray{
                         QStringLiteral("full"),
                         QStringLiteral("summary"),
                     } },
                 } },
             }, { QStringLiteral("selector") })),
        tool(QStringLiteral("qmlagent.ui_wait_for"),
             QStringLiteral("Agent-first semantic wait tool. Wait until a selector is found/notFound or one selected node property satisfies a predicate. Use after qmlagent.input_click, qmlagent.input_drag, qmlagent.input_wheel, loaders, popups, Drawer/Menu/Popup/Dialog transitions, and animated controls instead of sleeps, retry loops, screenshots, or frame-count tuning. For Qt Quick Controls popups, wait for generic popup evidence such as type=\"QQuickPopupItem\" or a visible popup property, then query the popup contents; style implementations may expose choices as ItemDelegate rather than MenuItem. If qmlagent.workflow_click_and_wait is not visible in lazy native-tool discovery, use qmlagent.input_click followed by this qmlagent.ui_wait_for tool. Timeout results include targeted nextHints for UI.query/Diagnostics follow-up."),
             schema({
                 { QStringLiteral("selector"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("until"), waitUntilSchema() },
                 { QStringLiteral("timeoutMs"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("integer") },
                     { QStringLiteral("description"), QStringLiteral("Maximum wait in milliseconds. Defaults to the service default when omitted.") },
                 } },
             }, { QStringLiteral("selector"), QStringLiteral("until") })),
        tool(QStringLiteral("qmlagent.ui_subscribe"),
             QStringLiteral("Subscribe to coalesced QmlAgent UI.treeChanged events on this persistent connection."),
             schema({})),
        tool(QStringLiteral("qmlagent.ui_unsubscribe"),
             QStringLiteral("Unsubscribe from QmlAgent UI.treeChanged events on this persistent connection."),
             schema({})),
        tool(QStringLiteral("qmlagent.diagnostics_analyze_tree"),
             QStringLiteral("Analyze the runtime tree for structured layout/input/log issues. Defaults to application repair scope. Use verbosity:\"summary\" for bounded agent-loop output; use evidence/full only when patch-ready detail is needed. Pass includeFrameworkIssues:true or issueScope:\"all\" when developing Qt Quick Controls/framework internals."),
             schema({
                 { QStringLiteral("includeInvisible"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } },
                 { QStringLiteral("includeFrameworkIssues"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } },
                 { QStringLiteral("maxIssues"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
                 { QStringLiteral("issueScope"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("enum"), QJsonArray{ QStringLiteral("application"), QStringLiteral("all") } },
                 } },
                 { QStringLiteral("verbosity"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("default"), QStringLiteral("summary") },
                     { QStringLiteral("enum"), QJsonArray{
                         QStringLiteral("summary"),
                         QStringLiteral("evidence"),
                         QStringLiteral("full"),
                     } },
                 } },
             })),
        tool(QStringLiteral("qmlagent.diagnostics_analyze_node"),
             QStringLiteral("Analyze one node by selector or nodeId and return evidence-backed issues."),
             schema(withNodeRef({ { QStringLiteral("checks"), stringArray } }))),
        tool(QStringLiteral("qmlagent.diagnostics_analyze_binding"),
             QStringLiteral("Resolve runtime binding provenance for one property. Use this when geometry/state looks computed: it reports active QQmlBinding or Qt bindable-property binding evidence, current value, source location, bounded source snippet, candidate identifier follow-up hints, currently captured dependency values when Qt exposes them, and bindable-property dependency summaries when values are not exposed. Source-token identifiers are hints, not dependency proof. This is structured repair evidence; it does not mutate the app."),
             schema(withNodeRef({
                 { QStringLiteral("property"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("description"), QStringLiteral("Property to inspect, for example x, y, width, height, visible, enabled, text, or color.") },
                 } },
             }), { QStringLiteral("property") })),
        tool(QStringLiteral("qmlagent.runtime_enable_mutation"),
             QStringLiteral("Explicitly enable setup-only Runtime.* mutation commands for this QmlAgent debug session. Required before qmlagent.runtime_set_property or qmlagent.runtime_invoke_method."),
             schema({
                 { QStringLiteral("enabled"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("boolean") },
                     { QStringLiteral("description"), QStringLiteral("Defaults to true. Set false to disable Runtime.* mutation again.") },
                 } },
             })),
        tool(QStringLiteral("qmlagent.input_click"),
             QStringLiteral("Click one node through Qt synthetic input. Completion includes QmlAgent settle metadata. For transitions, Drawer/Menu/Popup/Dialog open-close, loaders, or async state, prefer qmlagent.workflow_click_and_wait; if that tool is not visible in lazy native-tool discovery, call qmlagent.input_click then qmlagent.ui_wait_for."),
             schema(withNodeRef({}))),
        tool(QStringLiteral("qmlagent.input_long_press"),
             QStringLiteral("Long-press one node through Qt synthetic mouse input as one atomic action: press, hold, release cleanup, then settle. Use for MouseArea.onPressAndHold, context actions, press-and-hold affordances, and mobile-style UI. Prefer this over manual qmlagent.input_mouse press/wait/release sequences so agents do not leave a held button after interruption. Verify post-action state with qmlagent.ui_wait_for or use qmlagent.workflow_long_press_and_wait."),
             schema(withNodeRef({
                 { QStringLiteral("holdMs"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("integer") },
                     { QStringLiteral("description"), QStringLiteral("Hold duration in milliseconds. Defaults to 900.") },
                 } },
                 { QStringLiteral("button"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("point"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("array") },
                     { QStringLiteral("items"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("number") } } },
                 } },
                 { QStringLiteral("modifiers"), stringArray },
             }))),
        tool(QStringLiteral("qmlagent.input_wheel"),
             QStringLiteral("Dispatch one wheel event at a selector/node center through Qt synthetic input. Use negative deltaY to scroll down in normal Qt Quick Flickable/ListView/GridView/TableView/TreeView/ScrollView content. After scrolling, verify with qmlagent.ui_wait_for or qmlagent.ui_query."),
             schema(withNodeRef({
                 { QStringLiteral("deltaX"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
                 { QStringLiteral("deltaY"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
                 { QStringLiteral("angleDelta"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("array") },
                     { QStringLiteral("items"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
                 } },
                 { QStringLiteral("pixelDelta"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("array") },
                     { QStringLiteral("items"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
                 } },
                 { QStringLiteral("modifiers"), stringArray },
             }))),
        tool(QStringLiteral("qmlagent.input_focus"),
             QStringLiteral("Focus one QQuickItem by selector or nodeId for keyboard input."),
             schema(withNodeRef({}))),
        tool(QStringLiteral("qmlagent.input_mouse"),
             QStringLiteral("Dispatch one bounded mouse press/move/release event at a selector/node item point through Qt synthetic input. Use press/move/release sequences for drag verification."),
             schema(withNodeRef({
                 { QStringLiteral("type"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("enum"), QJsonArray{
                         QStringLiteral("mousePress"),
                         QStringLiteral("mouseMove"),
                         QStringLiteral("mouseRelease"),
                     } },
                 } },
                 { QStringLiteral("button"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("buttons"), stringArray },
                 { QStringLiteral("point"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("array") },
                     { QStringLiteral("items"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("number") } } },
                 } },
                 { QStringLiteral("modifiers"), stringArray },
             }), { QStringLiteral("type") })),
        tool(QStringLiteral("qmlagent.input_drag"),
             QStringLiteral("Drag one selector/node through Qt mouse input. Use for Slider, RangeSlider, Dial, splitters, handles, swipe gestures, and draggable controls. Provide item-local to:[x,y] or delta:[dx,dy]; the tool sends press, at least two moves, and release, then returns settle metadata. After animated drag state, verify with qmlagent.ui_wait_for or qmlagent.ui_query."),
             schema(withNodeRef({
                 { QStringLiteral("from"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("array") },
                     { QStringLiteral("items"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("number") } } },
                 } },
                 { QStringLiteral("to"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("array") },
                     { QStringLiteral("items"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("number") } } },
                 } },
                 { QStringLiteral("delta"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("array") },
                     { QStringLiteral("items"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("number") } } },
                 } },
                 { QStringLiteral("steps"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
                 { QStringLiteral("button"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("modifiers"), stringArray },
             }))),
        tool(QStringLiteral("qmlagent.input_touch"),
             QStringLiteral("Dispatch one bounded touch begin/update/end/cancel event at selector/node item-local points through Qt synthetic input. Use explicit point ids and states for multipoint sequences."),
             schema(withNodeRef({
                 { QStringLiteral("type"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("enum"), QJsonArray{
                         QStringLiteral("touchBegin"),
                         QStringLiteral("touchUpdate"),
                         QStringLiteral("touchEnd"),
                         QStringLiteral("touchCancel"),
                     } },
                 } },
                 { QStringLiteral("points"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("array") },
                     { QStringLiteral("items"), QJsonObject{
                         { QStringLiteral("type"), QStringLiteral("object") },
                         { QStringLiteral("properties"), QJsonObject{
                             { QStringLiteral("id"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
                             { QStringLiteral("state"), QJsonObject{
                                 { QStringLiteral("type"), QStringLiteral("string") },
                                 { QStringLiteral("enum"), QJsonArray{
                                     QStringLiteral("pressed"),
                                     QStringLiteral("updated"),
                                     QStringLiteral("stationary"),
                                     QStringLiteral("released"),
                                 } },
                             } },
                             { QStringLiteral("point"), QJsonObject{
                                 { QStringLiteral("type"), QStringLiteral("array") },
                                 { QStringLiteral("items"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("number") } } },
                             } },
                         } },
                     } },
                 } },
                 { QStringLiteral("modifiers"), stringArray },
             }), { QStringLiteral("type"), QStringLiteral("points") })),
        tool(QStringLiteral("qmlagent.input_key"),
             QStringLiteral("Dispatch a key event, optionally targeting selector/nodeId first."),
             schema(withNodeRef({
                 { QStringLiteral("key"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("keyCode"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
                 { QStringLiteral("text"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("type"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("enum"), QJsonArray{
                         QStringLiteral("keyClick"),
                         QStringLiteral("keyPress"),
                         QStringLiteral("keyRelease"),
                     } },
                 } },
                 { QStringLiteral("modifiers"), stringArray },
             }))),
        tool(QStringLiteral("qmlagent.input_type_text"),
             QStringLiteral("Type text through synthetic key input, optionally targeting selector/nodeId first. This appends/replaces according to the target's normal cursor/selection state; use qmlagent.input_clear_text first when you need an empty field. If click-to-focus fails, call qmlagent.input_focus on the same selector/nodeId, then retry qmlagent.input_type_text; focus_failed results include nextHints."),
             schema(withNodeRef({ { QStringLiteral("text"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } } }),
                    { QStringLiteral("text") })),
        tool(QStringLiteral("qmlagent.input_clear_text"),
             QStringLiteral("Clear a TextInput/TextField/TextArea-style target through the same Input.typeText path: focus target, select existing text when available, then delete through Qt key input. Use qmlagent.input_type_text afterward to enter new text, and verify final text/state with qmlagent.ui_query or qmlagent.ui_wait_for."),
             schema(withNodeRef({}))),
        tool(QStringLiteral("qmlagent.workflow_click"),
             QStringLiteral("Click a target selector and verify an immediate expected state in one dispatcher-owned workflow report. Use verbosity=\"summary\" for normal agent loops; use full only when deep evidence is needed. For Drawer/Menu/Popup/Dialog transitions, loaders, animated controls, or delayed availability, use qmlagent.workflow_click_and_wait instead of this qmlagent.workflow_click tool."),
             schema({
                 { QStringLiteral("selector"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("expectSelector"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("expect"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("description"), QStringLiteral("Property expectation such as visible=true or text!=\"\".") },
                 } },
                 { QStringLiteral("verbosity"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("enum"), QJsonArray{
                         QStringLiteral("full"),
                         QStringLiteral("summary"),
                     } },
                 } },
             }, { QStringLiteral("selector"), QStringLiteral("expectSelector"), QStringLiteral("expect") })),
        tool(QStringLiteral("qmlagent.workflow_click_and_wait"),
             QStringLiteral("Agent-first compressed workflow for transitions/popups: click a target selector through Qt input, subscribe to UI events, then UI.waitFor a semantic predicate in one report. Prefer this over qmlagent.input_click plus manual sleeps/retries for Drawer/Menu/Popup transitions, animations, loaders, and post-click async state. For Controls popups, prefer waiting for type=\"QQuickPopupItem\" or another generic popup/container selector, then query visible ItemDelegate/MenuItem/etc. contents because platform styles do not expose one uniform item type."),
             schema({
                 { QStringLiteral("selector"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("description"), QStringLiteral("Target selector to click.") },
                 } },
                 { QStringLiteral("waitSelector"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("description"), QStringLiteral("Selector whose state/property should satisfy the wait predicate after the click.") },
                 } },
                 { QStringLiteral("until"), waitUntilSchema() },
                 { QStringLiteral("timeoutMs"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
                 { QStringLiteral("verbosity"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("enum"), QJsonArray{
                         QStringLiteral("full"),
                         QStringLiteral("summary"),
                     } },
                 } },
             }, { QStringLiteral("selector"), QStringLiteral("waitSelector"), QStringLiteral("until") })),
        tool(QStringLiteral("qmlagent.workflow_long_press_and_wait"),
             QStringLiteral("Agent-first compressed workflow for press-and-hold UI: long-press a target selector, release safely, then UI.waitFor a semantic predicate in one report. Use for MouseArea.onPressAndHold, context menus, mobile-style alternate actions, and hidden affordances. Prefer this over qmlagent.input_mouse press + sleep + release."),
             schema({
                 { QStringLiteral("selector"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("description"), QStringLiteral("Target selector to long-press.") },
                 } },
                 { QStringLiteral("waitSelector"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("description"), QStringLiteral("Selector whose state/property should satisfy the wait predicate after the long press.") },
                 } },
                 { QStringLiteral("until"), waitUntilSchema() },
                 { QStringLiteral("holdMs"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("integer") },
                     { QStringLiteral("description"), QStringLiteral("Hold duration in milliseconds. Defaults to 900.") },
                 } },
                 { QStringLiteral("timeoutMs"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
                 { QStringLiteral("verbosity"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("enum"), QJsonArray{
                         QStringLiteral("full"),
                         QStringLiteral("summary"),
                     } },
                 } },
             }, { QStringLiteral("selector"), QStringLiteral("waitSelector"), QStringLiteral("until") })),
        tool(QStringLiteral("qmlagent.workflow_key"),
             QStringLiteral("Dispatch a key to a target selector and verify expected state in one dispatcher-owned workflow report. Expectations support property=value, numeric comparisons, and string operators such as text contains \"google\" / text startsWith \"https://\". Use verbosity=\"summary\" for normal agent loops; use full only when deep evidence is needed."),
             schema({
                 { QStringLiteral("selector"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("key"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("expectSelector"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("expect"), QJsonObject{
                    { QStringLiteral("type"), QStringLiteral("string") },
                    { QStringLiteral("description"), QStringLiteral("Property expectation such as visible=true, text!=\"\", text contains \"google\", or text startsWith \"https://\".") },
                } },
                 { QStringLiteral("verbosity"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("string") },
                     { QStringLiteral("enum"), QJsonArray{
                         QStringLiteral("full"),
                         QStringLiteral("summary"),
                     } },
                 } },
             }, { QStringLiteral("selector"), QStringLiteral("key"), QStringLiteral("expectSelector"), QStringLiteral("expect") })),
        tool(QStringLiteral("qmlagent.runtime_set_property"),
             QStringLiteral("White-box setup only: set one QObject/QML property by selector or nodeId. Requires qmlagent.runtime_enable_mutation first. Verify final behavior with UI/Input/Diagnostics/Log evidence; do not use as proof that a user can interact with the UI."),
             schema(withNodeRef({
                 { QStringLiteral("property"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("value"), QJsonObject{ { QStringLiteral("description"), QStringLiteral("Primitive JSON value, or simple primitive array/object when QVariant conversion is trivial.") } } },
                 { QStringLiteral("settle"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("object") } } },
             }), { QStringLiteral("property"), QStringLiteral("value") })),
        tool(QStringLiteral("qmlagent.runtime_invoke_method"),
             QStringLiteral("White-box setup only: invoke a public slot/Q_INVOKABLE by selector or nodeId with primitive args. Requires qmlagent.runtime_enable_mutation first. Verify final behavior with UI/Input/Diagnostics/Log evidence; do not use as proof that a control is clickable."),
             schema(withNodeRef({
                 { QStringLiteral("method"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("args"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("array") },
                     { QStringLiteral("items"), QJsonObject{} },
                 } },
                 { QStringLiteral("settle"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("object") } } },
             }), { QStringLiteral("method") })),
        tool(QStringLiteral("qmlagent.log_enable"),
             QStringLiteral("Enable QmlAgent log events on this persistent connection."),
             schema({ { QStringLiteral("replayBuffered"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } } })),
        tool(QStringLiteral("qmlagent.log_get_entries"),
             QStringLiteral("Return buffered QML/log entries. Log.enable is not required first. Use sinceTimestamp from the previous result's nextSinceTimestamp to fetch only new entries in long agent loops."),
             schema({
                 { QStringLiteral("level"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                 { QStringLiteral("sinceTimestamp"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("number") } } },
                 { QStringLiteral("maxEntries"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } },
             })),
        tool(QStringLiteral("qmlagent.render_capture_screenshot"),
             QStringLiteral("Fallback visual evidence: capture a QQuickWindow screenshot. Do not use this as the primary oracle. First use qmlagent.ui_query/ui_get_tree, diagnostics, logs, source, and input/workflow tools; request screenshot data only when structured evidence is insufficient or the task is explicitly visual. By default this returns metadata without PNG data. Set includeData:true only when image bytes are needed, and prefer scale/region to avoid large base64 payloads."),
             schema({
                 { QStringLiteral("windowId"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("integer") },
                     { QStringLiteral("description"), QStringLiteral("1-based QQuickWindow id. Defaults to the first QQuickWindow.") },
                 } },
                 { QStringLiteral("includeData"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("boolean") },
                     { QStringLiteral("description"), QStringLiteral("When true, include base64 PNG data. Defaults to false to preserve agent context budget.") },
                 } },
                 { QStringLiteral("scale"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("number") },
                     { QStringLiteral("description"), QStringLiteral("Optional downscale factor in (0, 1]. Use values such as 0.5 or 0.25 for token-safe fallback visual evidence.") },
                 } },
                 { QStringLiteral("region"), QJsonObject{
                     { QStringLiteral("type"), QStringLiteral("object") },
                     { QStringLiteral("description"), QStringLiteral("Optional window-local logical-pixel crop: {x,y,width,height}. Useful for fallback visual evidence around one UI area.") },
                 } },
             })),
        tool(QStringLiteral("qmlagent.source_resolve"),
             QStringLiteral("Resolve one node back to source with method/confidence/limitations."),
             schema(withNodeRef({}))),
    };
}

QJsonObject jsonResponse(const QJsonValue &id, const QJsonValue &result)
{
    return {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), id },
        { QStringLiteral("result"), result },
    };
}

QJsonObject jsonError(const QJsonValue &id, int code, const QString &message,
                      const QJsonValue &data)
{
    QJsonObject error{
        { QStringLiteral("code"), code },
        { QStringLiteral("message"), message },
    };
    if (!data.isUndefined())
        error.insert(QStringLiteral("data"), data);
    return {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), id },
        { QStringLiteral("error"), error },
    };
}

QJsonObject toolResult(const QJsonValue &payload, bool isError)
{
    QByteArray payloadJson;
    if (payload.isArray())
        payloadJson = QJsonDocument(payload.toArray()).toJson(QJsonDocument::Compact);
    else if (payload.isObject())
        payloadJson = QJsonDocument(payload.toObject()).toJson(QJsonDocument::Compact);
    else
        payloadJson = QJsonDocument(QJsonArray{ payload }).toJson(QJsonDocument::Compact);

    QJsonObject result{
        { QStringLiteral("content"), QJsonArray{ QJsonObject{
            { QStringLiteral("type"), QStringLiteral("text") },
            { QStringLiteral("text"), QString::fromUtf8(payloadJson) },
        } } },
    };
    if (payload.isObject() || payload.isArray())
        result.insert(QStringLiteral("structuredContent"), payload);
    if (isError)
        result.insert(QStringLiteral("isError"), true);
    return result;
}

QJsonObject toolErrorResult(const QString &message)
{
    return toolResult(QJsonObject{ { QStringLiteral("error"), message } }, true);
}

} // namespace QmlAgentMcp
