# C++ Milestone 40 Boundaries: MCP Remote HTTP Execution MVP

Milestone 40 promotes a narrow executable MCP remote HTTP slice while keeping prior M39 fail-closed guardrails for unsupported remote modes. This milestone is intentionally limited to POST-only JSON-RPC over HTTP and does not claim full Rust MCP remote parity.

## In Scope

1. Add an executable `TransportType::Http` MCP transport (`HttpJsonTransport`) that performs one JSON-RPC HTTP POST per `send(...)` call.
2. Gate HTTP execution behind `AVA_WITH_CPR`: default no-CPR builds fail closed with actionable runtime errors (`requires AVA_WITH_CPR=ON`).
3. Preserve safe remote-auth behavior by reading bearer tokens from `bearerTokenEnv` at request time, requiring `https://` whenever bearer/OAuth auth descriptors are configured, and never emitting token values in transport errors.
4. Reject remote URLs that include authority userinfo (`user:pass@host`) for both HTTP and SSE config declarations.
5. Enforce protocol-owned headers for this MVP (`Content-Type: application/json`, JSON `Accept`, and env-driven `Authorization` when configured) while rejecting inline credential-bearing header overrides (`Authorization`, `Proxy-Authorization`, `Cookie`).
6. Disable automatic HTTP redirect following for MCP JSON POST requests so redirect responses are surfaced as non-2xx status failures instead of being transparently followed.
7. Keep `TransportType::Sse` parsed but not executable, with explicit manager-level fail-closed reporting.
8. Add focused `ava_mcp_unit` coverage for CPR-gated HTTP manager behavior, HTTP transport no-pending/closed semantics, constructor guardrails, redirect handling, and SSE fail-closed leakage boundaries.

## Out of Scope

1. SSE streaming execution, GET event streams, or mixed HTTP/SSE session negotiation.
2. OAuth exchange/PKCE/device flows, token refresh, keychain ownership, or session/token persistence.
3. Remote retry/backoff, reconnect/resume, or long-lived remote session management.
4. JSON-RPC response batch handling (single-message response handling only in this MVP).
5. Binary blob extraction, custom TOML tool execution, plugin parity, and MCP UI workflows.
6. Live remote MCP server validation tests.

## Validation

```bash
just cpp-build cpp-debug --target ava_mcp_tests
just cpp-test cpp-debug -R ava_mcp_unit
git diff --check
```

## Residual Risk

M40 adds a useful but intentionally narrow remote transport execution path: POST-only, request/response JSON-RPC when CPR is available. Redirect following is explicitly disabled and constructor-level auth/header/url guardrails are fail-closed, but this milestone still does not remove broader remote MCP parity gaps around SSE streaming, OAuth lifecycle ownership, retry/resume behavior, batch responses, or UI/runtime extension breadth.
