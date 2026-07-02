# Contributing to QmlAgent

## Build

QmlAgent builds against Qt 6.11 (or a compatible Qt 6) with private headers
(`QmlPrivate`, `QuickPrivate`, `QmlDebugPrivate`, `QuickTemplates2Private`).

```
QT_BIN=/path/to/Qt/6.11.0/<platform>/bin
"$QT_BIN/qt-cmake" -S . -B build
cmake --build build -j
```

`cmake --install build` installs the plugin into
`<Qt prefix>/plugins/qmltooling/` and the tools into `<Qt prefix>/bin/`. If
the Qt prefix is not writable, override the destinations with
`-DQMLAGENT_PLUGIN_INSTALL_DIR=...` and `-DQMLAGENT_TOOL_INSTALL_DIR=...`
and point `QT_PLUGIN_PATH` at the plugin dir (see README).

## Tests

```
ctest --test-dir build --output-on-failure
```

Unit tests live in `tests/auto/qmlagent/`. The integration suite
(`qmlagent_tcp_smoke`, `tests/manual/qmlagent/tst_qmlagentintegration.cpp`)
launches real smoke apps through the actual launcher/ctl/MCP binaries. Its
environment is set by CMake: it runs offscreen (`QT_QPA_PLATFORM=offscreen`)
with `QT_QUICK_CONTROLS_STYLE=Basic` for deterministic, cross-platform
behavior, and shortens the GUI dispatch grace window with
`QMLAGENT_GUI_DISPATCH_GRACE_MS=1000` so the timeout/busy paths are testable.

## Code Conventions

- Qt style: 4-space indent, `QStringLiteral` for every literal that becomes a
  `QString`, `QLatin1String` for comparisons.
- Prefer file-static free functions over new classes; keep classes to what
  needs state or signals.
- Every result an agent consumes carries evidence fields: `ok`, structured
  `diagnostics` (id, severity, confidence, message, hints), and honest labels
  such as `verificationRole`, `semanticProof`, and `limitations`. New code
  must say what it proves and what it does not.
- Commits: one-line imperative subject, no trailing period
  (e.g. "Preserve window geometry across preview reload").

## Adding a Protocol Method

1. Implement it in the owning module in
   `src/plugins/qmltooling/qmldbg_agent/`, then register it in
   `agentMethods()` and `dispatch()` in `qqmlagentservice.cpp`. Anything that
   touches Qt Quick state must go through `runOnGuiThreadBlocking`.
2. Mirror it in `qmlAgentProtocolMethods()` in `tools/qmlagent/main.cpp`
   (static fallback for `qmlagentctl methods`).
3. If agent-facing, add an MCP tool in `qmlagentmcpprotocol.cpp` plus its
   handler in `main.cpp`, a `qmlagentctl` subcommand if a shell form makes
   sense, and update `skills/qmlagent-runtime/SKILL.md`.
4. Add coverage to `tests/manual/qmlagent/tst_qmlagentintegration.cpp`
   exercising the real transport, plus unit tests where logic allows.

## Reporting Issues

File a GitHub issue or pull request with: Qt version, platform, launch
command, whether the target was built with `QT_QML_DEBUG`, and the smallest
QML/app snippet that reproduces the problem.
