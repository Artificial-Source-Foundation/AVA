# Competitive Analysis — AI Coding CLI Tools (March 2026)

> Comprehensive analysis of 7 open-source competitors + Pi reference baseline.
> Updated: 2026-03-05

---

## Executive Summary

AVA's unique position: the only tool with a **3-tier agent hierarchy (Praxis)** paired with an **Obsidian-style plugin ecosystem** and a **desktop-first Tauri app**. No competitor combines all three.

**Current snapshot:** ~41 tools, 30+ extensions, 16 providers, ~4,280 tests. CLI `ava agent-v2` works end-to-end.

---

## Competitor Overview

| Tool | Language | Stars | Primary Surface | Killer Feature |
|------|----------|-------|-----------------|----------------|
| **OpenCode** | TS (Bun) | 48K | TUI + Desktop | Git worktrees, 75+ providers, plugin hooks |
| **Gemini CLI** | TS (Node) | 60K | TUI | Parallel tool scheduler, free tier, A2A protocol |
| **Aider** | Python | 39K | TUI | PageRank repo map, file watcher, voice input |
| **Goose** | Rust | 27K | TUI + Desktop | MCP-first, YAML recipes, local inference |
| **OpenHands** | Python | 45K | Web UI | Docker sandbox, recursive delegation |
| **Plandex** | Go | 12K | TUI | Diff sandbox, plan branching, 2M token context |
| **Cline** | TS | 58K | VS Code | Browser automation, model-variant prompts |
| **Pi** | TS | — | TUI | Session DAG, auto-compaction, cross-provider normalization |

---

## Feature Comparison Matrix

| Feature | AVA | OpenCode | Gemini CLI | Aider | Goose | OpenHands | Plandex | Cline |
|---------|-----|----------|------------|-------|-------|-----------|---------|-------|
| **Multi-Agent Hierarchy** | **3-tier Praxis** | Subagents | A2A | ❌ | Subagents | Delegate | ❌ | Partial |
| **Plugin System** | **Full npm** | npm hooks | MCP only | ❌ | MCP only | Microagents | ❌ | MCP only |
| **Desktop App** | **Tauri** | Electron | ❌ | Gradio | Electron | Web | ❌ | ❌ |
| **Provider Count** | 16 | 75+ | 1+MCP | 100+ | 20+ | Many | Many | Many |
| **Tool Count** | ~41 | ~20 | ~15 | ~5 | MCP | ~10 | ~10 | ~15 |
| **MCP Support** | Full | Full | Full | ❌ | MCP-first | Partial | ❌ | Full |
| **LSP Integration** | 9 tools | Experimental | ❌ | ❌ | ❌ | Partial | ❌ | tree-sitter |
| **Persistent Memory** | SQLite | SQLite | GEMINI.md | ❌ | MCP | Partial | ❌ | ❌ |
| **Context Compaction** | Auto | Auto | Auto | Summary | ❌ | ❌ | Auto | Auto |
| **Parallel Tool Exec** | Partial | ❌ | **Scheduler** | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Git Worktrees** | ❌ | **Yes** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Snapshot/Undo** | ✅ | **Yes** | ❌ | git | ❌ | ❌ | Sandbox | ❌ |
| **Diff Sandbox** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **Yes** | ❌ |
| **Ripgrep Search** | ✅ | ❌ | **Yes** | ❌ | ❌ | ❌ | ❌ | **Yes** |
| **Session DAG/Tree** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | Branches | Pi: **Yes** |
| **Auto-Compaction** | ✅ | **Yes** | **Yes** | ❌ | ❌ | ❌ | **Yes** | **Yes** |
| **Voice Input** | ❌ | ❌ | ❌ | **Yes** | ❌ | ❌ | ❌ | ❌ |
| **File Watcher** | ❌ | ❌ | ❌ | **Yes** | ❌ | ❌ | ❌ | ❌ |
| **Docker Sandbox** | ❌ | ❌ | ❌ | ❌ | ❌ | **Default** | ❌ | ❌ |
| **Browser Automation** | MCP | MCP | **Playwright** | ❌ | MCP | **Playwright** | Partial | **Playwright** |
| **Repo Map** | PageRank | ❌ | ❌ | **PageRank** | ❌ | ❌ | tree-sitter | tree-sitter |
| **Recipes/Workflows** | TOML cmds | ❌ | ❌ | ❌ | **YAML** | ❌ | Plans | ❌ |
| **Cross-provider msg norm** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | Pi: **Yes** |
| **Free Tier** | ❌ | ❌ | **1000/day** | ❌ | ❌ | ❌ | ❌ | ❌ |

---

## AVA's Unique Advantages

1. **3-tier Praxis hierarchy** — Commander → 4 Leads → 8 Workers (13 agents). No competitor has a visible, configurable team hierarchy.
2. **Obsidian-style plugin ecosystem** — Full `activate(api)` pattern with registerTool, registerCommand, registerAgentMode, registerProvider, addToolMiddleware. Richer than any competitor's extension API.
3. **~41 built-in tools** — Most tools of any competitor (OpenCode has ~20, others have 5-15).
4. **Desktop-first with Tauri** — Native performance, not Electron overhead.
5. **Skills + Custom Commands** — Two distinct plugin types (auto-triggered vs explicit).
6. **Per-agent model selection** — Each agent in the hierarchy can use a different provider/model.

---

## Critical Gaps to Close

### Tier 1 — Expected by users of competing tools

| # | Gap | Source | Impact | Effort |
|---|-----|--------|--------|--------|
| CG-01 | **Auto-compaction** (DONE) | Pi, OpenCode, Gemini, Cline | CLOSED — implemented 3-tier cascade compaction | Done |
| CG-02 | **Parallel tool execution** | Gemini CLI scheduler | HIGH — 2-5x speed for multi-file ops | Medium |
| CG-03 | **Git snapshot/undo** (DONE) | OpenCode, Aider | CLOSED — implemented ghost checkpoints/snapshots | Done |
| CG-04 | **Cross-provider message normalization** | Pi `transform-messages.ts` | MEDIUM — enables mid-session model switching | Small |
| CG-05 | **Steering interrupts (skip pending tools)** | Pi agent loop | MEDIUM — user can redirect agent mid-run | Small |
| CG-06 | **Session DAG/tree** | Pi session manager | MEDIUM — non-destructive branching | Large |

### Tier 2 — Differentiators to build

| # | Gap | Source | Impact | Effort |
|---|-----|--------|--------|--------|
| CG-07 | **Plugin tool hooks** | OpenCode `Plugin.trigger("tool.definition")` | MEDIUM — plugins can mutate tool descriptions | Small |
| CG-08 | **Cross-tool SKILL.md compat** | OpenCode, Gemini CLI | MEDIUM — read `.claude/skills/`, `.agents/skills/` | Small |
| CG-09 | **Git worktree isolation** | OpenCode | MEDIUM — per-session isolated branches | Medium |
| CG-10 | **Model packs** | Plandex | LOW — curated model combos per tier | Small |
| CG-11 | **Diff sandbox / review mode** | Plandex | LOW — preview all changes before applying | Medium |
| CG-12 | **Recipe/workflow sharing** | Goose deeplinks | LOW — shareable YAML/JSON workflows | Medium |

---

## What to Learn from Each Competitor

### OpenCode — Ecosystem breadth
- 75+ providers via Vercel AI SDK — widest provider support
- Plugin hooks let extensions mutate tool descriptions before execution
- Git worktrees for session isolation — unique safety feature
- Snapshot system with scheduled pruning

### Gemini CLI — Performance
- Parallel tool scheduler runs independent calls concurrently
- Google Search grounding (native, no API key)
- A2A protocol for cross-agent interoperability
- Model-variant system prompts (different prompt per model family)

### Aider — Developer workflow
- PageRank repo map — most sophisticated codebase navigation
- File watcher with comment-driven prompts (`# aider: fix this`)
- Architect mode — 2-model workflow (planner + coder)
- Voice input for hands-free coding

### Goose — Enterprise
- MCP-first design — every capability is an MCP server
- YAML recipes shareable via deeplinks
- Rust performance — fastest startup/lowest memory
- Local inference via llama.cpp

### OpenHands — Security
- Docker sandbox by default — most secure execution model
- Recursive multi-agent delegation with state tracking
- Event-sourced state enables replay and debugging
- Kubernetes deployment for scaling

### Plandex — Review workflow
- Diff sandbox — AI changes don't touch files until `apply`
- Plan branching — version control for implementation strategies
- 2M token effective context via tree-sitter indexing
- Claude subscription support (Pro/Max credits)

### Cline — Safety
- Human-in-the-loop at every step
- Model-variant tool descriptions (change per model family)
- gRPC multi-host architecture (VS Code + JetBrains + CLI)
- Supply chain security lessons (Clinejection attack)

### Pi — Minimalism
- Session DAG with tree navigation and branch summaries
- Auto-compaction triggered by token threshold
- Steering interrupts skip remaining tool calls
- Cross-provider message normalization (tool IDs, thinking blocks)
- Dynamic API key resolution per LLM call (handles OAuth token refresh)

---

## Recommended Priority Order

```
Phase 1 (CLI Working) — Get agent-v2 reliable
  CG-01: Auto-compaction (prevent crashes)
  CG-04: Cross-provider message normalization
  CG-05: Steering interrupts
  Fix: memory extension dist, merge conflict in CLI help

Phase 2 (CLI Competitive) — Match Pi baseline
  CG-02: Parallel tool execution
  CG-03: Git snapshot/undo
  CG-07: Plugin tool hooks
  CG-08: Cross-tool SKILL.md compatibility

Phase 3 (Differentiation) — Surpass competitors
  CG-06: Session DAG/tree
  CG-09: Git worktree isolation
  CG-10: Model packs
  CG-11: Diff sandbox
  CG-12: Recipe sharing
```

---

## Sources

- OpenCode reference code: `docs/reference-code/opencode/`
- Gemini CLI reference code: `docs/reference-code/gemini-cli/`
- Aider reference code: `docs/reference-code/aider/`
- Goose reference code: `docs/reference-code/goose/`
- OpenHands reference code: `docs/reference-code/openhands/`
- Plandex reference code: `docs/reference-code/plandex/`
- Cline reference code: `docs/reference-code/cline/`
- Pi reference code: `docs/reference-code/pi-mono/`
- Web research (March 2026): OpenCode 48K stars, Gemini CLI 60K, Aider 39K, Goose 27K, OpenHands 45K, Plandex 12K, Cline 58K
