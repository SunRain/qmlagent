---
name: qmlagent-runtime
description: Use when building, repairing, creating, or verifying Qt Quick/QML apps with QmlAgent. Covers the agent-first qmlagent-launcher app/preview workflow, native qmlagent MCP tools, qmlagentctl fallback commands, QT_QML_DEBUG requirements, preview reload, binding diagnostics, input/workflow verification, and structured-first evidence discipline.
---

# QmlAgent Runtime Workflow

Use QmlAgent as the runtime evidence loop for Qt Quick work. Do not treat this
skill as the protocol reference; once native MCP tools are visible, follow
their descriptions for exact parameters.

## Required Setup

1. Find the Qt installation `bin/` that contains `qt-cmake`,
   `qmlagent-launcher`, `qmlagentctl`, and `qmlagent-mcp`.
2. Build the target through CMake with `QT_QML_DEBUG`.
3. Prefer `qmlagent-launcher` for all agent-owned sessions.
4. After launch, call `qmlagent.target_status` first when native MCP tools are
   available.

If the app prints `Debugging has not been enabled`, fix the build. It means the
target was not built with QML debugging enabled; QmlAgent cannot attach.

## Launch Modes

Use the real app path when validating an application executable:

```sh
"$QT_BIN/qmlagent-launcher" app ./myapp -- --app-arg
```

Use preview mode only for local QML-root iteration:

```sh
"$QT_BIN/qmlagent-launcher" preview Main.qml
```

Preview sessions can reload. After editing the root QML file or a file it
loads, request reload through native MCP:

```txt
qmlagent.preview_reload
```

or through shell fallback:

```sh
"$QT_BIN/qmlagentctl" reload-preview
```

`qmlagent-launcher app <executable>` does not support preview reload. Rebuild
or relaunch app sessions unless the app itself owns a reload boundary.

Do not pass app binaries to `preview`, and do not expect `app` sessions to
reload QML. That split is intentional and user-visible.

## MCP Workflow

Do not start `qmlagent-mcp` manually inside the task. It should be registered
with the agent runtime before the session starts.

After `qmlagent-launcher` starts exactly one live session, request/response MCP
tools route through it automatically. Do not call `qmlagent.connect_tcp` or
`qmlagent.connect_local_socket` for launcher-owned request/response work.
Direct attach is only for manually launched targets or streamed subscriptions
that require a persistent attached client.

Normal order:

```txt
qmlagent.target_status
launch or confirm one qmlagent-launcher session
qmlagent.ui_query / qmlagent.ui_get_tree
qmlagent.diagnostics_* / qmlagent.source_resolve / qmlagent.log_get_entries
qmlagent.input_* / qmlagent.workflow_*
patch source
rebuild/relaunch app session, or preview_reload for preview session
verify with runtime evidence
```

Use compact or summary output by default. Ask for fuller evidence only when the
summary says fields were omitted or when patching needs deeper source/runtime
facts.

## qmlagentctl Fallback

Use `qmlagentctl` for launcher/session control or when native MCP tools are not
available:

```sh
"$QT_BIN/qmlagentctl" sessions --format compact
"$QT_BIN/qmlagentctl" status --format compact
"$QT_BIN/qmlagentctl" query 'id="saveButton"' --property text --format compact
"$QT_BIN/qmlagentctl" wait 'id="detailsPopup"' --state found --timeout 1000
"$QT_BIN/qmlagentctl" click 'id="saveButton"'
"$QT_BIN/qmlagentctl" clear-text 'id="urlField"'
"$QT_BIN/qmlagentctl" type 'id="urlField"' --text 'go.dev'
"$QT_BIN/qmlagentctl" binding 'id="saveButton"' --property y --format compact
"$QT_BIN/qmlagentctl" call --help
"$QT_BIN/qmlagentctl" reload-preview
"$QT_BIN/qmlagentctl" stop
```

`qmlagentctl screenshot` exists, but it is fallback visual evidence. Use
structural UI, diagnostics, source, logs, and input/workflow evidence first.
Only request image bytes when visual evidence is explicitly needed. Prefer
`qmlagentctl screenshot --out shot.png --scale 0.5` for shell fallback so
base64 does not enter the agent context. For MCP screenshot bytes, pass
`scale` and/or `region` with `includeData:true`.

## Evidence Discipline

- Prefer stable QML `id` selectors over session-local `nodeId`.
- For repeated delegates, try `id="delegateId" index=0` before adding
  automation-only `objectName`.
- When geometry/state looks computed, call
  `qmlagent.diagnostics_analyze_binding` for the property. Treat classic
  `QQmlBinding` dependency values as runtime evidence. Treat
  `candidateIdentifiers` as low-confidence follow-up hints that must be
  verified through `UI.query`, diagnostics, or source evidence before patching.
- Inspect `bindingProvenance` attached to layout/visibility diagnostics before
  making a separate binding call; it may already name the computed property,
  expression, source location, and dependency limitations.
- Use `UI.waitFor` or workflow tools for transitions, popups, loaders,
  animations, and async post-action state. Do not use sleeps.
- To enter new text into an occupied field, use native `qmlagent.input_clear_text` followed by
  `qmlagent.input_type_text`, or shell `qmlagentctl clear-text` followed by
  `qmlagentctl type`, then verify with `UI.query` or `UI.waitFor`.
- Use input/workflow results plus `UI.query`, diagnostics, logs, or source
  evidence as proof. Runtime mutation is setup-only.
- If QML fails to load, inspect logs/status first. A dead process cannot answer
  `UI.*` queries.
- Keep one active workflow owner per target session; multiple agents can race
  semantically even when launcher routing works.
- Write `REPORT.md` when the task asks for feedback, including commands/tools
  used, runtime evidence, what QmlAgent helped with, and what was awkward.
