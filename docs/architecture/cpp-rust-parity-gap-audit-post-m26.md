---
title: "C++/Rust Parity Gap Audit Post-M26"
description: "Non-web/non-desktop parity audit comparing the active Rust product surface with the scoped C++ migration tree after Milestone 26."
order: 18
updated: "2026-04-25"
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

C++ is not yet a complete non-web/non-desktop Rust replacement. The largest remaining gaps are not single bugs; they are whole subsystems that Rust still owns or areas intentionally scoped smaller in C++: full permission policy breadth, MCP HTTP/SSE/OAuth + custom-tool/plugin breadth, long-tail providers, semantic compaction and JSONL audit logging, bookmarks/search, hard-kill recovery, and advanced TUI/editor affordances.

## M28-M34 Completion Update

The post-M26 milestone sequence closed several earlier audit gaps with scoped, tested slices rather than broad Rust parity claims:

1. **Permission/security (M28):** source-aware permission inspection, exact-subject session approval caching, and compact headless dangerous-command classification are wired; persistent rules and full Rust classifier breadth remain deferred.
2. **Long-run runtime (M29):** budget accumulation/warnings/exhaustion, follow-up/post-complete queue promotion, and structural `agent_visible=false` compaction are wired; semantic summarization and JSONL logging remain deferred.
3. **Session recovery/checkpoints (M30):** interrupted-tool recovery, checkpoint events, and headless checkpoint saves are wired; bookmarks/search and cross-runtime UUID migration remain deferred.
4. **Provider streaming (M31):** Anthropic CPR-gated SSE streaming is wired and parser-tested; broad long-tail providers and provider-specific retry/backoff parity remain deferred.
5. **CLI/config/tool polish (M32):** `--cwd`, `--agent`, `--trust`, selected environment defaults, builtin-agent max-turn defaults, and core-tool metadata/output polish are wired; YAML config, custom TOML tools, web/browser tools, and subcommands remain deferred.
6. **Headless integration proof (M33):** deterministic non-live app-boundary coverage now exercises workspace/trust, resume/recovery, queue turns, budget-warning NDJSON, and event ordering through `run_headless_blocking`.
7. **Hardening/evidence (M34):** production-code shape review, small hardening fixes, focused local evidence, and residual-risk documentation close the scoped backend/headless lane without promoting web/desktop/full Rust parity.

## P0 Completion Blockers

1. Full command-risk classification is still Rust-only.
   - Rust reference: `crates/ava-permissions/src/classifier/`, `crates/ava-permissions/src/inspector.rs`, `crates/ava-tools/src/permission_middleware.rs`.
   - C++ current surface: `cpp/src/tools/core_tools.cpp`, `cpp/src/tools/permission_middleware.cpp`.
   - Impact: C++ has a fail-closed permission seam and approval bridge, but it does not yet have Rust's bash/parser-differential risk classifier, persistent rules, path/glob rules, warning tags, or plugin permission hooks.

2. MCP/custom-tool runtime breadth remains incomplete in C++.
   - Rust reference: `crates/ava-mcp/src/{client,transport,manager,config,oauth}.rs`, `crates/ava-tools/src/mcp_bridge.rs`, `crates/ava-tools/src/core/custom_tool.rs`.
   - C++ current surface: `cpp/src/mcp/`, `cpp/include/ava/mcp/`, `cpp/src/tools/mcp_bridge.cpp`, `cpp/src/orchestration/composition.cpp`, `cpp/MILESTONE27_BOUNDARIES.md`.
   - Impact: C++ now has stdio process spawning, receive timeouts, manager lifecycle/isolation, namespaced runtime registration, and headless/runtime MCP tool execution, but still lacks HTTP/SSE transport parity, OAuth/refresh flows, resources/prompts surfaces, custom TOML tool execution, and broader extension-runtime parity.

3. Real provider breadth and streaming architecture remain incomplete.
   - Rust reference: `crates/ava-llm/src/providers/`, `crates/ava-llm/src/provider.rs`, `crates/ava-llm/src/message_transform.rs`, `crates/ava-config/src/keychain.rs`.
   - C++ current surface: `cpp/src/llm/`, `cpp/include/ava/llm/`, `cpp/src/config/`.
   - Impact: C++ has OpenAI plus a scoped Anthropic baseline, but it still lacks Gemini/OpenRouter/Ollama/Copilot/Inception/Alibaba/ZAI/Kimi/Minimax provider parity, keychain/OAuth flows, provider plugin hooks, full message repair/normalization, and Rust-equivalent async streaming coverage.

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
   - C++ has meaningful scoped parity for read/write/edit/bash/glob/grep/git plus backup-history hardening, but advanced Rust tool ergonomics remain deferred.

4. CLI/config breadth.
   - C++ now has scoped daily-use headless flags (`--cwd`, `--agent`, `--trust`), queue flags, and selected environment defaults. It still lacks many Rust CLI/config surfaces: `--thinking`, `--review`, benchmark flags, auth/plugin/update/serve subcommands, YAML/TOML config loading, and richer project state.

## Verified Corrections From This Audit

1. C++ tool middleware is wired: `cpp/src/tools/registry.cpp` invokes middleware before execution and after result normalization. Any prior claim that middleware is dead code is stale for the current tree.
2. C++ fail-closed TUI approval previews exist for the scoped dock path after M26, but broader modal/widget parity remains deferred.
3. C++ MCP stdio receive behavior is now bounded by transport-level timeout in M27; residual risk has moved from "non-hanging transport" to broader protocol/auth/extension breadth parity.

## Next Completion Sequence

1. Extend the C++ MCP bridge beyond the stdio MVP: add HTTP/SSE and OAuth-safe auth flows, then close resources/prompts and error-recovery breadth gaps.
2. Port the Rust permission classifier/policy core or define a smaller C++ policy that is explicitly accepted as a product constraint.
3. Decide whether structural compaction is enough for the C++ lane or schedule semantic compaction/context-overflow retry as future work.
4. Add session bookmarks/search, JSONL audit logging, and cross-runtime ID migration only if the C++ product surface needs them.
5. Expand provider coverage based on product priority: Ollama/Gemini/OpenRouter first, then Copilot and long-tail compatible providers.
6. Add remaining CLI/config breadth only when it has a concrete owner: `--thinking`, `--review`, benchmark flags, subcommands, YAML/TOML config loading, and richer project state.

## Evidence Commands

Use these as the local confidence lane while closing gaps:

```bash
just cpp-configure cpp-debug
just cpp-build cpp-debug
just cpp-test cpp-debug
ionice -c 3 nice -n 15 env CARGO_BUILD_JOBS=4 cargo test -p ava-tools -- --test-threads=4
ionice -c 3 nice -n 15 env CARGO_BUILD_JOBS=4 cargo test -p ava-tui -- --test-threads=4
```
