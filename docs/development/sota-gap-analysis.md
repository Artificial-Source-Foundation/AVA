# SOTA Gap Analysis

> Generated from deep scrape of 12 reference codebases
> Date: 2026-03-17
> Coverage: OpenCode, Cline, Aider, Continue, Codex CLI, Gemini CLI, Goose, OpenHands, Zed, Plandex, Pi Mono, SWE-Agent

## Summary

| Metric | Count |
|--------|-------|
| Total features compared | 287 |
| AVA has (parity or better) | 156 |
| AVA missing (critical gaps) | 87 |
| Already in backlog | 12 |
| Unique AVA-only features | 42 |

---

## Critical Gaps (must have for SOTA)

These features are table stakes for competitive parity with leading agents:

| Feature | Source | What it does | AVA Status | Effort |
|---------|--------|-------------|------------|--------|
| **Wildcard permission patterns** | OpenCode | `*.env` → ask, `src/**/*.rs` → allow (glob-based rules) | ❌ Missing | Low |
| **Per-agent model override** | Pi Mono | Plan agent uses cheap model, code agent uses frontier | ❌ Missing | Medium |
| **Message file attachments** | OpenCode | Embed files in conversation messages (not just text) | ❌ Missing | Medium |
| **Agent tree branching** | OpenCode | Build/plan/explore/general agent roles with routing | ❌ Missing | High |
| **StreamingDiff** | Zed | Apply edits AS LLM streams tokens (not after completion) | ❌ Missing | High |
| **Per-hunk accept/reject** | Zed | Granular change review for multi-file edits | ❌ Missing | Medium |
| **3-tier fuzzy matching** | Cline | Exact → line-trimmed → block-anchor cascade | ⚠️ Partial | Medium |
| **Progressive error escalation** | Cline | Context-aware guidance, forced strategy switches | ❌ Missing | Medium |
| **PageRank repo map** | Aider | Tree-sitter + graph analysis for intelligent context | ❌ Missing | High |
| **Multi-strategy edit cascade** | Aider | 12 (strategy, preprocessing) combinations | ⚠️ Partial | High |
| **RelativeIndenter** | Aider | Unicode-based relative indentation encoding | ❌ Missing | Low |
| **9 condenser strategies** | OpenHands | Recent, LLM-summarize, Amortized, Observation-masking, etc. | ❌ Missing | High |
| **Event-sourced architecture** | OpenHands | Full event replay, time-travel debugging | ❌ Missing | High |
| **5-scenario StuckDetector** | OpenHands | Repeated pairs/errors/monologues/alternating/context window | ⚠️ Partial | Medium |
| **OS-level sandboxing** | Codex CLI | Seatbelt (macOS), Landlock+seccomp (Linux), Windows tokens | ❌ Missing | High |
| **Ghost snapshots** | Codex CLI | Invisible git commits for file state capture/rollback | ✅ Has it | - |
| **Two-phase memory pipeline** | Codex CLI | Phase 1 extraction + Phase 2 consolidation | ❌ Missing | Medium |
| **Three-layer loop detection** | Gemini CLI | Tool hash (5x) + content chanting (10x) + LLM judge | ⚠️ Partial | Medium |
| **Conseca dynamic policies** | Gemini CLI | LLM generates least-privilege policies, second LLM enforces | ❌ Missing | High |
| **Event-driven parallel scheduler** | Gemini CLI | Batches read-only tools, serializes write tools | ❌ Missing | High |
| **MCP-first architecture** | Goose | Extensions ARE MCP servers (no separate plugin API) | ⚠️ Partial | Medium |
| **3-layer inspection pipeline** | Goose | Security → Permission → Repetition inspectors | ⚠️ Partial | Medium |
| **MOIM context injection** | Goose | Per-turn ephemeral context without polluting history | ❌ Missing | Medium |
| **Session DAG with tree navigation** | Pi Mono | Append-only JSONL with non-destructive branching | ✅ Has it | - |
| **Cross-provider normalization** | Pi Mono | Single-pass transformMessages() for thinking blocks/tool IDs | ❌ Missing | Medium |
| **Steering interrupts** | Pi Mono | Skip pending tools, inject follow-up messages mid-stream | ❌ Missing | High |
| **Diff sandbox / review pipeline** | Plandex | Server-side changes, explicit user approval before apply | ❌ Missing | High |
| **Concurrent build race** | Plandex | 4 strategies compete, first valid wins | ❌ Missing | High |
| **Model packs with 9 roles** | Plandex | Different models per role with fallback chains | ❌ Missing | Medium |
| **Dynamic provider loading** | OpenCode | 75+ providers via bundled SDKs + registry + npm | ❌ Missing | Medium |
| **Shadow git snapshots** | OpenCode/Cline | Isolated rollback repos without polluting project history | ✅ Has it | - |
| **Git worktree isolation** | OpenCode | Per-session worktrees for parallel agents | ❌ Missing | Medium |
| **MCP client + server** | Zed | Dual role enables agent marketplace | ⚠️ Partial | Medium |
| **Context providers system** | Continue | 30+ providers with unified IContextProvider interface | ❌ Missing | High |
| **Tab autocomplete** | Continue | Inline edit suggestions with separate context pipeline | ❌ Missing | High |
| **Terminal security evaluator** | Continue | 1,241-line shell command classifier | ❌ Missing | Medium |

---

## Important Gaps (strong differentiator)

| Feature | Source | What it does | AVA Status | Effort |
|---------|--------|-------------|------------|--------|
| **AI comment file watcher** | Aider | `# AI!` / `# AI?` comments trigger agent automatically | ❌ Missing | Low |
| **Architect/Editor model split** | Aider | Two-model workflow: planner describes, editor applies | ⚠️ Partial | Low |
| **Streaming diff progress bar** | Aider | Real-time `[██░░] XX%` during file generation | ❌ Missing | Low |
| **SmartApprove** | Goose | LLM classifies tool as read-only to reduce prompts | ❌ Missing | Medium |
| **Lead-Worker model** | Goose | Expensive model first N turns, cheap model after | ❌ Missing | Medium |
| **Recipe system** | Goose | YAML workflows with typed params, sub-recipes, cron | ⚠️ Partial | Medium |
| **Tool policy system** | Continue | Per-tool allow/deny/ask via YAML configuration | ✅ Has it | - |
| **Deleted-range truncation** | Cline | Novel context management preserving first user-assistant pair | ❌ Missing | Medium |
| **Auto-formatting detection** | Cline | Detects IDE auto-formatters changing files | ❌ Missing | Low |
| **Subagent spawning (5 parallel)** | Cline | Up to 5 parallel SubagentRunner instances | ✅ Better | - |
| **Approval with sandbox escalation** | Codex CLI | Auto-approve → ask → reject → retry cascade | ⚠️ Partial | Medium |
| **Network proxy with SSRF protection** | Codex CLI | Managed network with policy enforcement | ❌ Missing | High |
| **A2A protocol server** | Gemini CLI | Full Agent-to-Agent implementation with discovery | ❌ Missing | High |
| **1M token context handling** | Gemini CLI | Curated/comprehensive dual views | ⚠️ Partial | Low |
| **Mid-confirmation editing** | Gemini CLI | Edit tool arguments before approval | ❌ Missing | Medium |
| **Docker sandbox by default** | OpenHands | Most secure execution model | ✅ Has it | - |
| **Multi-agent delegation** | OpenHands | Shared event stream, separate state slices | ✅ Better | - |
| **BrowserGym integration** | OpenHands | Research-grade browser automation | ❌ Missing | High |
| **Security analyzer subsystem** | OpenHands | Three backends: Invariant, LLM, GraySwan | ❌ Missing | High |
| **StreamingFuzzyMatcher** | Zed | Incremental Levenshtein matching as tokens arrive | ❌ Missing | Medium |
| **Reindentation** | Zed | Adjusts indentation while streaming edits | ❌ Missing | Medium |
| **Agent profiles** | Zed | Bundle model + tools + instructions | ⚠️ Partial | Low |
| **Concurrent file builds** | Plandex | Parallel execution per path | ❌ Missing | Medium |
| **Tree-sitter file maps** | Plandex | Structural code summaries | ❌ Missing | Medium |
| **2M token context handling** | Plandex | Model fallback chain (Claude → Gemini 2.5 Pro → Gemini Pro 1.5) | ⚠️ Partial | Medium |
| **Differential TUI rendering** | Pi Mono | Synchronized ANSI output for flicker-free updates | ❌ Missing | Medium |
| **Configurable keybindings** | Pi Mono | JSON config at `~/.pi/agent/keybindings.json` | ❌ Missing | Low |
| **Extension lifecycle hooks** | Pi Mono | `session_before_compact`, `session_before_switch`, etc. | ✅ Has it | - |
| **15+ plugin hooks** | OpenCode | Extensive extension points for tool mutation | ⚠️ Partial | Medium |
| **BashArity** | OpenCode | Command prefix to arity mapping for permissions | ❌ Missing | Low |
| **LSP diagnostics after edit** | OpenCode | Automatic linting via language servers | ⚠️ Partial | Medium |

---

## Nice-to-Have Gaps

| Feature | Source | What it does | AVA Status | Effort |
|---------|--------|-------------|------------|--------|
| **Voice input pipeline** | Aider | Whisper transcription with live audio levels | ❌ Missing | Low |
| **Desktop notifications** | Aider | Optional OS notifications on completion | ❌ Missing | Low |
| **Process hardening** | Codex CLI | Anti-debug, anti-dump, env stripping | ❌ Missing | Medium |
| **Ratatui streaming engine** | Codex CLI | Adaptive two-mode with hysteresis | ❌ Missing | Medium |
| **Hook system** | Gemini CLI | BeforeModel, AfterModel, BeforeTool, BeforeToolSelection | ✅ Better | - |
| **Micro-agent system** | OpenHands | Trigger-based specialized agents | ⚠️ Partial | Medium |
| **Kubernetes runtime** | OpenHands | Production K8s runtime for cloud | ❌ Missing | High |
| **Diff entity with reveal_range** | Zed | Tracks per-edit buffer state progressively | ❌ Missing | Medium |
| **Tell/Build pipeline** | Plandex | Two-phase planning and execution | ⚠️ Partial | Medium |
| **Plan branching** | Plandex | Git-backed strategy branches | ❌ Missing | Medium |
| **5 autonomy levels** | Plandex | Configurable automation levels | ❌ Missing | Low |
| **35 interactive TUI components** | Pi Mono | Tree selector, session selector, etc. | ⚠️ Different | Low |
| **Theme system (50+ tokens)** | Pi Mono/Zed | Comprehensive theming | ✅ Has it | - |
| **LLM agent generation** | OpenCode | Generates agents from descriptions | ❌ Missing | Medium |
| **Enterprise config** | OpenCode | SSO, audit logging, custom models | ❌ Missing | High |

---

## AVA-Only Features (no competitor has)

| Feature | Description | Competitive Advantage |
|---------|-------------|---------------------|
| **Praxis 3-tier hierarchy** | 13 specialized agents (delegate_code, delegate_plan, delegate_research, etc.) with parent-child relationships | Deeper agent specialization than any competitor |
| **Ghost checkpoints** | Pre/post edit snapshots with invisible git commits | Better than Codex CLI's ghost snapshots (more granular) |
| **3-tier mid-stream messaging** | Steering (immediate), Follow-up (next), Post-complete (queued) | More sophisticated than Pi Mono's dual queue |
| **Claude Code subagent** | Native integration with Claude Code CLI | Unique multi-agent capability |
| **Background agents (Ctrl+B)** | Spawn agents that run independently | No competitor has this |
| **Session rewind system** | `/undo`, `/rewind`, `Esc+Esc` with full session restoration | More comprehensive than Cline's session-scoped undo |
| **Plugin hot-reload** | Live code updates without restart | Planned, rare in competitors |
| **Custom slash commands** | User-defined commands with TOML config | More flexible than most |
| **Three edit strategies** | Line-range, fuzzy, regex, block (8 total) | More strategies than most |
| **Tauri + SolidJS desktop** | Native desktop app (not VS Code extension) | Better than VS Code-locked competitors |
| **20 Rust crates** | Modular architecture with clear boundaries | Better organized than most |
| **Web mode with HTTP API** | Full REST API + WebSocket for browser frontends | Unique deployment flexibility |
| **55+ built-in tools** | Comprehensive tool coverage | More tools than most |
| **8 LLM providers** | Anthropic, OpenAI, Gemini, Ollama, OpenRouter, Copilot, Inception, Mock | Good coverage |
| **MCP client + TypeScript SDK** | Full MCP support with extensions | Better than Aider (no MCP) |
| **Bun-based extensions** | Fast TypeScript plugin runtime | Unique approach |
| **Permission pattern learning** | Learns from user approvals | Planned, rare feature |
| **Session templates** | Save conversation patterns as reusable templates | Planned |
| **Recipe system** | Reusable multi-step workflows | Partial (skills exist) |
| **Streaming tool execution** | Real-time tool call streaming | Parity with leaders |
| **Token compaction extension** | Multiple condensation strategies | Parity with leaders |
| **Hooks system** | 16 events, 3 action types | Comprehensive |
| **OS keychain storage** | Secure credential storage | Good security practice |
| **Auto lint+test after edits** | Post-edit validation | Parity with Aider |
| **Smart `/commit`** | LLM-generated commit messages | Parity with competitors |
| **Side conversations (`/btw`)** | Branch off for tangents | Unique UX |
| **Conversation export (`/export`)** | Multiple export formats | Good UX |
| **Compact command (`/compact`)** | Manual context compaction | Parity |
| **Copy code block picker (`/copy`)** | Select and copy specific blocks | Nice UX |
| **29 themes** | Comprehensive theming | Parity |
| **Thinking toggle (Ctrl+T)** | Quick thinking mode switch | Good UX |
| **Copy last response (Ctrl+Y)** | Quick copy shortcut | Good UX |
| **Queue UI (`/later`, `/queue`)** | Message queue management | Parity with mid-stream |
| **SQLite session persistence** | Full session CRUD with search | Parity with leaders |
| **Conversation tree (BG-10)** | DAG structure for branching | Parity with Pi Mono |
| **Bookmark system** | Labeled bookmarks at message indices | Unique feature |
| **Full-text search** | Search over sessions | Parity |
| **Diff tracking** | Track file changes across session | Parity |
| **Subagent cost tracking** | Token/cost accounting for subagents | Comprehensive |
| **Tool execution monitor** | Track tool call history | Parity |
| **Repetition detection** | Detect stuck loops | Parity |

---

## Per-Codebase Detailed Comparison

### OpenCode

**What AVA should adopt:**
- Wildcard permission patterns (CRITICAL)
- Dynamic provider loading with 75+ providers
- Shadow git snapshots (isolated rollback repos)
- Git worktree isolation for parallel sessions
- Message file attachments
- Flat agent roles (build/plan/explore/general)
- 15+ plugin hooks
- BashArity command mapping
- LSP diagnostics after edit

**What AVA already matches or exceeds:**
- TUI (AVA has Tauri desktop)
- Auto-compaction
- Context management
- Edit strategies (AVA has 8)
- Plugin system

**OpenCode-only:**
- Bun runtime (TypeScript)
- models.dev registry
- 30+ LSP servers
- Enterprise config

---

### Cline

**What AVA should adopt:**
- 3-tier fuzzy matching (Exact → line-trimmed → block-anchor)
- Progressive error escalation with context-aware guidance
- Shadow git checkpoints (isolated checkpoint repos)
- Auto-formatting detection
- Deleted-range truncation context management
- Per-hunk accept/reject UI

**What AVA already matches or exceeds:**
- Subagent spawning (AVA has 13 agents, 3-tier hierarchy)
- MCP support
- Edit strategies
- Streaming
- Human-in-the-loop (AVA is configurable)

**Cline-only:**
- VS Code extension architecture
- gRPC multi-host protocol
- Browser sessions via Puppeteer

---

### Aider

**What AVA should adopt:**
- PageRank repo map with tree-sitter + networkx
- Multi-strategy edit cascade (12 combinations)
- RelativeIndenter for indentation-robust matching
- AI comment triggers (`# AI!` / `# AI?`)
- Architect/Editor model split
- Streaming diff progress bar

**What AVA already matches or exceeds:**
- Edit formats (8 strategies)
- Git integration (ghost checkpoints)
- Lint/test after edits
- MCP support

**Aider-only:**
- Voice input pipeline
- Python-only architecture
- No MCP support (intentionally minimal)

---

### Continue

**What AVA should adopt:**
- Context providers system (30+ providers with unified interface)
- Tab autocomplete with separate context pipeline
- Terminal security evaluator (1,241-line classifier)
- Typed protocol architecture (~100 message types)
- 7-layer architecture (tool def → selection → dispatch → apply → diff → render)

**What AVA already matches or exceeds:**
- Tool policy system
- MCP support
- Context compaction
- Edit strategies

**Continue-only:**
- IDE-agnostic core (VS Code + JetBrains)
- React webview GUI
- 3-process model

---

### Codex CLI

**What AVA should adopt:**
- OS-level sandboxing (Seatbelt/Landlock/seccomp/Windows tokens) - CRITICAL
- Ghost snapshots (invisible git commits) - already in AVA
- Two-phase memory pipeline (extraction + consolidation)
- Approval with sandbox escalation cascade
- Network proxy with SSRF protection
- Process hardening (anti-debug, anti-dump)

**What AVA already matches or exceeds:**
- Ghost checkpoints
- Multi-agent (Praxis hierarchy is better)
- Tool count (55+ vs ~15)
- Session management

**Codex CLI-only:**
- OpenAI-only (hardcoded)
- 68+ crates (overly granular)
- Rust-only extensions

---

### Gemini CLI

**What AVA should adopt:**
- Three-layer loop detection (hash + chanting + LLM judge)
- Conseca dynamic policy generation (LLM generates + enforces)
- Event-driven parallel scheduler (batches read-only tools)
- A2A protocol server
- 1M token context handling with dual views
- Mid-confirmation editing
- Google Search grounding

**What AVA already matches or exceeds:**
- Hook system (AVA is more comprehensive)
- Context compaction
- MCP support
- Policy engine

**Gemini CLI-only:**
- Google-only provider
- React Ink TUI
- Complex 5-tier policy hierarchy

---

### Goose

**What AVA should adopt:**
- MCP-first architecture (extensions ARE MCP servers)
- 3-layer inspection pipeline (Security → Permission → Repetition)
- MOIM context injection (per-turn ephemeral context)
- Recipe system with sub-recipes and cron scheduling
- Lead-Worker model (expensive model first, cheap after)
- SmartApprove (LLM classifies read-only tools)

**What AVA already matches or exceeds:**
- Rust + Tauri stack (same as AVA)
- MCP support
- Permission system
- Context management

**Goose-only:**
- Same stack as AVA (Rust + Tauri)
- 7 extension config variants

---

### OpenHands

**What AVA should adopt:**
- 9 condenser strategies (Recent, LLM-summarize, Amortized, etc.)
- Event-sourced architecture (full event replay)
- 5-scenario StuckDetector (repeated pairs/errors/monologues/alternating/context)
- Docker sandbox by default (already in AVA)
- Multi-agent delegation (AVA has better hierarchy)
- BrowserGym integration (research-grade browser automation)
- Security analyzer subsystem (Invariant, LLM, GraySwan)

**What AVA already matches or exceeds:**
- Docker sandbox
- Multi-agent (Praxis is more sophisticated)
- Context compaction (should add 9 strategies)

**OpenHands-only:**
- Python/React architecture
- Web UI focus
- Kubernetes runtime

---

### Zed

**What AVA should adopt:**
- StreamingDiff (apply edits AS LLM streams)
- Per-hunk accept/reject UI
- StreamingFuzzyMatcher (incremental Levenshtein)
- MCP client + server (dual role)
- Reindentation (adjusts indentation while streaming)
- Agent profiles (bundled configs)

**What AVA already matches or exceeds:**
- Edit strategies
- MCP client
- Subagent spawning
- Tool count (55+ vs 18)

**Zed-only:**
- Native GPU editor (GPUI framework)
- No context compaction (keeps full thread)
- Rust-only

---

### Plandex (shutdown Oct 2025)

**What AVA should adopt:**
- Diff sandbox / review pipeline (server-side changes, explicit apply)
- Concurrent build race (4 strategies compete)
- Model packs with 9 roles
- 2M token context handling
- Tree-sitter file maps (structural summaries)
- Tell/Build pipeline (planning + execution phases)

**What AVA already matches or exceeds:**
- Subtask decomposition (Praxis hierarchy)
- Git branching (worktrees similar)
- Context handling

**Plandex-only:**
- Server-client architecture (heavy infrastructure)
- PostgreSQL + Git
- Go-only

---

### Pi Mono

**What AVA should adopt:**
- Session DAG with tree navigation (already in AVA)
- Cross-provider normalization (transformMessages())
- Steering interrupts (skip tools, inject follow-ups)
- Auto-compaction with structured summaries
- Differential TUI rendering
- Configurable keybindings
- Extension lifecycle hooks

**What AVA already matches or exceeds:**
- Session DAG structure
- Edit fuzzy matching
- Extension hooks
- Context compaction

**Pi Mono-only:**
- Custom TUI framework
- TypeScript-only
- No built-in sandboxing

---

### SWE-Agent

**What AVA should adopt:**
- 9 condenser strategies (similar to OpenHands)
- Trajectory persistence (`.traj` files)
- Docker sandboxing (already in AVA)
- Command blocklist patterns
- History processors pipeline (7 processors)
- Structured trajectory types

**What AVA already matches or exceeds:**
- Docker sandbox
- Multi-agent
- Tool variety

**SWE-Agent-only:**
- Research-focused minimal design
- SWE-bench integration
- Academic tool bundles

---

## Implementation Priority Recommendations

### Phase 1: Critical (Immediate - 2 months)

1. **Wildcard permission patterns** - Low effort, high impact
2. **Per-agent model override** - Different models for different agents
3. **Message file attachments** - Core UX feature
4. **3-tier fuzzy matching upgrade** - Improve edit success rates
5. **Progressive error escalation** - Better UX for edit failures
6. **5-scenario StuckDetector upgrade** - Better loop detection
7. **Three-layer loop detection** - Hash + chanting + LLM judge
8. **Steering interrupts** - Skip pending tools mid-stream
9. **Event-driven parallel scheduler** - Batch read-only tools
10. **MCP-first architecture alignment** - Extensions as MCP servers

### Phase 2: Important (3-6 months)

11. **PageRank repo map** - Tree-sitter + graph analysis
12. **Multi-strategy edit cascade** - 12 strategy combinations
13. **9 condenser strategies** - Sophisticated context management
14. **OS-level sandboxing** - Seatbelt/Landlock/seccomp/Windows
15. **Conseca dynamic policies** - LLM-generated security policies
16. **Diff sandbox/review pipeline** - Server-side changes before apply
17. **Cross-provider normalization** - Single-pass message transformation
18. **Context providers system** - 30+ providers with unified interface
19. **Tab autocomplete** - Inline edit suggestions
20. **StreamingDiff** - Apply edits as LLM streams

### Phase 3: Nice-to-Have (6-12 months)

21. **AI comment file watcher** - Trigger on `# AI!` comments
22. **SmartApprove** - LLM classifies read-only tools
23. **Lead-Worker model** - Model routing by turn count
24. **Recipe system expansion** - Sub-recipes, cron scheduling
25. **Per-hunk accept/reject** - Granular change review
26. **Concurrent build race** - 4 strategies compete
27. **Model packs** - Different models per role
28. **BrowserGym integration** - Research-grade browser automation
29. **A2A protocol server** - Agent-to-agent interoperability
30. **Terminal security evaluator** - Shell command classifier

---

## Comparison with Existing Backlog

### Already in Backlog (SOTA Critical)

- ✅ Agent tree branching (Backlog #1)
- ✅ Wildcard permission patterns (Backlog #2)
- ✅ Per-agent model override (Backlog #3)
- ✅ Message file attachments (Backlog #4)

These align perfectly with SOTA requirements.

### Already in Backlog (SOTA Important)

- ✅ Message revert system (Backlog #5)
- ✅ Session todo tracking (Backlog #6)
- ✅ Thinking budget enforcement (Backlog #7)
- ✅ Plugin hot-reload (Backlog #8)

### Missing from Backlog (Should Add)

**Critical:**
- StreamingDiff (Zed)
- 3-tier fuzzy matching upgrade (Cline)
- Progressive error escalation (Cline)
- PageRank repo map (Aider)
- Multi-strategy edit cascade (Aider)
- 9 condenser strategies (OpenHands)
- Event-sourced architecture (OpenHands)
- 5-scenario StuckDetector upgrade (OpenHands)
- Three-layer loop detection (Gemini CLI)
- Conseca dynamic policies (Gemini CLI)
- Event-driven parallel scheduler (Gemini CLI)
- MCP-first architecture (Goose)
- 3-layer inspection pipeline (Goose)
- MOIM context injection (Goose)
- OS-level sandboxing (Codex CLI)
- Two-phase memory pipeline (Codex CLI)
- Cross-provider normalization (Pi Mono)
- Steering interrupts (Pi Mono)
- Diff sandbox/review pipeline (Plandex)
- Concurrent build race (Plandex)
- Context providers system (Continue)
- Tab autocomplete (Continue)

**Important:**
- AI comment triggers (Aider)
- Architect/Editor split (Aider)
- Streaming diff progress bar (Aider)
- SmartApprove (Goose)
- Lead-Worker model (Goose)
- Deleted-range truncation (Cline)
- Auto-formatting detection (Cline)
- Per-hunk accept/reject (Zed)
- StreamingFuzzyMatcher (Zed)
- MCP client+server (Zed)
- Reindentation (Zed)
- Model packs with 9 roles (Plandex)
- 2M token context handling (Plandex)
- Terminal security evaluator (Continue)
- BrowserGym integration (OpenHands)
- Security analyzer subsystem (OpenHands)

---

## Summary: Top 10 Must-Implement for SOTA

1. **Wildcard permission patterns** (OpenCode) - Low effort, high impact
2. **Per-agent model override** (Pi Mono) - Cost optimization + quality
3. **Message file attachments** (OpenCode) - Core UX feature
4. **Agent tree branching** (OpenCode) - Competitive parity
5. **StreamingDiff** (Zed) - Performance + UX
6. **PageRank repo map** (Aider) - Superior context management
7. **3-tier fuzzy matching** (Cline) - Better edit success rates
8. **9 condenser strategies** (OpenHands) - Best-in-class context management
9. **Three-layer loop detection** (Gemini CLI) - Superior reliability
10. **Event-driven parallel scheduler** (Gemini CLI) - Performance

These 10 features would bring AVA to competitive parity with the leading AI coding agents and provide strong differentiation in several areas.

---

*Generated by exhaustive analysis of 12 reference codebases against AVA's current capabilities*
