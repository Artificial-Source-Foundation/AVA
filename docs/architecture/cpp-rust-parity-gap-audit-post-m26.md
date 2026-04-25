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

C++ is strongest in the narrow headless/runtime/TUI slices already implemented by M17-M27: request lifecycle ownership, adapter action routing, child-run cancellation, cooperative signal cancellation, scoped edit parity, Anthropic provider baseline, an MCP stdio runtime MVP bridge, and fail-closed TUI approval previews.

C++ is not yet a complete non-web/non-desktop Rust replacement. The largest remaining gaps are not single bugs; they are whole subsystems that Rust still owns: full permission classification, MCP HTTP/SSE/OAuth + custom-tool breadth, long-tail providers and real streaming breadth, session repair/bookmarks/compaction persistence, headless queue/approval policies, and advanced TUI/editor affordances.

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

4. Long-run runtime features are incomplete.
   - Rust reference: `crates/ava-agent/src/agent_loop/`, `crates/ava-control-plane/src/orchestration.rs`, `crates/ava-tui/src/headless/`.
   - C++ current surface: `cpp/src/agent/runtime.cpp`, `cpp/src/control_plane/`, `cpp/apps/ava/`.
   - Impact: C++ still lacks Rust-equivalent context compaction, budget enforcement, follow-up/post-complete queue promotion, checkpoint/session JSONL logging, and full headless resume metadata restoration.

## P1 High-Priority Gaps

1. Session repair and recovery.
   - Rust has conversation repair, interrupted tool cleanup, bookmarks, session search, incremental persistence, external delegation links, and compaction-context restoration.
   - C++ has strong SQLite/schema foundations but lacks several higher-level session recovery and UX features.

2. TUI feature breadth.
   - Rust has multiline composer behavior, slash/autocomplete/mentions, attachments, modal selectors, sidebar/status/toast systems, tool/thinking grouping, configurable keybindings, refined plan feedback, and richer question options.
   - C++ has a scoped FTXUI app with the critical approval/question/plan dock path, cancellation, and state tests, but it remains intentionally smaller.

3. Core tool advanced parity.
   - Rust-only or partial C++ gaps include hashline anchoring, stale file detection, image/PDF read support, richer output fallback, secret redaction, bash streaming, and full read-state/edit recovery behavior.
   - C++ has meaningful scoped parity for read/write/edit/bash/glob/grep/git plus backup-history hardening, but advanced Rust tool ergonomics remain deferred.

4. CLI/config breadth.
   - C++ lacks many Rust CLI and config surfaces: `--cwd`/`AVA_WORKING_DIRECTORY`, `--agent`, `--trust`, `--thinking`, `--review`, queue flags (`--follow-up`, `--later`, `--later-group`), benchmark flags, auth/plugin/update/serve subcommands, YAML/TOML config loading, and project state.

## Verified Corrections From This Audit

1. C++ tool middleware is wired: `cpp/src/tools/registry.cpp` invokes middleware before execution and after result normalization. Any prior claim that middleware is dead code is stale for the current tree.
2. C++ fail-closed TUI approval previews exist for the scoped dock path after M26, but broader modal/widget parity remains deferred.
3. C++ MCP stdio receive behavior is now bounded by transport-level timeout in M27; residual risk has moved from "non-hanging transport" to broader protocol/auth/extension breadth parity.

## Next Completion Sequence

1. Extend the C++ MCP bridge beyond the new stdio MVP: add HTTP/SSE and OAuth-safe auth flows, then close resources/prompts and error-recovery breadth gaps.
2. Port the Rust permission classifier/policy core or define a smaller C++ policy that is explicitly accepted as a product constraint.
3. Implement context compaction and budget enforcement in the C++ runtime before declaring long-run headless parity.
4. Add session repair/interrupted-tool cleanup before relying on C++ persisted transcripts for crash recovery.
5. Expand provider coverage based on product priority: Ollama/Gemini/OpenRouter first, then Copilot and long-tail compatible providers.
6. Add CLI/config parity for non-web/non-desktop daily use: `--cwd`, `--agent`, queue flags, `--thinking`, env provider/model/cwd defaults, and explicit C++ help docs.

## Evidence Commands

Use these as the local confidence lane while closing gaps:

```bash
just cpp-configure cpp-debug
just cpp-build cpp-debug
just cpp-test cpp-debug
ionice -c 3 nice -n 15 env CARGO_BUILD_JOBS=4 cargo test -p ava-tools -- --test-threads=4
ionice -c 3 nice -n 15 env CARGO_BUILD_JOBS=4 cargo test -p ava-tui -- --test-threads=4
```
