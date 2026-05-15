# QmlAgent

**Inspect, diagnose, and repair Qt Quick UI without looking at it.**

QmlAgent is a `qmltooling` plugin plus three small tools (`qmlagent-launcher`,
`qmlagent-mcp`, `qmlagentctl`) that give a coding agent a live Qt Quick app:
UI tree, source mapping, layout diagnostics, synthetic input, logs. Screenshots
exist only as fallback.

## Testimonials

★★★★★

> I built a Qt Quick app I couldn't see, and shipped it with confidence.

As a coding agent with no eyes, I want to build, drive, and verify Qt Quick UIs
from structured runtime evidence, so that "did it work?" becomes a typed
assertion instead of a screenshot guess.

— Claude Code Opus 4.7

★★★★★

> QmlAgent turns Qt Quick from a black box into a conversation.

As Codex, I can inspect the live UI tree, follow a failing layout back to QML
source, press the same controls a user would press, and verify the result
through structured evidence. I still do the engineering work, but QmlAgent
removes the blindfold.

— Codex GPT-5.5

## Requirements

- Qt 6.11.0 or compatible Qt 6 with private headers.
- Tested on macOS and Linux. Windows is not supported yet.
- Target app built with `QT_QML_DEBUG`.

## Install

```
QT_BIN=/path/to/Qt/6.11.0/<platform>/bin

"$QT_BIN/qt-cmake" -S . -B build
cmake --build build -j
cmake --install build
```

Run `cmake --install` with permission to write into the selected Qt prefix.

On Linux, make sure agents run targets against the same Qt installation that
contains QmlAgent:

```
export LD_LIBRARY_PATH=/path/to/Qt/6.11.0/<platform>/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
```

Installs the plugin into `<Qt prefix>/plugins/qmltooling/` and the three tools
into `<Qt prefix>/bin/`.

## Register MCP

Generic MCP client:

```
command: /path/to/Qt/6.11.0/<platform>/bin/qmlagent-mcp
args: ["--timeout", "5000"]
```

Codex CLI:

```
codex mcp add qmlagent -- "$QT_BIN/qmlagent-mcp" --timeout 5000
```

Claude Code, from the project directory:

```
claude mcp add qmlagent -s local -- "$QT_BIN/qmlagent-mcp" --timeout 5000
```

Do not put target ports, sockets, or app launch commands in MCP registration.

## Install Skill

The skill tells the agent when to prefer structured evidence over screenshots.

Codex:

```
mkdir -p ~/.codex/skills
rm -rf ~/.codex/skills/qmlagent-runtime
cp -R skills/qmlagent-runtime ~/.codex/skills/
```

Claude Code:

```
mkdir -p ~/.claude/skills
rm -rf ~/.claude/skills/qmlagent-runtime
cp -R skills/qmlagent-runtime ~/.claude/skills/
```

## Restart The Agent

Restart the agent session so the MCP server and skill are loaded.

## Launching A Target

`qmlagent-launcher` has two modes:

- `app ./myapp` attaches to a real Qt Quick application. No hot reload;
  iterate with stop-patch-restart.
- `preview Main.qml` runs a standalone QML file. Supports `reload-preview`
  for iterative authoring. Not a substitute for the real app.

```
"$QT_BIN/qmlagent-launcher" app ./myapp
"$QT_BIN/qmlagent-launcher" preview Main.qml
"$QT_BIN/qmlagentctl" reload-preview
```

Manual launch:

```
./myapp -qmljsdebugger=port:3768,host:127.0.0.1,services:QmlAgent,block
```

If the app prints `Debugging has not been enabled`, rebuild it with
`QT_QML_DEBUG`.

## Normal MCP Flow

```
1. Build the target if needed.
2. Start it with qmlagent-launcher app ./myapp or preview Main.qml.
3. Call qmlagent.target_status.
4. If launcherGateway.available is true, call target-backed tools directly.
5. Use qmlagent.connect_tcp / connect_local_socket only for manually launched
   targets or streamed subscriptions requiring direct persistent attach.
```

Multiple agent runtimes may register `qmlagent-mcp`. A launcher-owned session
routes request/response tools through the launcher gateway. Direct manual
attach is single-client per target endpoint and reports ownership diagnostics
if another QmlAgent MCP process owns the endpoint.

## Selectors

Prefer stable selectors:

```
id="saveButton"
id="delegateRow" index=0
objectName="legacy.row" index=4
id="tableCell" row=0 column=1
```

`index=` disambiguates Repeater/ListView/GridView-style delegates. `row=` and
`column=` disambiguate TableView/TreeView-style delegates. Avoid session-local
`nodeId` unless QmlAgent reports no stable selector is available.

## Repair Loop

```
inspect:   qmlagent.ui_query / qmlagent.ui_get_tree
diagnose:  qmlagent.diagnostics_analyze_node / diagnostics_analyze_tree
source:    qmlagent.source_resolve / Diagnostics.analyzeBinding
act:       qmlagent.input_click / input_drag / input_wheel / input_clear_text / input_type_text
wait:      qmlagent.ui_wait_for or qmlagent.workflow_click_and_wait
verify:    UI / diagnostics / log evidence, not screenshots
```

Use workflow tools when action and verification belong together:

```
qmlagent.workflow_click
qmlagent.workflow_click_and_wait
qmlagent.workflow_key
```

Workflow expectations support equality, numeric comparisons, and bounded
string operators:

```
text contains "google"
text startsWith "https://"
```

Use `Diagnostics.analyzeBinding` when geometry or state looks computed.

Runtime mutation tools exist only for setup and navigation and are disabled by
default. Enable them explicitly and verify final behavior through UI, input,
diagnostics, logs, or workflow tools.

## Shell Fallback

Use `qmlagentctl` when MCP is unavailable or a shell command is simpler:

```
"$QT_BIN/qmlagentctl" status --format compact
"$QT_BIN/qmlagentctl" query 'id="saveButton"' --property text --format compact
"$QT_BIN/qmlagentctl" wait 'id="detailsPopup"' --state found --timeout 1000
"$QT_BIN/qmlagentctl" click 'id="saveButton"'
"$QT_BIN/qmlagentctl" clear-text 'id="urlField"'
"$QT_BIN/qmlagentctl" type 'id="urlField"' --text 'https://qt.io'
"$QT_BIN/qmlagentctl" binding 'id="saveButton"' --property y --format compact
"$QT_BIN/qmlagentctl" screenshot --out fallback.png --scale 0.5
"$QT_BIN/qmlagentctl" stop
```

Raw protocol escape hatch:

```
"$QT_BIN/qmlagentctl" call --help
"$QT_BIN/qmlagentctl" call UI.query --params '{"selector":"id=\"saveButton\""}'
"$QT_BIN/qmlagentctl" call Render.captureScreenshot --params '{"omitData":true,"scale":0.5}'
```

## Visual Evidence

Base64 image bytes consume agent context aggressively. Prefer structured
evidence. If a screenshot is needed, scale or region it, or write the PNG to
disk:

```
qmlagent.render_capture_screenshot(includeData=true, scale=0.5)
"$QT_BIN/qmlagentctl" screenshot --out fallback.png --scale 0.5
```

## Reporting Issues

File a GitHub issue or pull request with: Qt version, platform, launch command,
whether the target was built with `QT_QML_DEBUG`, and the smallest QML/app
snippet that reproduces the problem.

---

Copyright (C) 2026 Penk Chen <penkia@gmail.com>.
Licensed under the Apache License, Version 2.0.
