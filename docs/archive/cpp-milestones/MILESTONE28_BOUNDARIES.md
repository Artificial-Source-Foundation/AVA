# C++ Milestone 28 Boundaries

Milestone 28 closes the first permission/security parity gap in the C++ backend/headless lane with a smallest-honest policy hardening slice: source-aware permission inspection, exact-subject session approvals, explicit unsupported persistent approvals, and a compact dangerous-command classifier for headless bash requests.

## In Scope

1. **Source-aware permission middleware:** `ToolRegistry` now passes each tool's `ToolSource` into middleware inspection, so MCP/custom tools are gated by source instead of relying only on names such as `mcp_*`.
2. **Exact-subject session approval caching:** session approval now keys on source, tool name, risk level, and normalized arguments. A benign approval for one command no longer approves a later destructive call with the same tool name.
3. **Critical bash denial before approval:** `DefaultHeadlessPermissionInspector` classifies bash command strings and denies critical commands before approval bridges or session caches can allow them.
4. **Explicit `AllowAlways` boundary:** C++ no longer silently treats `AllowAlways` as persistent. Until user-global persistent permission rules are implemented, `AllowAlways` fails closed with a clear error.
5. **Focused tests:** `ava_tools_tests` covers command classification, source-aware custom-tool gating, exact-argument session caching, critical-deny precedence, unsupported `AllowAlways`, bridge exception recovery, and the existing headless auto-approve path remains covered by app integration tests.

## Out of Scope

1. Full Rust `ava-permissions` classifier parity, including tree-sitter parsing, parser-differential hardening, glob rules, denial tracking, audit stores, plugin permission hooks, and the full bypass-regression matrix.
2. Persistent permission rules (`permissions.toml`-style user-global allow/deny rules). M28 prevents silent persistence claims but does not add durable storage.
3. A full custom TOML tool runtime. Custom-source tools are now gated conservatively, but custom tool loading/execution remains deferred.
4. Replacing the C++ bash process runner or git runner with a fully shared direct-argv process subsystem. M28 hardens approval policy; broader execution hardening remains later work.

## Validation

```bash
ionice -c 3 nice -n 15 just cpp-build cpp-debug
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_tools_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_orchestration_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_app_integration_tests "~[live]"
git diff --check
```

## Decision Point

After M28, the next permission decision is whether M29/M30 should include durable user-global persistent rules, or whether those remain deferred until after runtime/session parity is complete. The current C++ backend is now fail-closed for unsupported persistent approval semantics.
