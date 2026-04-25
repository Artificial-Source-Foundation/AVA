---
title: "Backlog"
description: "Active AVA 0.6 work for the V1 push, plus an archive of the previous detailed backlog."
order: 3
updated: "2026-04-25"
---

# AVA Backlog

This backlog now tracks the active `0.6 -> V1` checklist first.

The previous detailed `3.3 / 3.3.1` backlog has been collapsed into thematic archive form below so the current priority stays easy to read.

Source of truth for direction: `docs/project/roadmap.md`

## Active Now

1. Polish the desktop app until it feels ready for daily use, especially chat layout, settings, tool cards, and overall fit and finish.
2. Make sure every core tool and important agent action has clear UI so users can understand what AVA is doing without reading logs.
3. Verify multi-chat behavior end to end so users can switch sessions while runs continue without losing state, mixing outputs, or breaking approvals/plans.
4. Prove the backend can do real coding work reliably in a **headless-first** flow (authoritative for backend correctness under the current scoped non-interactive exception), with TUI, desktop, and web checks to confirm lighter surface parity only (not full interactive approval/question/plan proof by itself).
5. Add a simple automated product smoke suite for the core journey: prompt -> tools -> edit -> verify -> persist.
6. Keep a repeatable AVA-vs-OpenCode comparison path so quality can be measured, not argued.
7. Finish the docs reset around the `0.6` story so roadmap, backlog, README, release language, and the public docs front door all describe the same product stage without mixing normal-user paths with contributor workflow detail.
8. Keep the power-user source-build docs aligned with real Cargo usage patterns, including out-of-tree invocation and clear profile guidance.
9. C++ M1-M16 are completed boundary slices; C++ Milestone 1 remains the historical contract/freeze authority, and the post-M16 gap/parity audits now own completion-gap + contract-evidence planning while future work stays narrow.
10. Continue post-M16 C++ migration in narrow slices only: keep scope to backend-owned interactive lifecycle maturity and adapter-level UX increments; defer full modal parity, MCP/plugin parity, and broad async/background runtime migration.
11. Use the post-M26 C++/Rust parity audit as the current non-web/non-desktop completion map: close MCP runtime bridging, permission classification/policy, runtime compaction/budget, session repair, provider breadth, and CLI/config parity before claiming C++ replacement completeness.

Active-now migration note: C++ Milestone 1 remains the historical contract/planning authority (`docs/architecture/cpp-backend-tui-migration-plan-m1.md`, `docs/architecture/cpp-contract-freeze-m1.md`, `docs/architecture/cpp-m1-event-stream-parity-checklist.md`), C++ Milestone 2 landed the initial `cpp/` build-graph bootstrap, C++ Milestone 3 replaced placeholders with foundational contract/filesystem behavior in `ava_types`, `ava_control_plane`, and `ava_platform`, C++ Milestone 4 added foundational `ava_config` + `ava_session` persistence, C++ Milestone 5 landed a scoped real `ava_llm` foundation (`cpp/MILESTONE5_BOUNDARIES.md`), C++ Milestone 6 landed a scoped real `ava_tools` core-tool-system foundation (`cpp/MILESTONE6_BOUNDARIES.md`), C++ Milestone 7 landed a scoped real `ava_agent` runtime-core foundation (`cpp/MILESTONE7_BOUNDARIES.md`), C++ Milestone 8 landed a scoped real `ava_orchestration` contracts/data foundation (`cpp/MILESTONE8_BOUNDARIES.md`), C++ Milestone 9 landed the first smallest-honest blocking headless CLI proof path (`cpp/MILESTONE9_BOUNDARIES.md`), C++ Milestone 10 landed a smallest-honest headless validation slice (`cpp/MILESTONE10_BOUNDARIES.md`), C++ Milestone 11 landed a smallest-honest interactive FTXUI TUI slice (`cpp/MILESTONE11_BOUNDARIES.md`), C++ Milestone 12 tightened that TUI slice with bounded parity validation + cleanup (`cpp/MILESTONE12_BOUNDARIES.md`), and C++ Milestone 13 now adds an orchestration-owned shared runtime composition seam plus native blocking subagent execution baseline (`cpp/MILESTONE13_BOUNDARIES.md`) including runtime-owned metadata precedence, fail-closed allowed-tool validation, and bounded subagent depth/spawn/turn controls. This remains intentionally short of behavior-parity backend/TUI runtime migration, full orchestration runtime parity, and full async/interactive headless parity, with config/default DTO ownership still anchored in `ava_config`, task-tool parity still deferred, and runtime-owned background spawn semantics still deferred.

Current bounded parity note: desktop and web submit/replay command paths now both use accepted-and-streaming run-start semantics (contract-follow-up Milestone 10 resolved `EX-002`), and web submit/replay flows continue to honor the shared persisted per-run thinking/model/compaction context used by desktop session runs; the remaining bounded divergence is the manual TUI/headless `/compact` path tracked in `docs/architecture/backend-contract-exceptions.md` as `EX-003`.

Current TUI delegation note: native blocking subagents now get real child sessions at start, stream live child transcript/tool updates into the TUI during execution, and reopen from canonical child-session state instead of relying only on final reconstruction; AVA now also exposes explicit background-agent semantics for non-blocking delegation and queues finished background summaries back into the parent run, so the remaining work is mostly presentation/fit-and-finish parity rather than missing live delegated-session ownership.

Current loop-detection note: exploratory repeated use of the same search-style tool with different arguments no longer trips the old name-only repetition warning, while exact repeated calls still warn everywhere and broader repeated call-signature cycle detection (including same-tool ping-pong and `A-B-C-A-B-C` style loops) is now reserved for providers/models already marked `loop_prone`.

Current CI note: the main CI pipeline no longer exports `RUSTC_WRAPPER=sccache` into browser-smoke or security-audit jobs that do not install `sccache`, and the frontend/Rust check suites are back in sync with the current startup primary-agent loading behavior and recent clippy expectations.

Current C++ migration CI note: CI now includes a minimal non-interactive `cpp/` configure/build/test lane that exercises the committed `cpp-release` CMake preset from the `cpp/` working directory (`cmake --preset`, `cmake --build --preset`, `ctest --preset`) to keep the active C++ milestone tree exercised without adding broad extra matrix surface.

Current C++ Milestone 2 green-fix note: bootstrap ergonomics now include committed CMake presets (`cpp/CMakePresets.json`), repo-level `just` helpers for C++ configure/build/test/clean, and explicit Milestone 2 boundary documentation (`cpp/MILESTONE2_BOUNDARIES.md`) while keeping runtime behavior unchanged.

Current C++ Milestone 4 green-fix note: the foundational config/session slice now closes the reduced DTO/schema gap for Rust-compatible persistence fields at the M4 level (including richer message/session persistence columns), applies deterministic SQLite policy defaults (`WAL`, `synchronous=NORMAL`, `foreign_keys=ON`, `busy_timeout=5000`, `cache_size=-64000`) with idempotent legacy-column migrations, and enforces POSIX owner-only permissions (`0600`) for sensitive JSON stores (`credentials.json`, `trusted_projects.json`) while keeping keychain/OAuth/YAML/FTS/runtime-parity expansion deferred.

Current C++ Milestone 7 green-fix note: the foundational `ava_agent` runtime now respects `SessionRecord::branch_head` when preparing provider prompts and when parenting new runtime-appended user/assistant messages, so inactive sibling branches in storage order no longer leak into the active branch path.

Current C++ Milestone 9 green-fix note: the headless CLI metadata path now clears stale `metadata.headless.last_run.error` after a later successful rerun in the same session, so persisted run metadata reflects the current terminal state instead of retaining an earlier approval/runtime failure.

Current C++ Milestone 10 green-fix note: the headless validation slice now covers multiple tool calls emitted from one assistant turn plus `max_turns` terminal-state persistence and NDJSON completion reason emission through the full scripted app integration path.

Current C++ Milestone 11 green-fix note: the TUI `AppState` scroll path now uses saturating scroll-down arithmetic, preventing very large keyboard/page-scroll deltas from wrapping `size_t` and moving the viewport backward instead of clamping to the bottom.

Current C++ Milestone 12 green-fix note: the bounded TUI cleanup slice now handles UTF-8 backspace by erasing whole codepoints, caps child-run observer state to the bounded view size, preserves actionable CLI parse diagnostics, and adds focused coverage for viewport slicing plus unsupported `/model <args>` behavior.

Current C++ Milestone 13 green-fix note: the orchestration baseline now centralizes the read-only specialist tool allowlist, distinguishes explicit subagent disablement from default-disable configuration, and covers explicit session resume, legacy `metadata.headless.*` runtime-selection fallback, and default `TaskSpawner::spawn(...)` delegation.

Current C++ Milestone 14 green-fix note: the interactive control-plane seam now fails adapter-originated stale settlements closed, rejects unknown canonical event enum values instead of silently mapping them to fallback contract strings, and covers pending-request ordering plus question/plan resolver exception and invalid-terminal-state behavior.

Current C++ Milestone 15 green-fix note: the streaming/cancellation seam now suppresses semantically empty assistant-response events for streamed tool-call-only turns, persists foreground headless `metadata.headless.last_run.run_id`, and explicitly clears minimal TUI running state after runtime return while keeping broader async/background/runtime parity deferred.

Current C++ Milestone 16 green-fix note: the TUI workflow slice now consumes interactive-dock Backspace without mutating the hidden composer, lets question answers include `q`, keeps accepted dock actions from taking backend lifecycle ownership, and covers unknown slash commands, `/clear` interactive reset, message-navigation/streamed-delta boundaries, empty request IDs, UTF-8 answer backspace, history traversal reset, and stale interactive metadata cleanup.

Current C++ Milestone 17 green-fix note: the interactive terminal-state closure slice now makes request registration exception-safe against pending-order insertion failures and strengthens control-plane request-store evidence for kind/state string conversions, default null run IDs, terminal lookup correlation, stale lookup rejection, all terminal re-settlement paths, and pending-order preservation across resolve/cancel/timeout transitions.

Current C++ Milestone 18 green-fix note: the TUI adapter-action harness now keeps `AppState` display-only after accepted adapter results, relying on bridge/backend pending snapshots to clear visibility; the documented backend-clear validation test name is present, cancel-question/reject-plan are explicitly in scope, and focused tests cover default reject reasons plus empty answer fallbacks.

Current C++ Milestone 19 green-fix note: child-run exception paths now best-effort persist terminal `metadata.orchestration.subagent_run` details and record terminal summaries without masking the original runtime exception; watchdog classification is captured at cancellation detection time; cancellation-handle truthiness now reflects actual cancellability; and TUI child-run observer state preserves terminal errors/watchdog state while strengthening bounded active-run eviction coverage.

Current C++ Milestone 20 green-fix note: runtime-created tool result messages now populate typed `tool_call_id` metadata alongside JSON tool-result payloads, headless auto-approve treats `low` inspection risk as safe, and focused integration coverage verifies high-risk `write`, `edit`, and `bash` calls remain rejected under `--auto-approve` while safe read-only execution stays covered.

Current C++ Milestone 21 green-fix note: successful child runs that exhaust `max_turns` without an error now still emit canonical `SubagentComplete` events, malformed `subagent_complete` NDJSON errors preserve a canonical `run_id` and name the missing field, and focused tests cover optional `message_count` omission plus max-turn child event correlation.

Current C++ Milestone 22 green-fix note: the edit-tool cascade now fails closed when flexible-whitespace matching is ambiguous, rejects empty explicit anchors and invalid locator values deterministically, applies `replace_all` bounds after CRLF normalization, and strengthens focused coverage for locator preflights, cascade ambiguity, ellipsis failure, non-`replace_all` deletion, and replacement-count/output-size limits.

Current C++ Milestone 23 green-fix note: the Anthropic provider slice now preserves structured parser `ProviderException` values, falls back to a valid object schema for non-object tool parameters, and strengthens focused protocol coverage for thinking blocks, defensive content shapes, tool argument/result coercion, cache-creation usage, and system/tool request edge cases.

Current C++ Milestone 24 green-fix note: the runtime-control slice now clears headless interactive bridge run IDs on runtime exceptions, makes the signal-handler scope non-copyable, and strengthens focused coverage for nested signal-handler installs plus persisted child-run metadata after parent-driven cancellation.

Current C++ Milestone 25 foundation green-fix note: MCP client health now counts terminal failures while replying to server-initiated requests, malformed non-object initialize capabilities fail closed, manager shutdown clears cached reports, POSIX-only stdio transport tests are guarded on Windows, and milestone-labelled MCP diagnostics point at the M25 foundation slice.

Current C++ Milestone 25 green-fix note: the local MCP stdio mock server now ignores JSON-RPC notifications instead of returning invalid `id: null` responses, so the stdio transport fixture follows request/notification semantics and the MCP regression remains deterministic.

Current C++ Milestone 27 green-fix note: the MCP stdio test lane now resolves POSIX `environ` through the global symbol when tools link the MCP bridge, fixes the process-group cleanup fixture to emit valid JSON-RPC, and hardens stdio transport cleanup by draining generated SIGPIPEs with bounded retry, reading child stdout in nonblocking mode, and closing inherited child file descriptors before `execvp`.

Current C++ Milestone 1 freeze governance note: freeze authority and enforcement now include an explicit CODEOWNERS section for freeze-governed paths, a PR template freeze-lift checklist reminder, and a dedicated CI guard (`scripts/dev/verify-cpp-m1-freeze.sh`) that fails freeze-file PRs unless `freeze-lift` is labeled and the freeze/parity checklist docs are updated in the same PR.

Current C++ Milestone 16 note: the current narrow pass now keeps orchestration/backend ownership intact while adding TUI workflow parity basics in `cpp/apps/ava_tui` (`/help`, `/clear`, `/model`, graceful unsupported `/compact`, input history up/down, message-navigation/status affordances, and adapter-facing interactive request visibility/clearing seams in `AppState`); full modal interactive UX and broader parity remain deferred.

Current post-M16 planning note: migration-completion gaps and first research priorities are tracked in `docs/architecture/cpp-backend-tui-migration-completion-gap-audit-m16.md`; scoped contract evidence and trace scenarios are tracked in `docs/architecture/cpp-backend-tui-parity-contract-audit-post-m16.md`; and the phased M17-M20 execution/decision roadmap is tracked in `docs/architecture/cpp-backend-headless-tui-migration-completion-roadmap-post-m16-m17-plus.md` (M17 interactive terminal-state closure, M18 TUI adapter action harness, M19 child-run cancellation/watchdog, M20 evidence hardening + deferred-inventory guardrails). Together these keep completion claims scoped to backend/headless/TUI and preserve web/desktop/MCP/plugin/provider/auth/config breadth as deferred-inventory or intentional non-goal buckets unless backlog scope is explicitly promoted.

Current C++ Milestone 17 note: the first post-M16 implementation slice keeps lifecycle ownership in control-plane/orchestration while adding focused request-store coverage for stale/non-existent request IDs, already-terminal request rejection, pending-request preservation, and terminal `run_id`/`request_id` correlation (`cpp/MILESTONE17_BOUNDARIES.md`); TUI adapter actions, child-run cancellation/watchdog work, and broader parity remain deferred to M18+.

Current C++ Milestone 18 note: the second post-M16 implementation slice adds a narrow `InteractiveActionAdapter` harness for request-id-bearing approve/reject/answer/accept-plan actions through `InteractiveBridge` (`cpp/MILESTONE18_BOUNDARIES.md`); full modal UX, child-run cancellation/watchdog work, and broader parity remain deferred.

Current C++ Milestone 19 note: the third post-M16 implementation slice adds orchestration-owned child-run visibility/cancellation and a bounded RunController deadline watchdog (`cpp/MILESTONE19_BOUNDARIES.md`), records deterministic child terminal summaries under child-session metadata, and projects child-run terminal metadata into TUI observer state without giving the TUI lifecycle ownership; broad async scheduler parity, hard-kill provider interruption, full child-run modal UX, and broader web/desktop/MCP/plugin/provider/auth/config parity remain deferred.

Current C++ Milestone 20/21 note: the scoped backend/headless/TUI completion gate aggregates M17-M19 evidence and closes focused parity-audit rows for NDJSON tool `call_id` correlation, explicit resume-by-ID tool-heavy metadata survival, cancellation transcript integrity, `EX-001` high-risk auto-approve rejection, and edit no-match immutability (`cpp/MILESTONE20_BOUNDARIES.md`). M21 starts the next parity-expansion loop with canonical `SubagentComplete` event/NDJSON projection and native blocking spawner event-sink emission (`cpp/MILESTONE21_BOUNDARIES.md`), but `EX-004` remains active until the default C++ headless task/subagent tool route produces `subagent_complete` end to end; full scripted NDJSON stream parity, full Rust edit strategy parity, broad async/hard-kill cancellation, and web/desktop/MCP/plugin/provider/auth/config breadth remain deferred or intentional non-goals.

Current C++ Milestone 22 note: the C++ `edit` tool now partially lifts the prior exact-only limitation for non-`replace_all` edits by adding a bounded deterministic cascade (quote-normalized exact matching, explicit occurrence/line-number/anchor targeting, line-trimmed blocks, auto-block-anchor matching, ellipsis fragments, and flexible-whitespace matching), bounded `replace_all` work, read/edit/backup file-size and backup-symlink hardening, focused `ava_tools_tests` coverage, and explicit boundaries in `cpp/MILESTONE22_BOUNDARIES.md`; advanced merge/fuzzy recovery parity remains deferred.

Current C++ Milestone 23 note: the C++ `ava_llm` lane now adds a scoped Anthropic production-provider slice (`cpp/MILESTONE23_BOUNDARIES.md`) with factory wiring, CPR-gated Messages API request/response handling, and focused unit coverage while keeping streaming parity, OAuth/device/browser auth, keychain integration, and broad provider/auth/config breadth (Gemini/OpenRouter/Ollama/Copilot/Inception/Alibaba/ZAI/Kimi/Minimax and related long-tail routes) explicitly deferred.

Current C++ Milestone 24 note: the C++ runtime-control lane now adds parent-to-child cooperative cancellation propagation and a headless `SIGINT`/`SIGTERM` cooperative cancellation bridge (`cpp/MILESTONE24_BOUNDARIES.md`) with focused orchestration/app tests; full async/background scheduler parity, provider hard-kill/HTTP abort parity, immediate signal termination semantics, follow-up/post-complete queue population, concurrent read-only tool execution, and full TUI runtime-control UX remain deferred.

Current C++ Milestone 25 note: the C++ extension-runtime lane now starts MCP parity with a narrow `ava_mcp` foundation (`cpp/MILESTONE25_BOUNDARIES.md`): JSON-RPC helpers, in-memory transport, synchronous client support for `initialize`/`tools/list`/`tools/call`, stdio-only config parsing, MCP/custom-tool path helpers, and focused tests. Full MCP runtime tool registration, stdio process spawning, HTTP/SSE/OAuth, plugin runtime, TOML custom tools, browser tools, and TUI/desktop MCP UX remain deferred.

Current C++ Milestone 26 note: the C++ TUI lane now adds a scoped interactive-request dock slice (`cpp/MILESTONE26_BOUNDARIES.md`) for pending approval/question/plan handles. The dock projects minimal/truncated UTF-8-safe approval/question/plan previews, disables approval when a tool payload is truncated, builds request-id-bearing approve/reject/answer/accept-plan/reject-plan/cancel-question adapter actions, and keeps orchestration/control-plane lifecycle ownership intact; focused tests cover dock projection, sticky priority, dismissal, empty-answer submission, and backend-owned clearing while full Rust modal/widget parity, polished request-payload rendering, command palette/session/model/provider/theme UX, MCP/plugin/custom-tool TUI UX, and web/desktop UX remain deferred.

Current C++ Milestone 27 note: the C++ MCP lane now lands a smallest-honest runtime bridge slice (`cpp/MILESTONE27_BOUNDARIES.md`) with real stdio transport NDJSON framing + receive timeout (non-hanging behavior), manager lifecycle/isolation for enabled stdio servers, namespaced MCP tool registration into the shared runtime composition path (`mcp_<server>_<tool>` with original-name call routing and source tracking), focused in-memory manager/bridge coverage, and a local mock stdio MCP server test helper. Final M27 hardening now also enforces allowlist-based stdio child environments, POSIX process-group shutdown for stdio child cleanup, bounded overall request deadlines in noisy JSON-RPC wait loops, receive-buffer cap regression coverage for oversized undelimited payloads, and full `ava_orchestration_tests` validation in the M27 evidence lane. Broad custom TOML tool execution, MCP HTTP/SSE/OAuth breadth, plugin runtime parity, and TUI/desktop MCP UX remain deferred.

Current C++ Milestone 27 green-fix note: the MCP stdio lane now declares POSIX `environ` at global scope so tools targets that link the MCP bridge resolve cleanly, and the process-group cleanup fixture emits valid JSON-RPC instead of backslash-escaped JSON.

Current C++ Milestone 28 note: the C++ permission/security lane now adds source-aware permission inspection and exact-subject session approval caching (`cpp/MILESTONE28_BOUNDARIES.md`). Headless bash calls get a compact dangerous-command classifier that denies critical commands before approval/session caches, MCP/custom sources default to explicit approval, and unsupported `AllowAlways` persistence now fails closed instead of being silently treated as session-only. Full Rust classifier breadth, persistent permission rules, custom TOML execution, and shared process-runner hardening remain deferred.

Current post-M26 C++/Rust parity note: the broad non-web/non-desktop parity sweep is tracked in `docs/architecture/cpp-rust-parity-gap-audit-post-m26.md`. The audit corrects stale findings where the current tree already has scoped parity (for example tool middleware wiring and fail-closed TUI approval previews) and keeps the remaining completion blockers explicit: full permission classification, MCP/custom-tool runtime wiring, provider/streaming breadth, compaction/budget/session recovery, and C++ CLI/config breadth.

Current provider-connect note: the TUI OAuth/browser-login fallback path now exposes a visible copy-to-clipboard shortcut for the full auth URL (plus browser reopen on the browser-login screen), and the TUI now keeps clipboard ownership alive after auth-URL copy on Linux so OpenAI browser/headless login no longer trips the short-lived clipboard-owner regression that was splattering multiline backend errors across the modal; long-lived TUI sessions now also refresh stale cached clipboard handles once on failure instead of requiring an app restart, and clipboard-backed image paste now surfaces real read failures instead of masking them as a generic empty-clipboard warning.

Current OpenAI provider note: the `ava-llm` OpenAI streaming path now routes normal, tools, and thinking-enabled requests through one internal helper so the central request-body builders stay easy to reason about without repeating the surrounding send/validate/SSE plumbing in three separate methods.

Current hook note: the local `pre-push` gate now keeps its fast path-aware shape while adding targeted compile smokes for touched high-risk Rust surfaces (`Cargo.toml`/workspace wiring, `src-tauri`, `crates/ava-web`, `crates/ava-config`) and stronger regression coverage for both routing logic and the installed Lefthook wrapper path.

Current subagent rework note: the backend foundation slice from `docs/architecture/subagent-rework-blueprint-m1.md` is now in place and the Milestone 2 material follow-up is closed on the backend seam (effective catalog now includes the default `subagent` alias, canonical subagent config read/write targeting lives in `ava-config`, backend-gate now runs the lighter focused backend smoke checks, and delegated smoke now asserts real `SubAgentComplete` payload fields); remaining work is the later TUI/desktop/web adoption/settings polish.

Current modularization roadmap note: the active Track Milestone 1 roadmap remains documented in `docs/architecture/agent-backend-modularization-roadmap-m1.md` (hotspots, target owner seams, sequence, risk controls, and validation gates). Track Milestone 2 backend extraction is in place for the shared control-plane seam (`crates/ava-control-plane`), Track Milestone 3 now lands a real orchestration owner seam via `crates/ava-agent-orchestration` with internal callers importing `stack`/`subagents` directly from that crate, Track Milestone 4 includes a narrow TUI boundary cleanup that separates terminal/UI event dispatch from runtime-driven event handling (`event_dispatch.rs` vs `runtime_event_dispatch.rs`) without changing runtime behavior, Track Milestone 5 now makes the side-surface boundary real by moving `ava serve` ownership into standalone `crates/ava-web` while preserving the existing `ava` CLI entrypoint, and the Track Milestone 6 lower-layer cleanup pass now routes pure control-plane contract consumers in touched `ava-web`/`ava-tui`/`src-tauri` paths to `ava-control-plane` directly instead of the `ava-agent` compatibility re-export seam (keeping only backend-only helpers on `ava-agent::control_plane::*`); follow-up Track Milestone 7 cleanup also retired the now-unneeded `ava-agent` crate-root pure control-plane re-exports so direct `ava-control-plane` ownership is explicit, follow-up Track Milestone 8 narrows `ava-agent-orchestration`'s crate-root surface to orchestration-owned `stack`/`subagents` only (runtime-core modules now imported directly from `ava-agent` inside orchestration internals), and Track Milestone 11 now removes the remaining pure `ava-agent::control_plane` compatibility shim modules so only backend-owned `events`/`sessions` helpers remain there.

Milestone namespace note: the shared-backend contract chain uses M4-M7, the contract-follow-up closure sequence is M10-M12, the backend modularization roadmap uses separate **Track Milestone N** numbering, the Rust-to-C++ planning artifacts use **C++ Milestone N** numbering, and the post-M16 C++ backend/headless/TUI completion roadmap uses M17-M20.

Current session-title note: first-message auto-titling now treats both `New Chat` and legacy `New Session` placeholders as renameable defaults and the web backend no longer cements the placeholder into persisted metadata before the first real prompt, but broader conversation-list/session-polish work remains in the active desktop app fit-and-finish queue.

Current web hardening note: `ava serve` now defaults to loopback-only bind/origin exposure, token-protects sensitive session/history/status reads plus persisted plan listing/loading routes, high-risk plugin/CLI discovery + plugin route surfaces, and privileged HTTP control-plane routes (and `/ws`), redacts raw control tokens from normal logs, and still keeps broader browser-origin exposure as an explicit `--insecure-open-cors` opt-in.

Current multi-chat correctness note: overlapping frontend session switches now gate async persisted-session finalization on the winning switch/current session, so an older load finishing late cannot re-select the stale session, overwrite the visible session artifacts, or re-persist the old last-session selection after a newer switch has already won.

Current web session note: the browser fallback/session-adapter path now fails closed for backend session writes, archived-session deletion clears archived client state too, and web create/list session payloads now preserve `project_id` through the existing metadata seam so project-scoped browser lists do not silently drift.

Current runtime-isolation note: setting `AVA_PURE=1` now starts AVA without auto-loading any global or project-local power plugins, giving headless or automation runs a simple no-plugins mode without moving or uninstalling plugin directories.

Current config note: global instruction loading now reads `$XDG_CONFIG_HOME/ava/AGENTS.md`; AVA's global config/data/state/cache defaults now live under lowercase XDG app directories instead of `~/.ava`.

Current CLI note: `ava` now accepts `--cwd <path>`, and `AVA_WORKING_DIRECTORY` provides the same cwd override for automation; both set the actual runtime working directory, and AVA still infers the repository/worktree from that cwd instead of taking a separate repo-root argument.

Current primary-agent note: startup now supports first-class configurable primary agents in `config.yaml` (`primary_agents` + optional `primary_agent`) plus an explicit `--agent <id>` CLI override; resumed/session-switched TUI sessions now persist and restore both `primaryAgentId` and `primaryAgentPrompt` metadata and re-apply primary-agent prompt behavior on restore, configured startup profiles now also participate in TUI `Tab` / `Shift+Tab` cycling (falling back to the existing Build/Plan mode cycle when no primary-agent profiles are configured), and explicit `--agent` still wins over resumed session metadata so startup intent remains deterministic.

Current child-transcript note: TUI subagent transcript views now keep child-only interactions scoped correctly (thinking/tool-group expansion mutates the child transcript, not the parent), suppress the extra read-only composer strip in favor of a single footer hint row, and hide internal child-transcript system scaffolding so delegated conversations read more consistently with the main transcript without claiming full parity.

Current docs note: user-facing setup/discovery docs for primary agents + subagents are now centralized in `docs/how-to/agents.md`, with targeted reference cross-links for config locations, explicit `agents.toml` -> `subagents.toml` migration guidance, canonical `subagents.toml` behavior, built-ins/defaults, `--agent`, resume/override semantics, trust gating, and `prompt_file` external prompt references.

## V1 Proof Definition

1. **Backend scope is headless-first**: the authoritative proof path is `ava` headless/benchmark execution.
2. **Proof is real work**: milestone evidence comes from realistic coding suites (`normal_coding`, `small_coding`, `stress_coding`, `test_heavy`) with compile/test validation, plus selected `tool_reliability` coverage and a minimal product smoke (`prompt -> tools -> edit -> verify -> persist`) as the required proof set; contract checks support but do not drive the milestone gate.
3. **Approval policy baseline**: only dangerous commands/actions require explicit approval; ordinary safe tool calls and workspace-preserving edits remain low-friction by default, and unattended headless proof must exercise that real runtime path rather than silently upgrading dangerous asks through yolo wiring.
4. **Primary comparison reference**: `OpenCode` is the main baseline for backend/runtime contract and automation parity.
5. **Secondary execution reference**: `Goose` is useful as a supplemental reference for execution modes and automation pattern parity.

## Simple V1 Checklist

1. Desktop feels polished and stable.
2. All core tools and approvals/questions/plans are understandable in the UI.
3. Multiple chats work correctly at the same time.
4. AVA can complete real coding tasks with the backend, not just toy prompts.
5. The main product flow is covered by automated tests.
6. Docs and version language clearly present this cycle as `0.6` on the path to V1.

## Archived Previous Backlog

The previous detailed backlog has been intentionally collapsed into the archive themes below.

1. Validation sweep across desktop, web, TUI, and headless with stronger smoke coverage for the real user journey.
2. Desktop hardening around session lifecycle, provider/model selection, onboarding/settings, approvals/questions/plans, queue behavior, and active-run correlation.
3. Benchmark expansion for coding, tool reliability, prompt discipline, MCP integration, LSP-adjacent flows, and product-surface smoke tests.
4. AVA-vs-OpenCode comparison work so regressions can be measured with a repeatable baseline.
5. CLI grounding and runtime-discipline work so AVA only claims tool actions and file changes that actually happened and were verified.
6. TUI-to-headless/backend parity work so interactive and non-interactive paths stay aligned on runtime behavior and tool surface.
7. Multi-run and session-attachment follow-up work across frontend and adapters, especially off-screen runs, switch-back recovery, and per-run ownership.
8. Settings cleanup and accessibility follow-up work, including provider flows, dialog behavior, and remaining shell simplification.
9. Backend contract and runtime-boundary cleanup work around queue semantics, event schemas, session continuity, and `AgentStack` ownership seams.
10. Keep local verification ergonomics healthy so staged-snapshot pre-commit checks and path-aware pre-push gates stay safe for partial commits and mixed-surface pushes.

## Out Of Scope For This Queue

1. HQ is removed from core. Any future return should be plugin-surface work, not active core backlog.
2. Historical milestone completion notes belong in `CHANGELOG.md` and architecture docs, not this backlog.
