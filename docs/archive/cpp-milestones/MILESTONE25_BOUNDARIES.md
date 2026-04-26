# C++ Milestone 25 Boundaries

M25 starts the C++ backend extension-runtime migration with a narrow MCP client foundation. It adds protocol/config/client seams that can be tested without live MCP servers while keeping full MCP tool registration, plugin runtime, custom TOML tools, browser tools, HTTP/SSE, OAuth, and TUI/desktop UX explicitly deferred.

## In Scope

1. Add an `ava_mcp` library with JSON-RPC 2.0 message helpers, an in-memory transport for deterministic tests, and a synchronous `McpClient` foundation.
2. Support the minimum MCP client protocol flow over a transport: `initialize`, `notifications/initialized`, paginated `tools/list`, and `tools/call`, pinned to MCP protocol version `2024-11-05` for this foundation slice.
3. Track basic MCP connection health for terminal transport/protocol correlation errors with reconnect-needed detection after three terminal errors; ordinary JSON-RPC server error responses are surfaced to callers without being treated as reconnect signals in this slice.
4. Parse MCP JSON config for stdio servers only (`name`, optional `enabled`, and nested `transport: { type: "stdio", command, args, env }`) and reject unsupported transports such as HTTP/OAuth with deterministic errors.
5. Add config path helpers for global/project MCP config and custom-tools directories so later milestones can wire discovery without changing path ownership.
6. Add focused `ava_mcp_tests` coverage for JSON-RPC encode/decode, in-memory transport, client protocol flow, server error propagation, health tracking, config parsing, unsupported transport rejection, and path helpers.

## Out of Scope

1. Spawning stdio MCP server processes and process lifecycle management.
2. MCP tool registration into `ToolRegistry` and runtime composition.
3. MCP HTTP/SSE transport, OAuth/PKCE/token refresh, prompts/resources, binary blob handling, progress logging, list-change notifications, and output fallback parity.
4. Plugin manager/runtime/hooks parity.
5. TOML custom tools and shell/script execution parity.
6. Browser/desktop automation tools and TUI/desktop MCP UX parity.

## Validation

```bash
ionice -c 3 nice -n 15 just cpp-configure cpp-debug
ionice -c 3 nice -n 15 just cpp-build cpp-debug
ionice -c 3 nice -n 15 just cpp-test cpp-debug -R ava_mcp_unit
git --no-pager diff --check -- cpp/include/ava/mcp cpp/src/mcp cpp/tests/unit/mcp_foundation.test.cpp cpp/src/CMakeLists.txt cpp/tests/CMakeLists.txt cpp/include/ava/config/paths.hpp cpp/src/config/paths.cpp cpp/MILESTONE25_BOUNDARIES.md CHANGELOG.md docs/project/backlog.md docs/architecture/cpp-backend-tui-migration-completion-gap-audit-m16.md docs/architecture/cpp-backend-tui-parity-contract-audit-post-m16.md
```

## Follow-Up Green-Fix Notes

- MCP client health now records terminal errors when replying to server-initiated requests fails, and malformed non-object `initialize.capabilities` responses are rejected as protocol errors instead of being treated as empty capabilities.
- `McpManager::shutdown()` now clears cached server reports along with connected clients/tools so post-shutdown queries do not return stale initialization state.
- Focused MCP tests now cover malformed capabilities, ping-reply send failures, shutdown report clearing/idempotency, and POSIX-only stdio transport fixtures are guarded on Windows.
- Milestone-labelled MCP diagnostics now refer to M25 for this foundation slice; later M27 runtime work may broaden stdio spawning/tool-registration scope without changing this slice's original decision point.

## Decision Point

M25 intentionally establishes only the MCP protocol/config foundation. Later milestones must decide whether to wire MCP tools into the default runtime, add stdio process spawning, or keep MCP/custom/plugin work behind opt-in extension seams.
