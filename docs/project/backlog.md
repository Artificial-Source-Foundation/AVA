---
title: "Backlog"
description: "Active AVA 0.6 work for the V1 push, plus an archive of the previous detailed backlog."
order: 3
updated: "2026-04-22"
---

# AVA Backlog

This backlog now tracks the active `0.6 -> V1` checklist first.

The older `3.3 / 3.3.1` planning backlog is preserved below in archived form so the current priority stays easy to read.

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

Current bounded parity note: desktop and web submit/replay command paths now both use accepted-and-streaming run-start semantics (contract-follow-up Milestone 10 resolved `EX-002`), and web submit/replay flows continue to honor the shared persisted per-run thinking/model/compaction context used by desktop session runs; the remaining bounded divergence is the manual TUI/headless `/compact` path tracked in `docs/architecture/backend-contract-exceptions.md` as `EX-003`.

Current TUI delegation note: native blocking subagents now get real child sessions at start, stream live child transcript/tool updates into the TUI during execution, and reopen from canonical child-session state instead of relying only on final reconstruction; AVA now also exposes explicit background-agent semantics for non-blocking delegation and queues finished background summaries back into the parent run, so the remaining work is mostly presentation/fit-and-finish parity rather than missing live delegated-session ownership.

Current loop-detection note: exploratory repeated use of the same search-style tool with different arguments no longer trips the old name-only repetition warning, while exact repeated calls still warn everywhere and broader repeated call-signature cycle detection (including same-tool ping-pong and `A-B-C-A-B-C` style loops) is now reserved for providers/models already marked `loop_prone`.

Current CI note: the main CI pipeline no longer exports `RUSTC_WRAPPER=sccache` into browser-smoke or security-audit jobs that do not install `sccache`, and the frontend/Rust check suites are back in sync with the current startup primary-agent loading behavior and recent clippy expectations.

Current provider-connect note: the TUI OAuth/browser-login fallback path now exposes a visible copy-to-clipboard shortcut for the full auth URL (plus browser reopen on the browser-login screen), and the TUI now keeps clipboard ownership alive after auth-URL copy on Linux so OpenAI browser/headless login no longer trips the short-lived clipboard-owner regression that was splattering multiline backend errors across the modal; long-lived TUI sessions now also refresh stale cached clipboard handles once on failure instead of requiring an app restart, and clipboard-backed image paste now surfaces real read failures instead of masking them as a generic empty-clipboard warning.

Current OpenAI provider note: the `ava-llm` OpenAI streaming path now routes normal, tools, and thinking-enabled requests through one internal helper so the central request-body builders stay easy to reason about without repeating the surrounding send/validate/SSE plumbing in three separate methods.

Current hook note: the local `pre-push` gate now keeps its fast path-aware shape while adding targeted compile smokes for touched high-risk Rust surfaces (`Cargo.toml`/workspace wiring, `src-tauri`, `crates/ava-web`, `crates/ava-config`) and stronger regression coverage for both routing logic and the installed Lefthook wrapper path.

Current subagent rework note: the backend foundation slice from `docs/architecture/subagent-rework-blueprint-m1.md` is now in place and the Milestone 2 material follow-up is closed on the backend seam (effective catalog now includes the default `subagent` alias, canonical subagent config read/write targeting lives in `ava-config`, backend-gate now runs the lighter focused backend smoke checks, and delegated smoke now asserts real `SubAgentComplete` payload fields); remaining work is the later TUI/desktop/web adoption/settings polish.

Current modularization roadmap note: the active Track Milestone 1 roadmap remains documented in `docs/architecture/agent-backend-modularization-roadmap-m1.md` (hotspots, target owner seams, sequence, risk controls, and validation gates). Track Milestone 2 backend extraction is in place for the shared control-plane seam (`crates/ava-control-plane`), Track Milestone 3 now lands a real orchestration owner seam via `crates/ava-agent-orchestration` with internal callers importing `stack`/`subagents` directly from that crate, Track Milestone 4 includes a narrow TUI boundary cleanup that separates terminal/UI event dispatch from runtime-driven event handling (`event_dispatch.rs` vs `runtime_event_dispatch.rs`) without changing runtime behavior, Track Milestone 5 now makes the side-surface boundary real by moving `ava serve` ownership into standalone `crates/ava-web` while preserving the existing `ava` CLI entrypoint, and the Track Milestone 6 lower-layer cleanup pass now routes pure control-plane contract consumers in touched `ava-web`/`ava-tui`/`src-tauri` paths to `ava-control-plane` directly instead of the `ava-agent` compatibility re-export seam (keeping only backend-only helpers on `ava-agent::control_plane::*`); follow-up Track Milestone 7 cleanup also retired the now-unneeded `ava-agent` crate-root pure control-plane re-exports so direct `ava-control-plane` ownership is explicit, follow-up Track Milestone 8 narrows `ava-agent-orchestration`'s crate-root surface to orchestration-owned `stack`/`subagents` only (runtime-core modules now imported directly from `ava-agent` inside orchestration internals), and Track Milestone 11 now removes the remaining pure `ava-agent::control_plane` compatibility shim modules so only backend-owned `events`/`sessions` helpers remain there.

Milestone namespace note: the contract-follow-up closure sequence is M10-M12, while the backend modularization roadmap uses separate **Track Milestone N** numbering.

Current session-title note: first-message auto-titling now treats both `New Chat` and legacy `New Session` placeholders as renameable defaults and the web backend no longer cements the placeholder into persisted metadata before the first real prompt, but broader conversation-list/session-polish work remains in the active desktop app fit-and-finish queue.

Current web hardening note: `ava serve` now defaults to loopback-only bind/origin exposure, token-protects sensitive session/history/status reads plus persisted plan listing/loading routes, high-risk plugin/CLI discovery + plugin route surfaces, and privileged HTTP control-plane routes (and `/ws`), redacts raw control tokens from normal logs, and still keeps broader browser-origin exposure as an explicit `--insecure-open-cors` opt-in.

Current multi-chat correctness note: overlapping frontend session switches now gate async persisted-session finalization on the winning switch/current session, so an older load finishing late cannot re-select the stale session, overwrite the visible session artifacts, or re-persist the old last-session selection after a newer switch has already won.

Current web session note: the browser fallback/session-adapter path now fails closed for backend session writes, archived-session deletion clears archived client state too, and web create/list session payloads now preserve `project_id` through the existing metadata seam so project-scoped browser lists do not silently drift.

Current runtime-isolation note: setting `AVA_PURE=1` now starts AVA without auto-loading any global or project-local power plugins, giving headless or automation runs a simple no-plugins mode without moving or uninstalling plugin directories.

Current config note: global instruction loading now reads `$XDG_CONFIG_HOME/ava/AGENTS.md`; AVA's global config/data/state/cache defaults now live under lowercase XDG app directories instead of `~/.ava`.

Current CLI note: `ava` now accepts `--cwd <path>`, and `AVA_WORKING_DIRECTORY` provides the same cwd override for automation; both set the actual runtime working directory, and AVA still infers the repository/worktree from that cwd instead of taking a separate repo-root argument.

Current primary-agent note: startup now supports first-class configurable primary agents in `config.yaml` (`primary_agents` + optional `primary_agent`) plus an explicit `--agent <id>` CLI override; resumed/session-switched TUI sessions now persist and restore both `primaryAgentId` and `primaryAgentPrompt` metadata and re-apply primary-agent prompt behavior on restore, configured startup profiles now also participate in TUI `Tab` / `Shift+Tab` cycling (falling back to the existing Build/Plan mode cycle when no primary-agent profiles are configured), and explicit `--agent` still wins over resumed session metadata so startup intent remains deterministic.

Current child-transcript note: TUI subagent transcript views now keep child-only interactions scoped correctly (thinking/tool-group expansion mutates the child transcript, not the parent), suppress the extra read-only composer strip in favor of a single footer hint row, and hide internal child-transcript system scaffolding so delegated conversations read closer to the main transcript.

Current docs note: user-facing setup/discovery docs for primary agents + subagents are now centralized in `docs/how-to/agents.md`, with targeted reference cross-links for config locations, explicit `agents.toml` -> `subagents.toml` migration guidance, canonical `subagents.toml` behavior, built-ins/defaults, `--agent`, resume/override semantics, trust gating, and `prompt_file` external prompt references.

## Milestone 1 Proof Definition

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
