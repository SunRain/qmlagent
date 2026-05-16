# MCP Transcript Fixtures

These fixtures are review aids for the `qmlagent-mcp` surface. They are not
golden runtime captures: node ids, source paths, event ordering, and exact tool
schemas can vary as the protocol grows.

Use them to quickly inspect the intended exchange shape:

- MCP frontend initializes the persistent server.
- Frontend lists tools and sees selector-first workflow tools.
- Frontend runs a grouped workflow through one MCP tool call.
- Server returns one workflow report with target, before, input, events, after,
  verification, and issues.

The integration test `referenceClientMcpPersistentMode` verifies the live
version of this flow against the smoke app.
