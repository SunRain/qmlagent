# Diagnostic Fixtures

Each subdirectory is a small Qt Quick app loaded by the generic
`qmlagent_diagnostic_fixture` test host.

Fixture shape:

```txt
<case>/
  Main.qml
  expected.json
```

`expected.json` describes the QmlAgent command and the diagnostic evidence the
fixture must produce. Keep fixtures narrow: one primary runtime symptom per
case, explicit selectors where practical, and source-location confidence checks
for any issue that claims source evidence.

Add high-confidence plugin-boundary cases first. For popup, overlay, delegate,
translation, and model semantics, encode expected limitations instead of
pretending the service can prove more than the runtime evidence supports.
