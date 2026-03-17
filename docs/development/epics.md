# AVA Epics

> Last updated: 2026-03-16
> Related: [roadmap.md](roadmap.md), [backlog.md](backlog.md)

## Planned Epics

### E10: Plugin System (v3.0)

Target: v3.0 release. AVA's next major milestone.

| Phase | Scope | Status |
|-------|-------|--------|
| Phase 1 | Plugin runtime (`ava-plugin` crate, trait, isolation, TOML config) | Not started |
| Phase 2 | Plugin SDK, `ava plugin install`, registry, versioning | Not started |
| Phase 3 | Marketplace, verified publishers, templates, OpenCode compat bridge | Not started |

Related backlog: B46 (marketplace), B55/B56/B72/B77 (plugin-first capabilities).

## Recently Completed Epics

### E11: Dead Code Cleanup (v2.1.1) -- COMPLETE

Removed 30 unwired modules (~10.5K lines) and archived design docs to `docs/ideas/`.

### E12: Documentation Overhaul (v2.1.1) -- COMPLETE

Refreshed CLAUDE.md, AGENTS.md, README, crate docs. Added plugin design research. Rebuilt `docs/development/` as project management hub.

### E13: Security Hardening (v2.1.1) -- COMPLETE

11 security commits covering: sandbox hardening, permission hardening, config hardening, auth hardening, symlink escape fix, trust gates, MCP env filtering, tool tracing.

### E14: Backend Gaps (v2.1.1) -- COMPLETE

Filled competitive gaps from deep scrape analysis: conversation tree/branching (BG-10), session bookmarks (BG-13), LiteLLM compat (BG-14), named agents (BG-11), tool output disk fallback (BG-3/4), pruning (BG-5), ghost revert (BG-6), compaction improvements (BG-7/8), branch summarization (BG-9), direction-aware truncation (BG-12), secret redaction, repetition inspector, turn diff tracker, focus chain, tool call repair.

## Completed v3 Epics (Sprints 60-66)

### E3: Streaming and Session UX (Sprint 60) -- COMPLETE

Streaming tool calls, session/context UX, project instructions, three-tier mid-stream messaging, workflow polish.

### E4: Reliable Edit Loop (Sprint 61) -- COMPLETE

RelativeIndenter, auto lint+test, smart `/commit`, ghost snapshots.

### E5: Cost and Runtime Controls (Sprint 62) -- COMPLETE

Thinking budgets, dynamic API keys, cost-aware routing, budget alerts. Validated via Sprint 62V.

### E6: Execution and Ecosystem Foundations (Sprint 63) -- COMPLETE

Pluggable backend ops (B65), background agents on branches (B39), dev tooling (B61), skill discovery (B71), file watcher (B45).

### E7: Knowledge and Context Intelligence (Sprint 64) -- COMPLETE

Auto-learned memories (B38), multi-repo context (B57), semantic indexing (B58), change impact analysis (B48).

### E8: Agent Coordination Backend (Sprint 65) -- COMPLETE

Spec-driven dev (B49), agent artifacts (B59), agent team peer comm (B50), ACP (B76).

### E9: Optional Capability Backends (Sprint 66) -- COMPLETE

Web search (B44), AST ops (B52), LSP ops (B53), code search (B69). All Extended tier.

## Completed Foundation Epics (Sprints 11-59)

### E1: Dynamic Model Intelligence (Sprints 53-55, 59) -- COMPLETE

Dynamic model catalog, thinking/reasoning modes, 7 coding plan providers, Copilot provider, compiled-in registry, rich StreamChunk.

### E2: Codebase Quality (Sprints 56-58) -- COMPLETE

Quality audit, unwrap/panic fixes, modal system revamp with shared SelectList widget.

### Foundation (Sprints 11-50f) -- COMPLETE

55 sprints covering: Rust agent stack, TUI, credentials, Praxis multi-agent, CLI providers, agent loop intelligence, MCP, TOML plugins, context/memory, performance, safety, code review, voice input, stabilization, and v2.1 release.
