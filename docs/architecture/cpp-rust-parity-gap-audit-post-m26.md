---
title: "C++/Rust Parity Gap Audit Post-M26"
description: "Non-web/non-desktop parity audit comparing the active Rust product surface with the scoped C++ migration tree after Milestone 26."
order: 18
updated: "2026-04-26"
---

# C++/Rust Parity Gap Audit Post-M26

This audit compares the active Rust implementation with the C++ migration tree after C++ Milestone 26. It intentionally excludes web and desktop UX. It also treats prior milestone boundaries as evidence: C++ has many production-shaped foundations, but a feature is not considered complete until it is wired through the runtime path and covered by tests.

## Scope

Included:

1. Core tools, permission middleware, and tool security.
2. Headless CLI/runtime, control-plane events, orchestration, queues, subagents, and sessions.
3. TUI interactive approval/question/plan flows.
4. LLM providers, credentials, model registry, retry/circuit-breaker behavior, and config.
5. MCP/custom-tool/plugin extension runtime foundations.

Excluded:

1. Web UI and `ava serve` parity.
2. Desktop/Tauri parity.
3. Cosmetic equivalence where C++ has an intentionally different UI toolkit.

## Current Completion Read

C++ is strongest in the narrow headless/runtime/TUI slices implemented through M33, with M34 providing final hardening and evidence documentation rather than new feature parity: request lifecycle ownership, adapter action routing, child-run cancellation, cooperative signal cancellation, scoped edit parity, Anthropic provider streaming, an MCP stdio runtime MVP bridge, budget/queue/recovery/checkpoint support, daily-use headless CLI polish, deterministic non-live headless integration evidence, and fail-closed TUI approval previews.

C++ is not yet a complete non-web/non-desktop Rust replacement. The largest remaining gaps are not single bugs; they are whole subsystems that Rust still owns or areas intentionally scoped smaller in C++: full permission policy breadth, MCP remote SSE/OAuth/session-retry + custom-tool/plugin breadth (with only a narrow CPR-gated HTTP POST slice promoted so far), long-tail providers, semantic compaction and JSONL audit logging, bookmarks/search, hard-kill recovery, and advanced TUI/editor affordances.

## M28-M40 Completion Update

The post-M26 milestone sequence closed several earlier audit gaps with scoped, tested slices rather than broad Rust parity claims:

1. **Permission/security (M28):** source-aware permission inspection, exact-subject session approval caching, and compact headless dangerous-command classification are wired; persistent rules and full Rust classifier breadth remain deferred.
2. **Long-run runtime (M29):** budget accumulation/warnings/exhaustion, follow-up/post-complete queue promotion, and structural `agent_visible=false` compaction are wired; semantic summarization and JSONL logging remain deferred.
3. **Session recovery/checkpoints (M30):** interrupted-tool recovery, checkpoint events, and headless checkpoint saves are wired; bookmarks/search and cross-runtime UUID migration remain deferred.
4. **Provider streaming (M31):** Anthropic CPR-gated SSE streaming is wired and parser-tested; broad long-tail providers and provider-specific retry/backoff parity remain deferred.
5. **CLI/config/tool polish (M32):** `--cwd`, `--agent`, `--trust`, selected environment defaults, builtin-agent max-turn defaults, and core-tool metadata/output polish are wired; the scoped parity follow-up now includes C++ `todo_read`, `todo_write`, and `tool_search` runtime registration with focused tests, while `web_fetch`/`web_search` remain deferred from the default surface pending a safe native HTTP transport lane. YAML config, custom TOML tools, browser automation tools, and subcommands remain deferred.
6. **Headless integration proof (M33):** deterministic non-live app-boundary coverage now exercises workspace/trust, resume/recovery, queue turns, budget-warning NDJSON, and event ordering through `run_headless_blocking`.
7. **Hardening/evidence (M34):** production-code shape review, small hardening fixes, focused local evidence, and residual-risk documentation close the scoped backend/headless lane without promoting web/desktop/full Rust parity.
8. **Permission-classifier hardening (M35):** C++ adds a small Rust-parity security slice for bash classification (critical-path `rm -rf`, semantic `find ... -delete`/`-exec rm -rf` deletion patterns, and parser-differential heuristics for IFS/brace-expansion/ANSI-C quoting/unicode whitespace) with focused tools coverage, while full parser/policy parity remains deferred.
9. **MCP runtime breadth (M36):** C++ adds resources/prompts client and manager delegation plus bounded MCP result projection over the existing synchronous transport seam. HTTP/SSE, OAuth, binary blobs, custom TOML tools, async notification debounce, and UI wiring remain deferred.
10. **Provider breadth (M37):** C++ promotes provider factory wiring for OpenAI-compatible `openrouter`, `inception`, and `zai`, plus Anthropic-compatible `alibaba`, `kimi`, and `minimax`; native Gemini, Copilot OAuth, Ollama local API behavior, Responses API/ChatGPT OAuth, plugin header hooks, and live-provider validation remain deferred.
11. **Config state primitives (M38):** C++ adds routing profile structs/JSON normalization, project-local `.ava/state.json` persistence for recent/mode model state, OAuth credential updates that preserve static fields, Rust-compatible key redaction, and an explicit native-keychain unavailable helper. Native OS keychain, encrypted fallback/migration, OAuth refresh flows, full YAML loading, and runtime routing-profile selection remain deferred.
12. **MCP remote-transport guardrails (M39):** C++ now parses `http`/`sse` MCP server config with URL/timeout validation, safe `bearerTokenEnv` and OAuth descriptor metadata, inline-secret rejection, and fail-closed manager reporting.
13. **MCP remote HTTP execution MVP (M40):** C++ now executes `TransportType::Http` as POST-only JSON-RPC when CPR is enabled, uses request-time `bearerTokenEnv` auth lookup, enforces protocol-owned JSON headers, disables automatic redirect following, requires `https://` whenever bearer/OAuth auth descriptors are configured, rejects URL authority userinfo (`user:pass@host`) in remote declarations, blocks inline credential-bearing remote headers, and still fails closed in no-CPR builds. SSE execution, OAuth exchange/refresh/keychain flows, retry/resume/session semantics, response batches, and UI wiring remain deferred.

## P0 Completion Blockers

1. Full command-risk classification is still Rust-only.
   - Rust reference: `crates/ava-permissions/src/classifier/`, `crates/ava-permissions/src/inspector.rs`, `crates/ava-tools/src/permission_middleware.rs`.
   - C++ current surface: `cpp/src/tools/command_classifier.cpp`, `cpp/src/tools/permission_middleware.cpp`.
   - Impact: C++ now has a fail-closed permission seam, source-aware approvals, expanded critical bash-pattern detection, and a narrow parser-differential heuristic layer. It still lacks Rust's tree-sitter parsing, persistent permission rules, path/glob policy rules, warning-tag surfaces, audit-store persistence, and plugin permission hooks.

2. MCP/custom-tool runtime breadth remains incomplete in C++.
   - Rust reference: `crates/ava-mcp/src/{client,transport,manager,config,oauth}.rs`, `crates/ava-tools/src/mcp_bridge.rs`, `crates/ava-tools/src/core/custom_tool.rs`.
   - C++ current surface: `cpp/src/mcp/`, `cpp/include/ava/mcp/`, `cpp/src/tools/mcp_bridge.cpp`, `cpp/src/orchestration/composition.cpp`, `cpp/MILESTONE27_BOUNDARIES.md`, `cpp/MILESTONE36_BOUNDARIES.md`, `cpp/MILESTONE39_BOUNDARIES.md`.
   - Impact: C++ now has stdio process spawning, receive timeouts, manager lifecycle/isolation, namespaced runtime registration, headless/runtime MCP tool execution, resources/prompts protocol methods, bounded result strings, guarded remote HTTP/SSE config parsing, and a CPR-gated POST-only HTTP JSON-RPC execution path. It still lacks SSE execution parity, OAuth exchange/refresh/keychain flows, robust remote retry/resume/session behavior, response-batch handling, binary blob handling, custom TOML tool execution, async notification debounce, UI wiring, and broader extension-runtime parity.

3. Real provider breadth and streaming architecture remain incomplete.
   - Rust reference: `crates/ava-llm/src/providers/`, `crates/ava-llm/src/provider.rs`, `crates/ava-llm/src/message_transform.rs`, `crates/ava-config/src/keychain.rs`.
   - C++ current surface: `cpp/src/llm/`, `cpp/include/ava/llm/`, `cpp/src/config/`.
    - Impact: C++ has OpenAI, Anthropic, protocol-compatible factory wiring for OpenRouter, Inception, ZAI, Alibaba, Kimi, and MiniMax, plus scoped OAuth credential persistence and key redaction helpers. It still lacks native Gemini, Ollama local API handling, Copilot OAuth/device flow, OS keychain/encrypted fallback, OAuth refresh flows, provider plugin hooks, full message repair/normalization, and Rust-equivalent async streaming coverage.

4. Long-run runtime features remain intentionally scoped.
   - Rust reference: `crates/ava-agent/src/agent_loop/`, `crates/ava-control-plane/src/orchestration.rs`, `crates/ava-tui/src/headless/`.
   - C++ current surface: `cpp/src/agent/runtime.cpp`, `cpp/src/control_plane/`, `cpp/apps/ava/`.
   - Impact: C++ now has scoped structural compaction, budget enforcement, follow-up/post-complete queue promotion, checkpoint events, and headless resume metadata restoration. Rust-equivalent semantic compaction, context-overflow retry, subagent budget rollups, and checkpoint/session JSONL logging remain deferred.

## P1 High-Priority Gaps

1. Session UX breadth.
   - Rust has bookmarks, session search, incremental JSONL/audit surfaces, external delegation links, and richer compaction-context restoration.
   - C++ now has scoped interrupted-tool recovery and checkpoints, but still lacks several higher-level session UX and audit features.

2. TUI feature breadth.
   - Rust has multiline composer behavior, slash/autocomplete/mentions, attachments, modal selectors, sidebar/status/toast systems, tool/thinking grouping, configurable keybindings, refined plan feedback, and richer question options.
   - C++ has a scoped FTXUI app with the critical approval/question/plan dock path, cancellation, and state tests, but it remains intentionally smaller.

3. Core tool advanced parity.
   - Rust-only or partial C++ gaps include hashline anchoring, stale file detection, image/PDF read support, richer output fallback, secret redaction, bash streaming, and full read-state/edit recovery behavior.
   - C++ now has meaningful scoped parity for read/write/edit/bash/glob/grep/git plus backup-history hardening, along with process-local todo state tools and registry-backed tool discovery; default-surface `web_fetch`/`web_search` remain deferred until a safe transport path is promoted, and advanced Rust tool ergonomics plus delegated task/subagent tool routing remain deferred.

4. CLI/config breadth.
    - C++ now has scoped daily-use headless flags (`--cwd`, `--agent`, `--trust`), queue flags, selected environment defaults, routing profile DTOs, and basic project-local recent/model-mode state. It still lacks many Rust CLI/config surfaces: `--thinking`, `--review`, benchmark flags, auth/plugin/update/serve subcommands, YAML/TOML config loading, runtime routing-profile selection, and full keychain/auth configuration parity.

## Verified Corrections From This Audit

1. C++ tool middleware is wired: `cpp/src/tools/registry.cpp` invokes middleware before execution and after result normalization. Any prior claim that middleware is dead code is stale for the current tree.
2. C++ fail-closed TUI approval previews exist for the scoped dock path after M26, but broader modal/widget parity remains deferred.
3. C++ MCP stdio receive behavior is now bounded by transport-level timeout in M27; residual risk has moved from "non-hanging transport" to broader protocol/auth/extension breadth parity.

## Next Completion Sequence

1. Expand from the M40 CPR-gated HTTP POST MVP to full remote MCP transport breadth: SSE execution semantics, negotiated stream handling, and resilient retry/resume/session ownership.
2. Close the remaining MCP runtime breadth gaps: binary blob handling, notification debounce, UI wiring, error recovery, custom TOML tools, and plugin runtime boundaries.
3. Decide whether the M35 heuristic classifier is the long-term C++ policy shape or whether to schedule a broader Rust-policy port (tree-sitter parsing + persistent policy/audit surfaces).
4. Decide whether structural compaction is enough for the C++ lane or schedule semantic compaction/context-overflow retry as future work.
5. Add session bookmarks/search, JSONL audit logging, and cross-runtime ID migration only if the C++ product surface needs them.
6. Expand provider coverage based on product priority: Ollama/Gemini native behavior first, then Copilot and provider-specific long-tail behavior.
7. Add remaining CLI/config breadth only when it has a concrete owner: `--thinking`, `--review`, benchmark flags, subcommands, YAML/TOML config loading, runtime routing-profile selection, and full keychain/OAuth flows.

## Evidence Commands

Use these as the local confidence lane while closing gaps:

```bash
just cpp-configure cpp-debug
just cpp-build cpp-debug
just cpp-test cpp-debug
cargo test -p ava-tools
cargo test -p ava-tui
```
