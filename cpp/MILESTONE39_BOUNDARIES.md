# C++ Milestone 39 Boundaries: MCP Remote Transport Guardrails

Milestone 39 starts the MCP HTTP/SSE/OAuth breadth lane with config and runtime guardrails only. It lets the C++ runtime understand remote MCP server declarations without silently accepting inline secrets or pretending remote transport execution is complete.

## In Scope

1. Parse MCP `http` and `sse` transport configs alongside existing `stdio` servers.
2. Validate remote MCP URLs and request timeouts with deterministic errors.
3. Support safe remote-auth metadata: `bearerTokenEnv` plus OAuth issuer/client/scopes descriptors.
4. Reject inline credential-bearing remote headers and inline OAuth token/client-secret fields.
5. Fail closed from `McpManager` for parsed remote transports until an executable CPR-backed transport is promoted.
6. Focused `ava_mcp_unit` coverage for remote config parsing, unsafe auth rejection, and no-secret runtime failure reports.

## Out of Scope

1. Executable HTTP or SSE MCP network transport.
2. OAuth/PKCE browser flows, device-code flows, token exchange, token refresh, or keychain persistence.
3. Remote MCP retry/backoff, reconnect, streaming notification debounce, or list-change invalidation.
4. Binary blob extraction, custom TOML tool execution, plugin runtime parity, or MCP UI surfaces.
5. Live remote MCP server validation.

## Validation

```bash
ionice -c 3 nice -n 15 just cpp-build cpp-debug --target ava_mcp_tests
ionice -c 3 nice -n 15 just cpp-test cpp-debug -R ava_mcp_unit
git diff --check
```

## Residual Risk

M39 intentionally avoids claiming remote MCP execution parity. The value is safe recognition and isolation: C++ can now load remote MCP config shape, reject obvious secret leakage, and report unsupported runtime execution cleanly. A later milestone must add the actual CPR-backed HTTP/SSE transport and OAuth lifecycle before removing this fail-closed boundary.
