# C++ Milestone 36 Boundaries: MCP Runtime Breadth

Milestone 36 expands the existing C++ MCP runtime without changing the transport architecture. It adds protocol breadth over the already-tested synchronous transport seam: resources, prompts, and bounded result projection.

## In Scope

1. `McpClient` support for `resources/list` and `resources/read`.
2. `McpClient` support for `prompts/list` and `prompts/get`.
3. `McpManager` delegation helpers for resources and prompts on connected servers.
4. Bounded MCP text result strings for tool/resource/prompt responses, capped at 100,000 UTF-8 bytes with an explicit truncation marker; oversized MCP binary `blob` payloads and image/audio `data` payload strings fail closed until binary artifact handling is in scope.
5. Focused `ava_mcp_tests` coverage using the existing in-memory/scripted transport seam.

## Out of Scope

1. HTTP/SSE MCP transport.
2. OAuth/PKCE, token refresh, keychain integration, or browser/device auth flows.
3. MCP resource or prompt UX wiring in the TUI, web, or desktop surfaces.
4. Binary blob extraction, fallback artifact files, and MIME-aware file persistence.
5. Async/background MCP scheduling or notification debounce parity.
6. Custom TOML tool execution and plugin runtime parity.

## Validation

```bash
ionice -c 3 nice -n 15 just cpp-build cpp-debug --target ava_mcp_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_mcp_tests "[ava_mcp]"
git diff --check
```

## Residual Risk

M36 intentionally does not claim remote MCP parity. HTTP/SSE and OAuth remain separate future slices because they introduce network dependency, auth storage, retry, and non-interactive browser-flow concerns that should be reviewed independently.
