# Post-v2.0 SOTA Sprint Plan

> Comprehensive plan to make AVA the state-of-the-art AI coding agent.
> Source: Deep audit of 12 competitor codebases + competitive gap analysis (2026-03-05)
> Prerequisite: v2.0 release (memory persistence, plan mode CLI, loop detection L3, per-hunk UI, Landlock)

---

## Overview

6 sprints, ~6 weeks. Ordered by impact-to-effort ratio.
Each sprint is independent — can be parallelized where noted.

---

## Sprint 11: Quick Wins & Hardening (1 week)

Small changes, big impact. All items are S effort.

| # | Feature | Source | What to build | Reference file |
|---|---------|--------|---------------|----------------|
| 1 | Tail-call tool chaining | Gemini CLI | Tool returns `TailToolCallRequest` → next tool executes without LLM round-trip. Saves tokens + latency on sequential ops | `gemini-cli/packages/core/src/tools/` |
| 2 | Env variable denylist | Goose | Block 31 dangerous env vars (PATH, LD_PRELOAD, HOME, etc) before shell execution | `goose/crates/goose/src/agents/tool_inspection.rs` |
| 3 | MOIM per-turn context | Goose | Inject ephemeral context each turn (CWD, timestamp, token %, active extensions) without bloating history | `goose/crates/goose/src/agents/` |
| 4 | SmartApprove | Goose | LLM classifies tool as read-only → auto-approve without user prompt. Reduces approval fatigue | `goose/crates/goose/src/agents/tool_inspection.rs` |
| 5 | Cross-provider msg normalization | Pi Mono | Handle thinking blocks, orphaned tool results, tool ID mismatches across providers | `pi-mono/packages/ai/src/providers/transform-messages.ts` |
| 6 | Plugin tool hooks | OpenCode | Plugins can mutate tool descriptions before registration | `opencode/packages/opencode/src/tool/` |
| 7 | Cross-tool SKILL.md compat | OpenCode/Gemini | Read `.claude/skills/`, `.agents/skills/`, `GEMINI.md` files | `opencode/packages/opencode/src/config/` |
| 8 | Parallel tool execution | Gemini CLI | Run independent tool calls concurrently instead of sequentially | `gemini-cli/packages/core/src/tools/` |

---

## Sprint 12: Edit Excellence II (1 week)

Directly improves edit success rate. Can run parallel with Sprint 11.

| # | Feature | Source | What to build | Reference file |
|---|---------|--------|---------------|----------------|
| 1 | Concurrent edit race | Plandex | Race 4 edit strategies in parallel (auto-apply, fast-apply, validation loop, whole-file). First valid wins | `plandex/app/server/model/plan/build.go` |
| 2 | Streaming fuzzy matcher | Zed | Incremental Levenshtein matching as tokens arrive. 80% match threshold. Start matching before full old_text | `zed/crates/agent/src/edit_agent/streaming_fuzzy_matcher.rs` |
| 3 | External editor for tool args | Gemini CLI | ModifiableDeclarativeTool — user can edit tool arguments in $EDITOR before execution | `gemini-cli/packages/core/src/tools/` |
| 4 | Auto-formatting detection | Cline | Detect formatter-induced changes, report back to model to prevent cascading match failures | `cline/src/core/task/tools/handlers/` |
| 5 | 4-pass patch matcher | Cline | Levenshtein (66% threshold) → rstrip → trim-both → Unicode normalization in sequence | `cline/src/core/diff/` |
| 6 | Windowed file editing | SWE-agent | Never show entire file to agent — 100-line viewing window with scroll. Reduces context usage | `swe-agent/sweagent/tools/` |

---

## Sprint 13: Agent Intelligence (1 week)

Makes the agent smarter and more resilient.

| # | Feature | Source | What to build | Reference file |
|---|---------|--------|---------------|----------------|
| 1 | Steering interrupts | Pi Mono | `skipToolCall()` + inject follow-up messages mid-execution without restarting | `pi-mono/packages/coding-agent/src/core/` |
| 2 | Reviewer agent loop | SWE-agent | Second LLM validates agent output before returning to user | `swe-agent/sweagent/agent/` |
| 3 | Model fallback chain | Plandex | Switch to larger-context model on overflow instead of truncating (Claude → Gemini 2.5 Pro) | `plandex/app/server/model/` |
| 4 | Progressive error escalation | Cline | Context-aware error messages. After N consecutive failures, force strategy switch | `cline/src/core/task/tools/handlers/` |
| 5 | StuckDetector 5 scenarios | OpenHands | Detect: repeated pairs, repeated errors, monologues, alternating actions, context window | `openhands/controller/stuck.py` |
| 6 | Architect mode | Aider | 2-model workflow — expensive model plans, cheap model executes | `aider/aider/coders/` |

---

## Sprint 14: Safety & Infrastructure (1-2 weeks)

Foundational improvements. Some items are L effort.

| # | Feature | Source | What to build | Reference file |
|---|---------|--------|---------------|----------------|
| 1 | Diff sandbox / review pipeline | Plandex | All changes stored server-side. User reviews + approves before filesystem touch | `plandex/app/server/model/plan/build.go` |
| 2 | Conseca dynamic policies | Gemini CLI | Phase 1: LLM generates least-privilege policy from prompt. Phase 2: second LLM enforces per tool call | `gemini-cli/packages/core/src/safety/conseca/` |
| 3 | Event-sourced architecture | OpenHands | Full event replay with pub/sub. Enables time-travel debugging and deterministic re-execution | `openhands/events/` |
| 4 | Dynamic provider loading | OpenCode | Bundled SDKs + external registry (models.dev) + npm install at runtime. 75+ providers | `opencode/packages/opencode/src/provider/` |
| 5 | Shadow git snapshots | OpenCode | Isolated rollback repos at `$DATA_DIR/snapshot/$PROJECT_ID`. No project history pollution | `opencode/packages/opencode/src/session/snapshot/` |
| 6 | 3-layer inspection pipeline | Goose | Security → Permission → Repetition inspectors with typed result merging + escalation-only | `goose/crates/goose/src/agents/tool_inspection.rs` |

---

## Sprint 15: UX & Desktop Polish (1 week)

Desktop experience improvements. Can run parallel with Sprint 14.

| # | Feature | Source | What to build | Reference file |
|---|---------|--------|---------------|----------------|
| 1 | Differential TUI rendering | Pi Mono | Synchronized ANSI output for flicker-free terminal updates | `pi-mono/packages/coding-agent/src/` |
| 2 | Web trajectory inspector | SWE-agent | Browser-based viewer for full agent session replay with cost/exit analysis | `swe-agent/sweagent/` |
| 3 | Model-variant system prompts | Gemini CLI | Different system prompt per model family (Claude vs Gemini vs GPT) | `gemini-cli/packages/core/src/` |
| 4 | 9 model roles | Plandex | Different models per role (planner, coder, namer, committer, summarizer) with fallback chains | `plandex/app/server/model/` |
| 5 | File watcher with comment prompts | Aider | Watch files for `# ava: fix this` comments, auto-trigger agent | `aider/aider/watch.py` |
| 6 | Recipe/workflow sharing | Goose | Shareable YAML/JSON workflows via deeplinks | `goose/crates/goose/src/` |

---

## Sprint 16: Ecosystem & Interop (1 week)

Ecosystem plays. Lowest urgency, highest long-term value.

| # | Feature | Source | What to build | Reference file |
|---|---------|--------|---------------|----------------|
| 1 | MCP server mode | Zed | Expose AVA's tools to other MCP clients (VS Code, other agents) | `zed/crates/agent/src/native_agent_server.rs` |
| 2 | A2A protocol | Gemini CLI | Agent-to-agent interoperability. AVA can call/be called by other agents | `gemini-cli/packages/a2a-server/src/` |
| 3 | Tab autocomplete | Continue | Inline edit suggestions in editor (needs editor integration) | `continue/core/autocomplete/` |
| 4 | Session DAG/tree | Pi Mono | Non-destructive session branching with tree navigation and branch summaries | `pi-mono/packages/coding-agent/src/` |
| 5 | Model packs | Plandex | Curated model combos per tier (budget/balanced/premium) | `plandex/app/server/model/` |
| 6 | gRPC multi-host | Cline | Architecture for VS Code + JetBrains + CLI sharing one backend | `cline/src/core/` |

---

## Parallelization

```
Week 1:  Sprint 11 (Quick Wins)        ─┐ parallel
         Sprint 12 (Edit Excellence II) ─┘
Week 2:  Sprint 13 (Agent Intelligence)
Week 3:  Sprint 14 (Safety & Infra)     ─┐ parallel
         Sprint 15 (UX & Desktop)       ─┘
Week 4:  Sprint 16 (Ecosystem)
```

Total: ~4 weeks with parallelization, ~6 weeks sequential.

---

## Feature Count

| Sprint | Features | Effort |
|--------|----------|--------|
| 11 | 8 | All S |
| 12 | 6 | Mix S/M |
| 13 | 6 | Mix S/M |
| 14 | 6 | Mix M/L |
| 15 | 6 | Mix S/M |
| 16 | 6 | Mix M/L |
| **Total** | **38** | |

---

## After All Sprints

AVA will have every significant feature from every competitor:
- Aider's PageRank + architect mode + file watcher
- Zed's streaming diff + per-hunk UI + MCP server
- Gemini CLI's parallel execution + Conseca + A2A + tail-call chaining
- Goose's SmartApprove + MOIM + env denylist + inspection pipeline
- OpenCode's dynamic providers + shadow snapshots + plugin hooks
- OpenHands's event sourcing + 5-scenario stuck detection
- Pi Mono's steering interrupts + cross-provider normalization + TUI rendering
- Plandex's concurrent edits + diff sandbox + model roles + fallback chains
- Cline's progressive escalation + auto-format detection + 4-pass matcher
- SWE-agent's reviewer loop + trajectory inspector
- Continue's tab autocomplete

Plus AVA's unique advantages no competitor has:
- 3-tier Praxis agent hierarchy
- Richest extension API (8 methods + middleware priorities)
- 55+ tools (most comprehensive)
- Cross-session FTS5 recall
- Desktop-first Tauri app with Rust hotpaths
