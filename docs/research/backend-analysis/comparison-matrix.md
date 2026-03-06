# AVA vs Competitors: Comprehensive Comparison Matrix

> Generated from deep competitor audit of 12 AI coding tools.
> Last updated: 2026-03-05
> Based on: Aider, Cline, Codex CLI, Continue, Gemini CLI, Goose, OpenCode, OpenHands, Pi Mono, Plandex, SWE Agent, Zed

---

## Table of Contents

1. [Tool Comparison Matrix](#1-tool-comparison-matrix)
2. [Architecture Comparison](#2-architecture-comparison)
3. [Feature Gap Analysis](#3-feature-gap-analysis)
4. [What AVA Leads](#4-what-ava-leads)
5. [What AVA Matches](#5-what-ava-matches)
6. [What AVA Trails](#6-what-ava-trails)
7. [Recommended Next Actions](#7-recommended-next-actions)

---

## 1. Tool Comparison Matrix

### 1.1 Edit System

| Tool Category | AVA | Aider | Cline | Codex CLI | Continue | Gemini CLI | Goose | OpenCode | OpenHands | Plandex | SWE Agent | Zed | Pi Mono |
|---------------|-----|-------|-------|-----------|----------|------------|-------|----------|-----------|---------|-----------|-----|---------|
| **Edit strategies** | 8 | **10** | 3 | 1 | 2 | 2 | 2 | 3 | 2 | 4 | 3 | 2 | 1 |
| **Fuzzy matching** | ✅ | **✅ Multi-strategy** | ✅ 3-tier | ✅ 4-pass | ✅ 9 replacers | ✅ 4-tier | ✅ | ✅ 9 replacers | ✅ | ✅ race | ✅ window | ✅ streaming | ✅ 2-tier |
| **Streaming edits** | ✅ | ❌ | ✅ | ❌ | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **✅ StreamingDiff** | ❌ |
| **Self-correction** | ✅ | ✅ 3 reflections | ✅ | ✅ | ✅ LSP | ✅ LLM fix | ✅ | ✅ LSP | ✅ | ❌ | ✅ lint | ❌ | ❌ |
| **Per-hunk accept/reject** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **✅** | ❌ |
| **Relative indentation** | ✅ | **✅ RelativeIndenter** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ reindent | ❌ |
| **Unicode normalization** | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ | ✅ | ❌ | ❌ | ❌ | ❌ | **✅ NFC/NFD** |

### 1.2 Context & Memory

| Tool Category | AVA | Aider | Cline | Codex CLI | Continue | Gemini CLI | Goose | OpenCode | OpenHands | Plandex | SWE Agent | Zed | Pi Mono |
|---------------|-----|-------|-------|-----------|----------|------------|-------|----------|-----------|---------|-----------|-----|---------|
| **Context compaction** | ✅ token | **✅ Background LLM** | ✅ auto-condense | ✅ rollup | ✅ summarize | ✅ | ✅ MOIM | ✅ auto | **✅ 9 strategies** | ✅ gradual | ❌ | ❌ none | ✅ auto |
| **Repo map** | ✅ PageRank | **✅ PageRank** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ tree-sitter | ❌ | ❌ | ❌ |
| **Token budget** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ 1M | ✅ | ✅ | ✅ | ✅ 2M | ✅ | ❌ | ✅ |
| **Session branching** | ✅ DAG | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ tree | ❌ | **✅ plan branches** | ❌ | ❌ | **✅ DAG** |
| **Cross-session search** | ✅ FTS5 recall | ❌ | ❌ | ❌ | ❌ | ✅ memory | ✅ chatrecall | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Context providers** | ❌ | ❌ | ❌ | ❌ | **✅ 30+** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **History processors** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **✅ 7 types** | ❌ | ❌ | ❌ | ❌ |

### 1.3 Agent Loop & Reliability

| Tool Category | AVA | Aider | Cline | Codex CLI | Continue | Gemini CLI | Goose | OpenCode | OpenHands | Plandex | SWE Agent | Zed | Pi Mono |
|---------------|-----|-------|-------|-----------|----------|------------|-------|----------|-----------|---------|-----------|-----|---------|
| **Turn limit** | ✅ | ✅ 3 reflections | ❌ | ✅ | ❌ | ✅ 100 | ✅ 1000 | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Stuck detection** | ✅ doom loop | ❌ | ❌ | ❌ | ❌ | **✅ 3-layer** | ✅ repetition | ✅ doom loop | **✅ 5 scenarios** | ❌ | ✅ | ❌ | ❌ |
| **Multi-agent** | **✅ 13 agents** | ✅ architect | ✅ 5 subagents | ✅ spawn | ❌ | ✅ A2A | ❌ | ✅ 7 agents | ✅ delegate | ❌ | ❌ | ✅ 1 subagent | ❌ |
| **Agent hierarchy** | **✅ 3-tier** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Loop detection** | ✅ | ❌ | ❌ | ❌ | ❌ | **✅ 3-layer** | ✅ | ✅ | **✅ 5 scenarios** | ❌ | ❌ | ❌ | ❌ |
| **Completion detection** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |

### 1.4 Safety & Permissions

| Tool Category | AVA | Aider | Cline | Codex CLI | Continue | Gemini CLI | Goose | OpenCode | OpenHands | Plandex | SWE Agent | Zed | Pi Mono |
|---------------|-----|-------|-------|-----------|----------|------------|-------|----------|-----------|---------|-----------|-----|---------|
| **Docker sandbox** | ✅ optional | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **✅ default** | ❌ | ✅ | ❌ | ❌ optional |
| **OS-level sandbox** | ❌ | ❌ | ❌ | **✅ Seatbelt/Landlock** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Permission system** | ✅ middleware | ❌ | ✅ 3-tier | ✅ 5-type | ✅ YAML | ✅ 5-tier | ✅ 3-layer | ✅ wildcard | ✅ analyzers | ✅ RBAC | ✅ | ✅ regex | ❌ |
| **Git checkpoints** | ✅ snapshots | ✅ auto-commits | ✅ shadow | **✅ ghost** | ❌ | ❌ | ❌ | ✅ shadow | ❌ | ✅ per-plan | ❌ | ❌ | ❌ |
| **Command filtering** | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ |
| **Network isolation** | ❌ | ❌ | ❌ | **✅ proxy** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |

### 1.5 UX & Developer Experience

| Tool Category | AVA | Aider | Cline | Codex CLI | Continue | Gemini CLI | Goose | OpenCode | OpenCode | OpenHands | Plandex | SWE Agent | Zed | Pi Mono |
|---------------|-----|-------|-------|-----------|----------|------------|-------|----------|----------|-----------|---------|-----------|-----|-----|
| **Desktop app** | **✅ Tauri** | ❌ | ❌ | ❌ | ❌ | ❌ | **✅ Tauri** | ❌ | ❌ | ❌ | ❌ | ❌ | **✅ Native GPU** | ❌ |
| **Streaming** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | **✅ diff** | ✅ |
| **Diff display** | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ❌ | **✅ per-hunk** | ✅ |
| **Session management** | ✅ DAG | ❌ | ❌ | ✅ | ❌ | ❌ | ✅ | ✅ tree | ✅ | ❌ | ✅ | ✅ | ❌ | **✅ DAG** |
| **MCP client** | ✅ | ❌ | ✅ | ❌ | ✅ | ✅ | **✅ native** | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ |
| **MCP server** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **✅** | ❌ |
| **Browser automation** | ✅ MCP | ❌ | ✅ Puppeteer | ❌ | ❌ | ✅ CDP | ❌ | ❌ | **✅ BrowserGym** | ❌ | ❌ | ❌ | ❌ |
| **Autocomplete** | ❌ | ❌ | ❌ | ❌ | **✅** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |

### 1.6 Unique Features

| Tool Category | AVA | Aider | Cline | Codex CLI | Continue | Gemini CLI | Goose | OpenCode | OpenHands | Plandex | SWE Agent | Zed | Pi Mono |
|---------------|-----|-------|-------|-----------|----------|------------|-------|----------|-----------|---------|-----------|-----|---------|
| **PageRank repo map** | ✅ | **✅** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **StreamingDiff** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **✅** | ❌ |
| **Per-hunk accept/reject** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **✅** | ❌ |
| **9 condenser strategies** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **✅** | ❌ | ❌ | ❌ | ❌ |
| **History processors** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **✅** | ❌ | ❌ | ❌ | ❌ |
| **Action samplers** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **✅** | ❌ | ❌ |
| **Reviewer agent** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **✅** | ❌ | ❌ |
| **Ghost snapshots** | ✅ | ❌ | ❌ | **✅** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Conseca** | ❌ | ❌ | ❌ | ❌ | ❌ | **✅** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **A2A protocol** | ❌ | ❌ | ❌ | ❌ | ❌ | **✅** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| **Concurrent builds** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **✅** | ❌ | ❌ | ❌ |
| **Session DAG** | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ | ❌ | **✅** |
| **Steering interrupts** | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | **✅** |

---

## 2. Architecture Comparison

### 2.1 Agent Loop Pattern

| Project | Pattern | Key Details |
|---------|---------|-------------|
| **AVA** | **Extension-based middleware** | Hooks at priority levels, 55+ tools, Praxis 3-tier hierarchy |
| Aider | Chat-and-parse | 10 edit formats, fuzzy matching, 3 reflection limit |
| Cline | Monolithic Task class | 3,547 lines, approval gates, subagent spawning |
| Codex CLI | Turn-based Rust | ~10K lines, 12 safety subsystems, multi-agent |
| Continue | GUI-driven Redux thunks | IDE-agnostic, 3-process model |
| Gemini CLI | Event-driven scheduler | State machine, parallel batches, tail calls |
| Goose | Turn-based with MOIM | MCP-first, 3-layer inspection |
| OpenCode | Custom outer loop | Vercel AI SDK, flat agents, hooks system |
| OpenHands | Event-sourced | 9 condensers, Docker default, stuck detector |
| Plandex | Tell/build pipeline | Server-side, diff sandbox, 9 model roles |
| SWE Agent | ACI windowed | 100-line windows, history processors, reviewers |
| Zed | Thread-based | StreamingDiff, per-hunk UI, MCP dual role |
| Pi Mono | 3-layer loop | ai → agent → coding-agent, DAG sessions |

### 2.2 Edit Strategy

| Project | Strategies | Best-in-Class Features |
|---------|------------|------------------------|
| **AVA** | **8 strategies** | Fuzzy, line-range, regex, block, indent-aware |
| Aider | **10 formats** | RelativeIndenter, git cherry-pick fallback |
| Cline | 3 | 3-tier fuzzy matching, progressive error escalation |
| Codex CLI | 1 | Ghost snapshots for rollback |
| Continue | 2 | Streaming diff application |
| Gemini CLI | 2 | 4-tier cascade, LLM self-correction |
| Goose | 2 | MCP-native, unique match enforcement |
| OpenCode | 3 | 9 replacer cascade, LSP diagnostics |
| OpenHands | 2 | Windowed edit, lint gating |
| Plandex | 4 | Concurrent build race, tree-sitter validation |
| SWE Agent | 3 | 100-line windows, flake8 gating |
| Zed | 2 | **StreamingDiff**, per-hunk accept/reject |
| Pi Mono | 1 | NFC/NFD path normalization |

### 2.3 Context / Token Management

| Project | Strategy | Key Details |
|---------|----------|-------------|
| **AVA** | **Token tracking + compaction** | Prune strategy, extension-based |
| Aider | **PageRank repo map** | Graph-based relevance, binary search fit |
| Cline | Deleted-range truncation | Preserves first pair, auto-condense |
| Codex CLI | Rollup/compaction | Ghost snapshots, 2-phase memory |
| Continue | Conversation compaction | LLM-generated summaries |
| Gemini CLI | 1M native context | Curated/comprehensive dual views |
| Goose | MOIM injection | Per-turn ephemeral context |
| OpenCode | Auto-compaction | Protected-tool compaction |
| OpenHands | **9 condenser strategies** | Most sophisticated |
| Plandex | Model fallback chain | 2M tokens, gradual summarization |
| SWE Agent | History processors | 7 types, trajectory logging |
| Zed | No compaction | Full thread kept, zstd compression |
| Pi Mono | Auto-compaction | Structured summary, DAG tree |

### 2.4 Session Management

| Project | Format | Key Features |
|---------|--------|--------------|
| **AVA** | **JSON DAG** | Branching, FTS5 recall |
| Aider | In-memory + git | Auto-commits, markdown log |
| Cline | JSON | Task-based, VS Code state |
| Codex CLI | JSONL | Session resume, ghost snapshots |
| Continue | JSON | IDE-managed |
| Gemini CLI | JSON | Memory save/load |
| Goose | SQLite | MOIM, recipes |
| OpenCode | JSONL tree | Drizzle ORM, branching |
| OpenHands | EventStream | Full replay, 9 condensers |
| Plandex | PostgreSQL | Plan branches, git repos |
| SWE Agent | JSON trajectories | `.traj` files, replay |
| Zed | SQLite | zstd compression, thread-based |
| Pi Mono | JSONL DAG | Tree navigation, branching |

### 2.5 Permission / Safety Model

| Project | Approach | Key Details |
|---------|----------|-------------|
| **AVA** | **Middleware pipeline** | Priority levels, Docker optional |
| Aider | Git-based | Auto-commits, explicit shell confirmation |
| Cline | 3-tier approval | Human-in-the-loop, shadow git |
| Codex CLI | **OS-level sandbox** | Seatbelt/Landlock/seccomp, 12 subsystems |
| Continue | YAML policy | Terminal security evaluator |
| Gemini CLI | 5-tier TOML | Conseca dynamic policies |
| Goose | 3-layer inspection | Security, Permission, Repetition |
| OpenCode | Wildcard patterns | Tree-sitter bash, worktrees |
| OpenHands | Docker default | 3 security analyzers |
| Plandex | RBAC + diff sandbox | Server-side, explicit apply |
| SWE Agent | SWE-ReX remote | Containerized execution |
| Zed | Regex + path checks | Hardcoded rules, MCP double-gate |
| Pi Mono | None by default | Optional extensions |

### 2.6 Extension / Plugin System

| Project | Architecture | Key Details |
|---------|--------------|-------------|
| **AVA** | **ExtensionAPI** | 8 methods, hooks, middleware, validators |
| Aider | None | `.aider.conf.yml` only |
| Cline | VS Code hooks | @mentions, MCP client |
| Codex CLI | Starlark rules | `.rules` files |
| Continue | YAML config | Context providers, MCP |
| Gemini CLI | Multi-tool extensions | Hooks, skills |
| Goose | **MCP-native** | Extensions ARE MCP servers |
| OpenCode | npm + hooks | 15+ hook points |
| OpenHands | Microagents | Trigger-based |
| Plandex | None | Model packs only |
| SWE Agent | Tool bundles | Bash script groups |
| Zed | Agent profiles | MCP client+server |
| Pi Mono | Extension SDK | Lifecycle hooks |

### 2.7 Provider Count & Support

| Project | Count | Notable |
|---------|-------|---------|
| **AVA** | **16** | Anthropic, OpenAI, Google, Azure, AWS, etc. |
| Aider | 20+ | Via litellm |
| Cline | 40+ | Most variety |
| Codex CLI | 1 | OpenAI only |
| Continue | 30+ | Via openai-adapters |
| Gemini CLI | 1 | Gemini only |
| Goose | 20+ | Via Rust LLM SDK |
| OpenCode | **75+** | Via Vercel AI SDK + models.dev |
| OpenHands | 20+ | Via litellm |
| Plandex | ~10 | Via LiteLLM sidecar |
| SWE Agent | ~5 | Via litellm |
| Zed | 14 | Native integrations |
| Pi Mono | 22 | Via 9 API protocols |

---

## 3. Feature Gap Analysis

### P0 — Table Stakes (AVA has all)

| Feature | Competitors | Status |
|---------|-------------|--------|
| File read/write/edit | 12/12 | **Done** — 8 edit strategies |
| Shell execution | 11/12 | **Done** — bash + PTY + background |
| Glob/grep search | 10/12 | **Done** — glob + grep |
| MCP client | 7/12 | **Done** — stdio, SSE, HTTP |
| Multi-provider | 11/12 | **Done** — 16 providers |
| Extension system | 8/12 | **Done** — ExtensionAPI |
| Context management | 9/12 | **Done** — compaction + prune |

### P1 — Competitive Advantages

| Feature | Competitors | Status | Gap? |
|---------|-------------|--------|------|
| Multi-agent hierarchy | 5/12 (Cline, Codex, OpenCode, OpenHands) | **Done** — Praxis 3-tier | No |
| Docker sandbox | 4/12 (Codex, OpenHands, Plandex) | **Partial** — optional | Minor |
| Tab autocomplete | 1/12 (Continue) | **Missing** | **Gap** |
| Streaming edits | 2/12 (Continue, **Zed**) | **Partial** | **Gap** |
| Per-hunk accept/reject | 1/12 (**Zed**) | **Missing** | **Gap** |
| OS-level sandbox | 1/12 (**Codex CLI**) | **Missing** | **Gap** |
| PageRank repo map | 1/12 (**Aider**) | **Partial** | **Gap** |
| 9 condenser strategies | 1/12 (**OpenHands**) | **Partial** (1) | **Gap** |
| History processors | 1/12 (**SWE Agent**) | **Missing** | **Gap** |
| Action samplers | 1/12 (**SWE Agent**) | **Missing** | **Gap** |
| Conseca dynamic policies | 1/12 (**Gemini CLI**) | **Missing** | **Gap** |
| A2A protocol | 1/12 (**Gemini CLI**) | **Missing** | **Gap** |
| Concurrent builds | 1/12 (**Plandex**) | **Missing** | **Gap** |
| Session DAG branching | 2/12 (OpenCode, **Pi Mono**) | **Done** — DAG | No |

### P2 — Unique Differentiators to Build

| Feature | Who Has It | Priority |
|---------|------------|----------|
| **StreamingDiff** | Zed | High |
| **Per-hunk accept/reject** | Zed | High |
| **OS-level sandboxing** | Codex CLI | Medium |
| **9 condenser strategies** | OpenHands | Medium |
| **History processors** | SWE Agent | Medium |
| **Action samplers** | SWE Agent | Low |
| **Conseca** | Gemini CLI | Medium |
| **A2A protocol** | Gemini CLI | Low |
| **Concurrent builds** | Plandex | Medium |
| **PageRank repo map** | Aider | High |
| **3-layer loop detection** | Gemini CLI | High |

---

## 4. What AVA Leads

1. **Most Comprehensive Tool Suite (55+)** — More than any competitor
2. **Richest Extension API** — 8 methods, hooks, middleware, validators
3. **Most Structured Multi-Agent System** — Praxis 3-tier hierarchy with 13 agents
4. **LSP as Agent Tools** — 9 LSP tools exposed to agent
5. **Session Recall Across Conversations** — FTS5 full-text search
6. **Desktop-Native with Full Backend** — Tauri + 55+ tools
7. **Edit Strategy Breadth with Flexibility** — 8 strategies, per-model config
8. **Unified Hooks + Middleware** — Dual architecture unique to AVA

---

## 5. What AVA Matches

| Capability | Competitor | Status |
|------------|------------|--------|
| Multiple edit strategies | Aider (10), OpenCode (9 replacers) | Parity |
| Fuzzy matching | Aider, Cline, Codex, Gemini, Zed | Parity |
| Context compaction | Aider, Cline, Gemini, Goose, OpenCode, OpenHands, Pi Mono | Parity |
| Docker sandbox | OpenHands (default), SWE Agent | Parity (optional) |
| MCP support | Goose (native), Cline, Continue, Gemini, Zed | Parity |
| Multi-provider | OpenCode (75+), Cline (40+) | Partial (should expand) |
| Session branching | OpenCode, Pi Mono | Parity |
| Streaming output | Aider, Cline, Codex, Gemini, Goose, Zed | Parity |
| Error recovery | Aider, Cline, Codex, Gemini, OpenCode | Parity |

---

## 6. What AVA Trails

### High Priority Gaps

1. **StreamingDiff** (Zed) — Apply edits as LLM streams, not after
2. **Per-hunk accept/reject** (Zed) — Granular change review UI
3. **PageRank repo map** (Aider) — Graph-based code relevance
4. **3-layer loop detection** (Gemini CLI) — Hash + chanting + LLM judge
5. **9 condenser strategies** (OpenHands) — Only 1 currently
6. **OS-level sandboxing** (Codex CLI) — Seatbelt/Landlock/seccomp
7. **Tab autocomplete** (Continue) — Inline edit suggestions

### Medium Priority Gaps

8. **History processors** (SWE Agent) — Chain-of-responsibility transforms
9. **Conseca dynamic policies** (Gemini CLI) — LLM-generated security
10. **Concurrent builds** (Plandex) — Parallel edit strategies
11. **Action samplers** (SWE Agent) — Best-of-N selection
12. **A2A protocol** (Gemini CLI) — Agent-to-agent interoperability
13. **Ghost snapshots** (Codex CLI) — Invisible git commits
14. **MCP server mode** (Zed) — Expose AVA tools to other agents

### Lower Priority Gaps

15. **Reviewer agent** (SWE Agent) — LLM validates outputs
16. **Windowed editing** (SWE Agent) — 100-line viewing window
17. **SmartApprove** (Goose) — LLM classifies read-only tools
18. **Cross-provider normalization** (Pi Mono) — Handle thinking blocks, tool IDs

---

## 7. Recommended Next Actions

### Immediate (P0)

| # | Action | Reference | Effort |
|---|--------|-----------|--------|
| 1 | **Implement StreamingDiff** | Zed: `crates/streaming_diff/src/streaming_diff.rs` | Large |
| 2 | **Add per-hunk accept/reject UI** | Zed: `crates/agent_ui/src/buffer_codegen.rs` | Large |
| 3 | **Upgrade to PageRank repo map** | Aider: `aider/repomap.py` | Medium |
| 4 | **Implement 3-layer loop detection** | Gemini CLI: `packages/core/src/services/loopDetectionService.ts` | Medium |

### Short-term (P1)

| # | Action | Reference | Effort |
|---|--------|-----------|--------|
| 5 | **Add 3-4 more condenser strategies** | OpenHands: `openhands/memory/condenser/impl/` | Medium |
| 6 | **Implement OS-level sandbox option** | Codex CLI: `codex-rs/core/src/seatbelt.rs`, `landlock.rs` | Large |
| 7 | **Add history processor pipeline** | SWE Agent: `sweagent/agent/history_processors.py` | Medium |
| 8 | **Implement action samplers** | SWE Agent: `sweagent/agent/action_sampler.py` | Medium |

### Medium-term (P2)

| # | Action | Reference | Effort |
|---|--------|-----------|--------|
| 9 | **Add Conseca-like dynamic policies** | Gemini CLI: `packages/core/src/safety/conseca/` | Medium |
| 10 | **Implement concurrent builds** | Plandex: `app/server/model/plan/build.go` | Medium |
| 11 | **Add MCP server mode** | Zed: `crates/agent/src/native_agent_server.rs` | Medium |
| 12 | **Explore A2A protocol** | Gemini CLI: `packages/a2a-server/src/` | Low |

---

*Updated comparison matrix based on deep audit of 12 competitor codebases (March 2026)*
