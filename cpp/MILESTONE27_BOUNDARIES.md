# C++ Milestone 27 Boundaries

Milestone 27 closes the next MCP runtime gap in the C++ backend/headless lane with a smallest-honest runtime MVP: stdio transport with non-hanging receive behavior, manager lifecycle/isolation, and namespaced MCP tool registration into the shared runtime composition path.

## In Scope

1. **Stdio MCP transport runtime seam:** add a real `StdioTransport` implementation in `ava_mcp` that spawns local stdio servers, frames JSON-RPC over NDJSON (`\n`-delimited lines), skips blank lines, and enforces a bounded receive timeout so the runtime cannot block forever waiting on a server response.
2. **MCP manager lifecycle:** add a scoped `McpManager` that initializes enabled stdio servers from parsed config, performs `initialize` + `tools/list`, aggregates server-tool ownership metadata, routes `tools/call` to the owning server, supports shutdown, and isolates per-server failures (one failing server does not prevent others from loading).
3. **MCP-to-ToolRegistry bridge:** add `McpBridgeTool` + `register_mcp_tools(...)` in `ava_tools` so MCP tools are exposed as namespaced tool names (`mcp_<server>_<tool>`), preserve original MCP tool names for `tools/call`, record MCP source ownership (`ToolSource::mcp(server)`), and respect built-in shadow protection already enforced by `ToolRegistry::register_tool_with_source(...)`.
4. **Runtime composition wiring:** wire MCP config loading + manager initialization + MCP tool registration into `compose_runtime(...)`, with explicit request seams for test-only MCP config/transport override and a runtime option to include global MCP config in product app paths.
5. **Focused tests:** extend C++ tests with (a) in-memory manager/bridge coverage, (b) a local mock stdio MCP server helper executable for NDJSON transport proof, and (c) an orchestration composition test proving MCP tool registration/execution through the shared runtime path without web/desktop dependencies.

## Out of Scope

1. MCP HTTP/SSE transport parity, OAuth/device/refresh auth flows, resources/prompts surfaces, binary-blob/output-fallback parity, and list-changed debounce behavior.
2. Broad custom TOML tool execution and shell-script custom-tool parity. This remains deferred to a later milestone (M28+) pending a safe execution model and guardrails.
3. Plugin runtime management, browser automation tool parity, and TUI/desktop MCP UX parity.
4. Full Rust MCP runtime breadth and long-tail extension orchestration behavior.

## Validation

```bash
ionice -c 3 nice -n 15 just cpp-build cpp-debug
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_mcp_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_tools_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_orchestration_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_app_tests
git --no-pager diff --check -- \
  cpp/include/ava/mcp/transport.hpp \
  cpp/src/mcp/transport.cpp \
  cpp/include/ava/mcp/config.hpp \
  cpp/src/mcp/config.cpp \
  cpp/include/ava/mcp/client.hpp \
  cpp/src/mcp/client.cpp \
  cpp/include/ava/mcp/manager.hpp \
  cpp/src/mcp/manager.cpp \
  cpp/include/ava/tools/mcp_bridge.hpp \
  cpp/src/tools/mcp_bridge.cpp \
  cpp/include/ava/orchestration/composition.hpp \
  cpp/src/orchestration/composition.cpp \
  cpp/src/orchestration/task.cpp \
  cpp/apps/ava/headless_run.cpp \
  cpp/apps/ava_tui/main.cpp \
  cpp/src/CMakeLists.txt \
  cpp/tests/CMakeLists.txt \
  cpp/tests/helpers/mock_mcp_stdio_server.cpp \
  cpp/tests/unit/mcp_foundation.test.cpp \
  cpp/tests/unit/tools_registry.test.cpp \
  cpp/tests/unit/orchestration_foundation.test.cpp \
  cpp/MILESTONE27_BOUNDARIES.md \
  CHANGELOG.md \
  docs/project/backlog.md \
  docs/architecture/cpp-rust-parity-gap-audit-post-m26.md
```

## Decision Point

After M27, the C++ runtime now has a practical stdio MCP MVP bridge. The next extension-runtime decision is whether to implement a safe custom-tool execution lane (TOML/script descriptors, policy boundaries, and sandboxing) before widening into HTTP/SSE/OAuth MCP breadth.
