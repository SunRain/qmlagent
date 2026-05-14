# QmlAgent

QmlAgent is an agent-first Qt Quick runtime feedback service.

It installs a `QmlAgent` qmltooling plugin plus three small tools:

```txt
qmlagent-launcher   start a debug-enabled app or preview QML file
qmlagent-mcp        native MCP stdio server for coding agents
qmlagentctl         shell fallback and smoke/debug commands
```

Use it to inspect, diagnose, interact with, and verify a running Qt Quick UI
through structured runtime evidence. Screenshots exist only as fallback visual
evidence after `UI.*`, `Diagnostics.*`, `Source.*`, `Input.*`, `Workflow.*`,
and `Log.*` evidence is insufficient.

## What It Does

QmlAgent lets a coding agent work with a running Qt Quick app without relying
on human visual inspection:

- See the live UI tree.
- Click, type, drag, wheel, and touch UI elements through Qt input events.
- Read QML warnings and runtime logs.
- Detect layout, visibility, actionability, and binding-related failures.
- Map runtime failures back to likely QML source lines.
- Verify fixes through structured runtime evidence instead of asking a person
  to look at the screen.

## Requirements

- Qt 6.11.0 or compatible Qt 6 build with private headers available.
- The target app must be built with QML debugging enabled, usually
  `QT_QML_DEBUG` in the target compile definitions.
- Use the correct Qt installation path for your machine.

Given a Qt bin directory:

```sh
QT_BIN=/path/to/Qt/6.11.0/<platform>/bin
```

Useful discovery commands:

```sh
"$QT_BIN/qtpaths6" --install-prefix
"$QT_BIN/qtpaths6" --plugin-dir
"$QT_BIN/qt-cmake" --version
```

## Build And Install

Configure with the Qt installation's `qt-cmake`:

```sh
"$QT_BIN/qt-cmake" -S . -B build
cmake --build build -j
cmake --install build
```

Install puts:

```txt
<Qt prefix>/plugins/qmltooling/libqmldbg_agent.dylib   # macOS
<Qt prefix>/bin/qmlagent-launcher
<Qt prefix>/bin/qmlagent-mcp
<Qt prefix>/bin/qmlagentctl
```

On other platforms the plugin filename extension follows Qt platform
conventions.

## Launch A Target

Preferred app flow:

```sh
"$QT_BIN/qmlagent-launcher" app ./myapp
```

Preferred from-scratch QML / live-preview flow:

```sh
"$QT_BIN/qmlagent-launcher" preview Main.qml
# after editing Main.qml or imported local QML/JS used by the preview root:
"$QT_BIN/qmlagentctl" reload-preview
```

Only `preview Main.qml` supports `reload-preview`. `app ./myapp` is a real
application session; rebuild/relaunch unless the app itself owns a reload
boundary.

Manual target launch is still supported:

```sh
./myapp -qmljsdebugger=port:3768,host:127.0.0.1,services:QmlAgent,block
```

If the app prints `Debugging has not been enabled`, rebuild it with
`QT_QML_DEBUG`; that is a target build failure, not a QmlAgent attach failure.

## Register MCP

Register the persistent MCP server after installing. Do not include target
ports, sockets, or app launch commands in MCP registration.

Codex:

```sh
codex mcp add qmlagent -- "$QT_BIN/qmlagent-mcp" --timeout 5000
codex mcp list
```

Claude Code, from the project directory that should own the local registration:

```sh
claude mcp add qmlagent -s local -- "$QT_BIN/qmlagent-mcp" --timeout 5000
claude mcp get qmlagent
```

Other MCP clients:

```txt
command: /path/to/Qt/6.11.0/<platform>/bin/qmlagent-mcp
args: ["--timeout", "5000"]
```

Restart the agent session after registration so native tools are loaded.

Normal MCP flow:

```txt
1. Build the target app if needed.
2. Start it with qmlagent-launcher app ./myapp or qmlagent-launcher preview Main.qml.
3. Call qmlagent.target_status.
4. If launcherGateway.available is true, call target-backed tools directly.
5. Use qmlagent.connect_tcp/connect_local_socket only for manually launched
   targets or streamed subscriptions requiring direct persistent attach.
```

Multiple agent runtimes may register `qmlagent-mcp`. A launcher-owned session
routes request/response tools through the launcher gateway. Direct manual
attach remains single-client per target endpoint and reports ownership
diagnostics if another QmlAgent MCP process owns the endpoint.

## Shell Fallback

Use `qmlagentctl` when MCP is unavailable or when a shell command is simpler:

```sh
"$QT_BIN/qmlagentctl" status --format compact
"$QT_BIN/qmlagentctl" query 'id="saveButton"' --property text --format compact
"$QT_BIN/qmlagentctl" wait 'id="detailsPopup"' --state found --timeout 1000
"$QT_BIN/qmlagentctl" click 'id="saveButton"'
"$QT_BIN/qmlagentctl" replace-text 'id="urlField"' --text 'https://qt.io'
"$QT_BIN/qmlagentctl" binding 'id="saveButton"' --property y --format compact
"$QT_BIN/qmlagentctl" screenshot --out fallback.png --scale 0.5
"$QT_BIN/qmlagentctl" stop
```

Raw protocol escape hatch:

```sh
"$QT_BIN/qmlagentctl" call --help
"$QT_BIN/qmlagentctl" call UI.query --params '{"selector":"id=\"saveButton\""}'
"$QT_BIN/qmlagentctl" call Render.captureScreenshot --params '{"omitData":true,"scale":0.5}'
```

`qmlagentctl call --help` lists all raw protocol method names.

## Agent Workflow

Use stable selectors first:

```txt
id="saveButton"
id="delegateRow" index=0
objectName="legacy.row" index=4
id="tableCell" row=0 column=1
```

Avoid session-local `nodeId` unless QmlAgent reports that no stable selector is
available.

For repair/build loops:

```txt
inspect:    qmlagent.ui_query / qmlagent.ui_get_tree
diagnose:   qmlagent.diagnostics_analyze_node / diagnostics_analyze_tree
source:     qmlagent.source_resolve / diagnostics_analyze_binding
act:        qmlagent.input_click / input_drag / input_wheel / input_type_text
wait:       qmlagent.ui_wait_for or qmlagent.workflow_click_and_wait
verify:     UI/query/diagnostics/log evidence, not screenshots
```

Use workflow tools when the action and verification belong together:

```txt
qmlagent.workflow_click
qmlagent.workflow_click_and_wait
qmlagent.workflow_key
```

Workflow expectations support equality, numeric comparisons, and bounded string
operators such as:

```txt
text contains "google"
text startsWith "https://"
```

Use `Diagnostics.analyzeBinding` when geometry or state looks computed:

```sh
"$QT_BIN/qmlagentctl" binding 'id="drawer"' --property x --format compact
```

Runtime mutation tools exist only for setup/navigation and are disabled by
default. Enable them explicitly and verify final behavior through normal UI,
input, diagnostics, logs, or workflow tools.

## Visual Evidence

Screenshots are fallback evidence. Prefer structured evidence first.

MCP screenshot bytes are base64 and can be large. If image bytes are necessary,
use `scale` and/or `region`:

```txt
qmlagent.render_capture_screenshot(includeData=true, scale=0.5)
```

For shell workflows, prefer:

```sh
"$QT_BIN/qmlagentctl" screenshot --out fallback.png --scale 0.5
```

This writes decoded PNG bytes without printing base64 into the agent context.

## Skills

This repo ships a small agent skill template:

```txt
skills/qmlagent-runtime/SKILL.md
```

Install for Codex:

```sh
mkdir -p ~/.codex/skills
rm -rf ~/.codex/skills/qmlagent-runtime
cp -R skills/qmlagent-runtime ~/.codex/skills/
```

Install for Claude Code:

```sh
mkdir -p ~/.claude/skills
rm -rf ~/.claude/skills/qmlagent-runtime
cp -R skills/qmlagent-runtime ~/.claude/skills/
```

Restart the agent after installing a skill.

## Report Issues

Please report bugs, missing agent workflows, Qt version problems, and protocol
ergonomics issues with a GitHub issue or pull request. Include the Qt version,
platform, launch command, whether the target was built with `QT_QML_DEBUG`, and
the smallest QML/app snippet that reproduces the problem.

Copyright (C) 2026 Penk Chen <penkia@gmail.com>.
Licensed under the Apache License, Version 2.0.
