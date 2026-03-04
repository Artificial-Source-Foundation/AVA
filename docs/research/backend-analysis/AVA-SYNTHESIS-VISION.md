# AVA Synthesis Vision: Combining the Best of All Worlds

> Consolidated analysis of 12 AI coding agents to define AVA's ultimate form.
> Based on deep backend analysis of: Goose, OpenCode, Cline, Aider, Codex CLI, Gemini CLI, Continue, OpenHands, SWE-agent, Zed, Pi Mono, Plandex

---

## Executive Summary

After analyzing 12 major AI coding tools at the backend architecture level, AVA is already **competitive or leading** in most categories:

- **55+ tools** — more than any competitor (next closest: Cline ~25)
- **3-tier Praxis hierarchy** — most sophisticated multi-agent system
- **Richest ExtensionAPI** — 8 registration methods, middleware, hooks, validators
- **9 LSP tools** — unique among standalone agents
- **FTS5 session recall** — cross-session search nobody else has

**The path to dominance**: Close 6 critical gaps, then differentiate on 5 unique vectors.

---

## Part 1: What Each Competitor Does Best

### Goose (Block/Square)
**Strengths:**
- **MCP-native architecture** — Everything is an MCP server, simplest extension model
- **Recipe system** — YAML workflows with cron scheduling, structured output
- **Lead-Worker provider** — Automatic model switching between planning and execution
- **MOIM context injection** — Per-message context without persisting to history
- **Declarative custom providers** — Add providers via YAML without code
- **20+ providers** — Most provider support
- **Security classification service** — ML-based prompt injection detection

**Weaknesses:** Only 4 built-in tools, no git integration, no LSP

### OpenCode (SST)
**Strengths:**
- **Dual-stack architecture** — Separate backend API + frontend TUI
- **9 edit strategies with benchmark harness** — Tests strategies against real diffs
- **JSONL session format with DAG/tree** — Branching conversations
- **Separate git snapshot repo** — No pollution of project history
- **Skills system** — Auto-invoked based on file patterns
- **ACP (Agent Communication Protocol)** — REST API for editor integration

**Weaknesses:** Only ~20 tools, no hierarchical delegation

### Cline
**Strengths:**
- **40+ providers** — Most variety via OpenRouter
- **Subagent spawning** — Flat spawning with `spawn_subagent`
- **Shadow git checkpoints** — Rollback support
- **Per-tool approval gates** — Fine-grained permissions

**Weaknesses:** VS Code only (not standalone), monolithic 3547-line Task class

### Aider
**Strengths:**
- **13 edit formats** — Most variety (whole, diff, udiff, editor-diff, etc.)
- **Repo map with PageRank** — Only sends relevant symbols to context
- **Architect mode** — Dual-model pipeline (architect plans, editor applies)
- **Voice coding** — Voice-to-code input
- **AI comment watcher** — Watches files for `# AI: do X` comments
- **Auto-commits every edit** — Git as safety net

**Weaknesses:** Chat-parse (not tool calling), no plugin system

### Codex CLI (OpenAI)
**Strengths:**
- **OS-level sandboxing** — Seatbelt (macOS), bwrap/Landlock (Linux), seccomp
- **Managed network proxy** — All network through controlled proxy
- **Ghost snapshots** — Invisible rollback points
- **Full async with cancellation** — Rust-based, true parallelism

**Weaknesses:** OpenAI only, no MCP, no extensions

### Gemini CLI (Google)
**Strengths:**
- **1M native context** — Leverages Gemini's massive window
- **Dual loop detection** — Heuristic + LLM self-assessment for stuck detection
- **Policy engine** — Composable `AllowedToolsChecker`, `ConfirmationChecker`
- **A2A protocol** — Agent-to-Agent standard
- **Google Search grounding** — Real-time web in responses

**Weaknesses:** Gemini only, 5-turn hard limit, no parallel tool calls

### Continue
**Strengths:**
- **30+ providers** — Via openai-adapters
- **Context providers** — Extensible system (files, symbols, URLs)
- **Autocomplete** — Tab completion with fast model
- **IDE-native** — Deep VS Code integration

**Weaknesses:** IDE-only, ~20 tools

### OpenHands
**Strengths:**
- **9 condenser strategies** — Most sophisticated context management:
  - Recent, LLM-summarize, amortized, observation-masking, structured, hybrid, browser-turn, identity, no-op
- **Event-sourced architecture** — Full replay, time-travel debugging
- **Docker mandatory** — Complete isolation
- **AgentDelegator** — Can delegate to specialized agents

**Weaknesses:** Docker-only, no MCP, no LSP

### SWE-agent
**Strengths:**
- **History processors pipeline** — Chain of filters transforms what LLM sees
- **Action samplers (best-of-N)** — Generate N responses, pick best
- **Windowed edit** — 100-line viewing window with line numbers
- **10 output parsing formats** — Most flexible bash output handling

**Weaknesses:** Academic/research focus, no plugin system

### Zed
**Strengths:**
- **StreamingDiff** — Applies edits AS LLM streams tokens
- **Per-hunk accept/reject** — Review each change individually
- **EditAgent** — Specialized sub-agent for multi-file edits
- **Agent profiles** — Bundled model + tools + instructions
- **MCP server mode** — Exposes Zed's tools to other MCP clients
- **Native LSP** — Editor-integrated language servers

**Weaknesses:** Editor-only (not standalone agent)

### Pi Mono
**Strengths:**
- **Minimal core (~5 files)** — Everything via extensions
- **25+ event hooks** — Rich extension lifecycle
- **Tree-structured sessions** — JSONL with DAG
- **22 providers via 9 protocols** — Most protocol variety
- **Provider/model switching** — Per-request selection
- **Minimal tool mode** — Only essential tools for simple tasks

**Weaknesses:** Smaller ecosystem, newer project

### Plandex
**Strengths:**
- **Concurrent build pipeline** — Builds multiple files simultaneously
- **9 model roles** — Different model per role (planner, coder, namer, committer, etc.)
- **Plan/Build separation** — Explicit planning phase before execution
- **Server-side git** — Branch-based plan management

**Weaknesses:** Client-server (not local), no plugin system

---

## Part 2: The Gap Analysis

### P0 — Critical Gaps (Lose to Competitors)

| Gap | Competitors | Impact | Solution |
|-----|-------------|--------|----------|
| **Streaming diff application** | Zed | High latency perception | Apply edits as tokens stream |
| **Per-hunk review** | Zed | User trust | Accept/reject individual changes |
| **Enhanced loop detection** | Gemini, OpenHands | Wasted tokens | Multi-signal stuck detection |
| **Edit strategy benchmarks** | OpenCode, Aider | Suboptimal edits | Test harness for 8 strategies |
| **Declarative safety policies** | Gemini, Continue | Rigid permissions | YAML policy rules |
| **OS-level sandboxing** | Codex CLI | Security | Seatbelt/bwrap as Docker alt |

### P1 — Competitive Parity (Need to Match)

| Gap | Competitors | Solution |
|-----|-------------|----------|
| **Multiple compaction strategies** | OpenHands (9) | Add 3-4 strategies beyond current |
| **History processor pipeline** | SWE-agent | Hook-based history transforms |
| **MCP server mode** | Zed | Expose AVA tools via MCP |
| **Concurrent multi-file builds** | Plandex | Parallel file edits |
| **Voice coding** | Aider | Whisper integration |
| **AI comment watcher** | Aider | File watcher for trigger comments |

### P2 — Differentiation Opportunities

| Feature | Inspiration | AVA Advantage |
|---------|-------------|---------------|
| **Action samplers (best-of-N)** | SWE-agent | Combine with Praxis hierarchy |
| **Lead-Worker auto-routing** | Goose | Per-turn model switching |
| **Recipe system** | Goose | Multi-agent recipes |
| **1M context window support** | Gemini | For Gemini + future models |
| **Event-sourced replay** | OpenHands | Debug agent decisions |

---

## Part 3: AVA's Unique Strengths to Amplify

### 1. Tool Breadath (55+ vs ~25 max)

AVA has **2x the tools** of any competitor:
- 9 LSP tools (nobody else exposes LSP to agents)
- 6 git tools (branch, PR, issue, worktree)
- 4 memory tools (cross-session persistence)
- 3 background shell tools
- Session recall (FTS5 search)

**Amplification**: Add 5 more unique tools:
- `code_review` — AI review with inline comments
- `test_generate` — Auto-generate tests for changed code
- `benchmark` — Performance regression detection
- `dependency_graph` — Visualize import relationships
- `hot_reload` — Preview changes without restart

### 2. Praxis Hierarchy (3-tier vs flat)

No competitor has **structured delegation**:
- Commander (Team Lead) → 5 Senior Leads → Junior Devs
- Domain specialization (Frontend/Backend/QA/Research/Debug)
- Auto-routing based on task analysis
- Budget awareness per worker

**Amplification**:
- Add **orchestrator patterns** (MapReduce, DAG execution)
- **Dynamic team sizing** (spawn more workers for large tasks)
- **Cross-team coordination** (standup meetings between agents)
- **Skill transfer** (workers learn from each other)

### 3. ExtensionAPI (8 methods vs 3-4)

Richest extension surface:
- `registerTool` + `registerProvider` + `registerAgentMode`
- `registerValidator` + `addToolMiddleware` (priority-based)
- `registerHook` / `callHook` (sequential chaining)
- Per-extension storage + Plugin scaffold CLI

**Amplification**:
- **Extension marketplace** with ratings (Obsidian model)
- **Vibe-code plugins** — AI generates extensions from description
- **Extension composition** — Plugins can depend on other plugins
- **Hot reload** — Update extensions without restart

### 4. Desktop-Native Tauri

Only AVA combines:
- Desktop app (not CLI-only like Codex/Gemini)
- Native performance (not Electron bloat)
- Local-first (not cloud-dependent like Plandex)
- Multi-platform (macOS, Linux, Windows)

**Amplification**:
- **Native UI components** (context menus, tray icons)
- **System integrations** (notifications, file watchers)
- **Offline mode** (local models, cached resources)
- **Ambient terminal** (one-shot commands from shell)

### 5. Session Architecture (DAG vs linear)

Only OpenCode, Pi Mono, and AVA have **branching conversations**:
- Fork sessions for exploration
- Merge branches when ready
- Cross-branch search via DAG ancestors

**Amplification**:
- **Visual branch graph** (like Git graph)
- **Branch comparison** (diff between explorations)
- **Cherry-pick** (apply changes from one branch to another)
- **Branch templates** (start from common patterns)

---

## Part 4: The Consolidated Vision

### AVA 2.0: The Ultimate AI Coding Agent

```
┌─────────────────────────────────────────────────────────────────┐
│                        USER INTERFACE                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐              │
│  │ Chat Panel  │  │ Team View   │  │ Code Viewer │              │
│  │ (main chat) │  │ (Praxis UI) │  │ (inline)    │              │
│  └─────────────┘  └─────────────┘  └─────────────┘              │
└─────────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────────┐
│                     AGENT ORCHESTRATION                          │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐              │
│  │  Commander  │  │  Scheduler  │  │  Validator  │              │
│  │ (Team Lead) │  │ (task queue)│  │ (QA checks) │              │
│  └──────┬──────┘  └─────────────┘  └─────────────┘              │
│         │                                                        │
│  ┌──────┴──────┬─────────────┬─────────────┬─────────────┐      │
│  │ Frontend    │ Backend     │ QA Lead     │ Researcher  │      │
│  │ Lead        │ Lead        │             │             │      │
│  └──────┬──────┴──────┬──────┴──────┬──────┴──────┬──────┘      │
│         │             │             │             │              │
│    ┌────┴────┐   ┌────┴────┐   ┌────┴────┐   ┌────┴────┐       │
│    │ Workers │   │ Workers │   │ Workers │   │ Workers │       │
│    └─────────┘   └─────────┘   └─────────┘   └─────────┘       │
└─────────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────────┐
│                        TOOL ECOSYSTEM                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐              │
│  │ 60+ Tools   │  │ LSP Client  │  │ MCP Client  │              │
│  │ (built-in)  │  │ (9 tools)   │  │ (3000+)     │              │
│  └─────────────┘  └─────────────┘  └─────────────┘              │
│                                                                  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐              │
│  │ Git Tools   │  │ Memory      │  │ Sandbox     │              │
│  │ (6 tools)   │  │ (FTS5)      │  │ (Docker/OS) │              │
│  └─────────────┘  └─────────────┘  └─────────────┘              │
└─────────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────────┐
│                     EXTENSION SYSTEM                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐              │
│  │ Marketplace │  │ Plugin SDK  │  │ Hot Reload  │              │
│  │ (ratings)   │  │ (simple)    │  │ (dev mode)  │              │
│  └─────────────┘  └─────────────┘  └─────────────┘              │
└─────────────────────────────────────────────────────────────────┘
```

### Key Differentiators

1. **The Dev Team Metaphor** — Not one chatbot, a visible team you can interact with
2. **Model Flexibility** — Use best model for each task, auto-routed
3. **Local-First** — Your data, your machine, your control
4. **Extensible** — Obsidian-style plugin ecosystem
5. **Proven Architecture** — Combines best patterns from 12 competitors

### Success Metrics

- **Tools**: 55 → 70+ (more than 2x nearest competitor)
- **Extensions**: 30 → 100+ (marketplace launch)
- **Users**: Vibe coders + developers + plugin creators
- **Performance**: <500ms startup, <100ms tool execution
- **Satisfaction**: Best-in-class for multi-file edits

---

## Part 5: Implementation Roadmap

### Phase 1: Close Critical Gaps (Sprint 24-26)

1. **Streaming diff application** — Apply edits as LLM streams
2. **Per-hunk review UI** — Accept/reject individual changes
3. **Enhanced stuck detection** — Multi-signal loop detection
4. **Edit strategy benchmarks** — Test harness for 8 strategies
5. **Declarative policies** — YAML safety rules
6. **OS sandboxing** — Seatbelt/bwrap alternatives

### Phase 2: Achieve Parity (Sprint 27-29)

1. **Multiple compaction strategies** — 3-4 strategies
2. **History processors** — Hook-based transforms
3. **MCP server mode** — Expose AVA tools
4. **Concurrent builds** — Parallel file edits
5. **Voice coding** — Whisper integration
6. **Comment watcher** — AI trigger comments

### Phase 3: Differentiate (Sprint 30-32)

1. **Action samplers** — Best-of-N generation
2. **Lead-Worker routing** — Per-turn model switching
3. **Recipe system** — Multi-agent workflows
4. **1M context support** — For Gemini models
5. **Event replay** — Debug agent decisions

### Phase 4: Ecosystem (Sprint 33+)

1. **Extension marketplace** — Community plugins
2. **Vibe-code plugins** — AI generates extensions
3. **Branch visualization** — DAG graph UI
4. **Team templates** — Pre-configured agent teams
5. **Enterprise features** — SSO, audit logs, admin

---

## Conclusion

AVA is already the **most comprehensive AI coding agent** by tool count and architecture. The path forward is clear:

1. **Close 6 critical gaps** to match best-in-class features
2. **Add 6 parity features** to eliminate competitive disadvantages  
3. **Build 5 unique differentiators** on top of Praxis + ExtensionAPI
4. **Launch ecosystem** with marketplace and community

The result: **AVA 2.0** — the Obsidian of AI coding, combining the best of Goose's MCP-native design, Zed's streaming edits, Aider's repo map, OpenHands' context management, and AVA's unique Praxis hierarchy.

**Timeline**: 6-9 months to full differentiation.
**Goal**: Become the default AI coding agent for developers who want power AND flexibility.

---

*Generated from analysis of: Goose, OpenCode, Cline, Aider, Codex CLI, Gemini CLI, Continue, OpenHands, SWE-agent, Zed, Pi Mono, Plandex*
*Last updated: 2026-03-03*
