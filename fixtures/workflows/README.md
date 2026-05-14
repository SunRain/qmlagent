# Workflow Fixtures

Workflow fixtures exercise reference-client report composition across multiple
service commands. They are not service-core protocol fixtures.

- `input-state-change/`: click is delivered, expected state changes,
  verification passes, and no workflow issue is emitted.
- `input-no-effect/`: click is delivered, expected state is unchanged,
  verification fails, and the client emits one `state.no_change_after_action`
  issue.
- `input-refused/`: click is refused by input preflight, the input diagnostic is
  surfaced as the workflow issue, and no state no-change issue is emitted.
