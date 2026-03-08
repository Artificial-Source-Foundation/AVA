# Competitive Gap Analysis

> AVA vs 8 reference codebases + PI Coding Agent. Updated 2026-03-02.
>
> Sources analyzed: **OpenCode**, **Aider**, **Cline**, **Roo Code**, **Gemini CLI**, **Goose**, **OpenHands**, **Plandex**, **PI Coding Agent**

---

## Executive Summary

AVA has strong foundations in multi-agent orchestration, codebase intelligence, memory, and permissions — areas where most competitors have nothing. But there are **15 high-impact gaps** that multiple codebases implement and AVA lacks entirely.

**Closed since last update (Sessions 48-53):** message queue, file watcher, step-level undo, git auto-commit, weak/editor model split, streaming tool preview.

**Closed (Sprint B7-B10):** mid-session provider switching, minimal tool mode, lead-worker auto-routing, iterative lint→fix loop (validator wired into agent completion gate).

**Closed (Sprint 15-16):** flat team delegation (5 delegate tools), Praxis 3-tier hierarchy (Commander → Leads → Workers), per-agent model/provider, agent import/export, planning pipeline, 13 built-in agents.

**Closed (Sprint 19):** parallel tool execution, vision/image support, background shell management, tool result truncation, granular permission modes (5 levels), MCP HTTP streaming + health monitoring, auto-learning memory, model availability + fallback, global doom loop detection, toolshim for non-tool-calling models, git tools (PR/branch/issue), per-tool-call checkpoints, Praxis orchestrator (parallel agents, error recovery, result aggregation), plugin install/uninstall backend + catalog API.

**Biggest gaps by frequency** (how many codebases have it):

| Gap | Who Has It | Priority |
|-----|-----------|----------|
| ~~Checkpointing / time-travel undo~~ | Cline, Gemini CLI, Plandex, Aider | ~~Critical~~ **DONE** |
| ~~Cost/token tracking UI~~ | Cline, Aider, PI, Gemini CLI | ~~Critical~~ **DONE** |
| ~~Vision/image support~~ | Cline, Aider, PI | ~~High~~ **DONE** |
| ~~Git auto-commit on edits~~ | Aider, Gemini CLI | ~~**High**~~ **DONE** |
| ~~Weak model for secondary tasks~~ | Aider, PI | ~~**High**~~ **DONE** |
| ~~Architect + editor model split~~ | Aider, PI | ~~**Medium**~~ **DONE** |
| ~~Sandbox/container execution~~ | Gemini CLI, OpenHands, Goose | ~~**High**~~ **DONE** |
| ~~Iterative lint → fix loop~~ | Aider, Cline, Gemini CLI | ~~High~~ **DONE** (validator wired into agent loop) |
| ~~Streaming tool preview~~ | Cline, OpenCode | ~~**Medium**~~ **DONE** |
| ~~File watcher / AI comments~~ | Aider, OpenCode | ~~**Medium**~~ **DONE** |
| ~~Architect + editor model split~~ | Aider, PI | ~~**Medium**~~ **DONE** |
| ~~Session revert/step-level undo~~ | OpenCode, Cline, Plandex | ~~**Medium**~~ **DONE** |
| ~~Message queue (steering/follow-up)~~ | PI | ~~**Medium**~~ **DONE** |
| ~~Tree-sitter (100+ languages)~~ | Aider | ~~**Medium**~~ **PARTIALLY DONE** (Sprint 17: regex-based symbol extraction for TS/JS, Python, Rust, Go — not tree-sitter WASM but covers main use cases) |
| RPC/SDK mode for embedding | PI, Aider | **Low** |
| Telemetry/analytics | Cline, Gemini CLI | **Low** |

### New Gaps Identified (2026-02-10 Audit)

| Gap | Source | Priority |
|-----|--------|----------|
| ~~Mid-session provider switching~~ | PI | ~~**Medium**~~ **DONE** |
| Session branching tree (true forks, shared prefix) | PI | **Medium** |
| ~~Minimal tool mode (4-tool subset)~~ | PI | ~~Low~~ **DONE** |
| Runtime skill/tool creation + hot reload | PI | Medium |
| ~~MCP OAuth flows (auth + refresh + storage)~~ | Cline, Gemini CLI, Goose | ~~Medium~~ **DONE** (Sprint 17: full OAuth + resources + prompts + sampling + reconnect) |
| ~~Remote browser support~~ | Cline | ~~Medium~~ **OUT OF SCOPE** (browser tool removed Sprint 13 — see git history; use MCP Puppeteer) |

### New Gaps Identified (2026-02-28 — Sprint 16 Audit)

| Gap | Who Has It | AVA Status | Priority |
|-----|-----------|------------|----------|
| ~~Doom loop detection at registry level~~ | Roo Code (ToolRepetitionDetector) | **DONE** (Sprint 19: `trackGlobalToolCall()`, `detectGlobalDoomLoop()` across all concurrent agents) | ~~Medium~~ Done |
| Session resume by ID | Gemini CLI, OpenCode | Not yet — sessions start fresh | Medium |
| Trusted folder boundaries | Gemini CLI | No folder trust zones | Low |
| Virtual scrolling for long histories | Cline | Progressive rendering, no virtualization | Low |
| Session sharing (URL) | Goose, OpenCode | Not yet | Low |
| Fuzzy edit strategies (9 modes) | OpenCode | 8 strategies — close but not benchmarked | Low |

### New Gaps Identified (2026-02-14 Deep Audit — Goose, OpenCode, Cline, Roo Code)

| Gap | Who Has It | AVA Status | Priority |
|-----|-----------|------------|----------|
| ~~Lead-Worker model auto-routing~~ | Goose | **DONE** (Sprint 15: flat delegation → Sprint 16: Praxis 3-tier with Commander → Leads → Workers, keyword routing) | ~~**High**~~ Done |
| ~~Parallel subagents (multi-task)~~ | Cline (5 parallel read-only), OpenCode (batch) | **DONE** (task-parallel.ts: Semaphore concurrency, explore=5, execute=1) | ~~High~~ Done |
| Resumable subagents | OpenCode (`task_id` param) | **DONE** (task tool has `sessionId`) | Done |
| ~~Batch tool (parallel tool exec)~~ | OpenCode (25 parallel via Promise.all) | **DONE** (task-parallel.ts: Promise.allSettled + Semaphore) | ~~Medium~~ Done |
| ~~Visibility metadata on messages~~ | Goose (user_visible + agent_visible flags) | **DONE** (MessageVisibility type + visibility strategy) | ~~Medium~~ Done |
| MCP-native tool architecture | Goose (all tools via MCP) | Native tools + MCP bridge | Low |
| ~~Tool prefix namespacing~~ | Goose (`ext__tool`) | **DONE** (namespacing.ts: `mcp__`/`ext__` prefixes, backward-compat lookup) | ~~Medium~~ Done |
| ~~Security inspector pipeline~~ | Goose (pattern-based, confidence scores, audit trail) | **DONE** (SecurityInspector + RepetitionInspector + InspectorPipeline + AuditTrail) | ~~Medium~~ Done |
| Mode system with tool restrictions | Roo Code (4 modes + custom) | Workers have filtered tools (same concept) | Done (via workers) |
| Boomerang task delegation | Roo Code (parent-child tree, mode-per-subtask) | Team hierarchy (same concept, more structured) | Done (via commander) |
| ~~Toolshim for non-tool-calling models~~ | Goose (prompt-based tool extraction) | **DONE** (Sprint 19: `parseToolCallsFromText()`, `buildToolSchemaXML()`, `needsToolShim()`) | ~~Low~~ Done |
| ~~Context visibility layering~~ | Goose (compacted = agent-visible only) | **DONE** (visibility.ts strategy) | ~~Medium~~ Done |
| ~~Auto-compaction threshold~~ | Goose (80%), Roo Code (configurable %) | **DONE** (createAutoCompactor + compactionThreshold setting, default 80%) | ~~Medium~~ Done |
| Recipe system (YAML task automation) | Goose (cron scheduling, success checks, retry) | Custom commands (TOML, no scheduling) | Low |
| ~~Container/sandbox execution~~ | Goose (Docker), Gemini CLI, OpenHands | **DONE** (DockerSandbox + NoopSandbox, opt-in mode, graceful fallback) | ~~High~~ Done |
| Extension malware checking | Goose | No — plugins trusted by default | Low |
| ~~SQLite session storage~~ | Goose | **DONE** (Sprint 17: `SqliteSessionStorage`, `MemorySessionStorage`, `SessionStorage` interface) | ~~Medium~~ Done |
| Client/server architecture | OpenCode (Hono HTTP + SSE) | Tauri IPC (desktop-only) | Low |
| ~~Tool repetition detection in registry~~ | Roo Code (ToolRepetitionDetector) | **DONE** (Sprint 19: global doom loop detection across all concurrent agents) | ~~Low~~ Done |

---

## Gap Details

### 1. Checkpointing / Time-Travel Undo (CRITICAL)

**What**: Snapshot workspace + session state at every step. User can rollback to any previous point — files, messages, or both.

**Who has it**:
- **Cline**: 3 restore modes (task only, workspace only, both). `CheckpointOverlay` UI with confirmation dialog.
- **Gemini CLI**: Full checkpoint system with git-based snapshots. `/checkpoint` command.
- **Plandex**: Version control on plans with rewind/branch. Every plan change creates a versioned snapshot.
- **Aider**: Git auto-commit = implicit checkpoint. `/undo` reverts last commit.

**AVA status**: Session forking exists ("Fork from here") but it's one-time branching, not iterative checkpointing. `createCheckpoint()` / `rollbackToCheckpoint()` were scaffolded in Session 40 but not wired to UI.

**Impact**: Enables exploratory coding — try approach A, rollback, try approach B. Critical for vibe coders.

**Implementation**: Wire existing `session.ts` checkpoint functions to UI. Add workspace-level file snapshots (git stash or shadow copies).

---

### 2. Cost & Token Tracking UI (CRITICAL)

**What**: Show per-request cost, total session cost, tokens in/out, prompt cache hits, context pressure.

**Who has it**:
- **Cline**: `getApiMetrics.ts` parses every API call. Shows `≈$0.47` per request + total in header. `ContextWindow.tsx` shows remaining tokens.
- **Aider**: `/tokens` command shows budget. Tracks cache writes/reads separately.
- **PI**: `/session` command shows token count + estimated cost.
- **Gemini CLI**: Token usage displayed per response.

**AVA status**: Internal `ContextTracker` counts tokens. `ContextBar.tsx` shows a progress bar. No cost calculation, no per-request metrics, no cache tracking.

**Impact**: Users managing API budgets need cost visibility. #1 requested feature in most AI coding tools.

**Implementation**: Add cost calculation per provider (price per 1K tokens). Show in `ContextBar.tsx` or new `CostDisplay` component.

---

### ~~3. Vision / Image Support (HIGH)~~ — DONE

**What**: Accept image uploads in chat (paste, drag-drop, file picker). Send to vision-capable models. Display inline.

**Who has it**:
- **Cline**: Image uploads, clipboard paste, vision model detection, lazy-load display.
- **Aider**: `/add image.png`, `/paste` from clipboard, auto-detects vision models.
- **PI**: Image input support in chat.

**AVA status**: **DONE**. Frontend: paste/drop/base64 image support (Phase 1.5). Backend: `ImageBlock` type in `ContentBlock` union, agent loop handles image content in messages, OpenAI-compat provider conversion (Sprint 19).

**Impact**: Visual debugging (screenshot → fix), mockup → code, diagram → implementation.

---

### 4. Git Auto-Commit on Edits (HIGH)

**What**: After each successful edit, auto-commit with AI-generated message. Separates user work from AI work. Enables undo.

**Who has it**:
- **Aider**: Auto-commit after every edit. Weak model generates commit message. `/undo` reverts last. Author attribution: `"User (aider)"`.
- **Gemini CLI**: Git checkpoint integration for time-travel.

**AVA status**: **DONE**. `git/auto-commit.ts` auto-stages and commits after file-modifying tools. Settings UI toggle in Behavior tab. `undoLastAutoCommit()` reverts the most recent ava commit. Commit message prefix is configurable.

**Impact**: Safety net. Never lose work. Git history shows exactly what AI changed vs what user changed.

---

### 5. Weak Model for Secondary Tasks (HIGH)

**What**: Every main model has an associated cheap/fast model for secondary tasks (commit messages, summaries, simple fixes).

**Who has it**:
- **Aider**: `weak_model_name` per model. Claude Sonnet → Claude Haiku for commits.
- **PI**: Different models for different sub-tasks.

**AVA status**: **DONE**. `weakModel` optional field in `ProviderSettings` + `getWeakModelConfig()` helper. Planner and self-review validator use weak model when configured. Frontend: dropdown in LLM tab with model pair auto-suggestions. Off by default (uses primary model).

**Impact**: 50-80% cost reduction on secondary tasks (commit messages, summaries, simple classification).

---

### 6. Sandbox / Container Execution (HIGH)

**What**: Run code in isolated environment (Docker, gVisor, nsjail) to prevent unsafe operations on host.

**Who has it**:
- **Gemini CLI**: 5 sandbox methods (Docker, gVisor, nsjail, SELinux, manual).
- **OpenHands**: Full containerized sandbox — every agent runs in Docker with mounted workspace.
- **Goose**: Extension-based sandboxing.

**AVA status**: **Done** (Sprint B6). `tools/sandbox/` module: `DockerSandbox` (Docker-based), `NoopSandbox` (host passthrough, default). `SandboxSettings` in config (`mode: 'none' | 'docker'`). Bash tool routes through sandbox when `mode: 'docker'`, with graceful fallback to host if Docker unavailable. Memory/CPU limits, network isolation, timeout enforcement.

**Impact**: Safety for autonomous agents. Critical for "let it run overnight" workflows.

---

### 7. Iterative Lint → Fix Loop (HIGH)

**What**: After edit → run linter → if errors → send errors back to model → model fixes → repeat until clean.

**Who has it**:
- **Aider**: `linter.py` with `find_filenames_and_linenums()`. Auto-retry loop.
- **Cline**: Editor integration with VS Code Problems panel.
- **Gemini CLI**: Auto-test + auto-lint with feedback loop.

**AVA status**: **DONE** (Sprint B7). `validator/` pipeline (syntax → TypeScript → lint) is now wired into the agent loop's `complete_task` handler. On task completion, if files were modified, validation runs automatically. Failures inject feedback into the conversation and the agent gets up to `maxValidationRetries` (default 2) attempts to fix issues before accepting completion.

**Impact**: Dramatically improves first-time code correctness. Agent fixes its own mistakes.

---

### ~~8. Streaming Tool Preview (MEDIUM)~~ — DONE

**What**: Show tool results (diffs, terminal output) while the model is still generating its response, not after.

**Who has it**:
- **Cline**: `handlePartialBlock()` in tool handlers. See diffs appearing in real-time.
- **OpenCode**: Streaming tool execution with live terminal output.

**AVA status**: **DONE** — Already implemented in the streaming pipeline. `onToolUpdate` callback fires on every tool state change during streaming, updating `session.updateMessage()` reactively. `ToolCallGroup` auto-expands while active (pending/running tools). `ToolCallCard` shows spinner for running tools, status transitions, args summary, duration, and expandable output. The full reactive chain: `streamResponse` → `onToolUpdate` → `setActiveToolCalls` + `session.updateMessage` → `MessageBubble` → `ToolCallGroup(isStreaming=true)` → `ToolCallCard`.

**Impact**: Better perceived performance. User sees progress immediately.

---

### ~~9. File Watcher / AI Comments in Code (MEDIUM)~~ — DONE

**What**: Monitor project files for special comments (`// AI!`, `// AI?`). When detected, agent processes the comment as an instruction.

**Who has it**:
- **Aider**: `watch.py` — `AI!` triggers edit, `AI?` triggers question. Works with any language.
- **OpenCode**: File watcher for reactive workflows.

**AVA status**: **DONE** — `src/services/file-watcher.ts` watches project directory via Tauri FS plugin. Detects `// AI!`, `// AI?`, `# AI!`, `# AI?`, `-- AI!`, `-- AI?` patterns across 30+ file extensions. 500ms debounced recursive watch. Configurable toggle in Settings → Behavior. Wired to ChatView — detected comments auto-send as chat messages.

**Impact**: Seamless IDE integration without VS Code extension. User writes comment, switches to AVA, sees result.

---

### 10. Architect + Editor Model Split (MEDIUM)

**What**: Use expensive reasoning model for planning, cheaper model for actual file edits. Separates "what to do" from "how to write it".

**Who has it**:
- **Aider**: `architect_coder.py`. o1 (architect) + Sonnet (editor). `editor-diff` format.
- **PI**: Extensible model routing for different task phases.

**AVA status**: **DONE**. `editorModel` optional field in `ProviderSettings` + `getEditorModelConfig()` helper. Commander executor auto-applies editor model to workers (Junior Devs) when no per-worker override exists. Frontend: dropdown in LLM tab with 8 editor model presets and auto-pair suggestions (e.g., Opus → Sonnet, Sonnet → Haiku). Team Lead uses primary (architect) model for planning, Junior Devs use cheaper editor model for file changes.

**Impact**: Better results with reasoning models (o1, DeepSeek R1) that plan well but edit poorly. Cost savings.

---

### ~~11. Session Revert / Step-Level Undo (MEDIUM)~~ — DONE

**What**: Undo individual steps within a session, not just fork from a point.

**Who has it**:
- **OpenCode**: Session revert with step-level granularity.
- **Cline**: Checkpoint restore (task, workspace, or both).
- **Plandex**: Plan rewind/branch with version control.

**AVA status**: **DONE** — Built on git auto-commit foundation. Each file-modifying tool creates an ava-prefixed commit. Undo button in MessageInput toolbar reverts the most recent ava commit via `git revert --no-edit`. Shows success/error feedback for 2.5s. Only visible when git auto-commit is enabled. Plus session checkpoints (from Session 40) for coarser rollback.

**Impact**: Lighter than forking — just undo last step without creating new session.

---

### 12. Message Queue / Steering Interrupts (MEDIUM)

**What**: Queue messages to interrupt agent mid-execution (steering) or run after current task (follow-up).

**Who has it**:
- **PI**: Message queue with steering (interrupt) and follow-up (after completion) modes.

**AVA status**: **DONE**. `useChat` has a `messageQueue` signal — messages sent while streaming are queued as follow-ups and auto-dequeued after completion. `steer()` function cancels current stream and sends immediately. `Ctrl+Shift+Enter` keyboard shortcut for steering. Queue badge in MessageInput toolbar. Textarea stays enabled during chat streaming for type-ahead. Session switch clears queue. Cancel clears queue.

**Impact**: Better for long-running tasks. User can steer without canceling.

---

### 13. Tree-Sitter for 100+ Languages (MEDIUM)

**What**: Use tree-sitter grammars for symbol extraction instead of LSP (which requires language servers).

**Who has it**:
- **Aider**: `repomap.py` with tree-sitter. 100+ language support. Disk-based tag cache.

**AVA status**: `codebase/` module uses LSP-based symbols (5 languages: TS, Python, Go, Rust, Java).

**Impact**: Support any language without installing language servers. Faster than LSP for simple symbol extraction.

**Implementation**: Add `web-tree-sitter` with WASM grammars. Cache symbols to SQLite.

---

### 14. RPC/SDK Mode for Embedding (LOW)

**What**: Run as a library or RPC server for embedding in other applications.

**Who has it**:
- **PI**: `createAgentSession()` SDK + `--mode rpc` for stdin/stdout protocol.
- **Aider**: Python library mode.

**AVA status**: Planned for Phase 4. Core is importable as library but no RPC server.

**Impact**: Enables embedding AVA in CI/CD, custom tools, other apps.

---

### 15. Telemetry / Analytics (LOW)

**What**: Track feature usage, error rates, session metrics for product improvement.

**Who has it**:
- **Cline**: PostHog + OpenTelemetry.
- **Gemini CLI**: Enterprise-grade telemetry.

**AVA status**: None. Privacy-first philosophy.

**Impact**: Product improvement data. Optional/opt-in is fine.

---

## What AVA Has That Others Don't

These are AVA's **unique advantages** — features no other codebase implements:

| Feature | AVA | Closest Competitor |
|---------|--------|-------------------|
| **Praxis 3-tier hierarchy** (Commander → Leads → Workers) | 13 built-in agents, tier-aware delegation, planning pipeline | Roo Code (flat parent-child, no typed roles), Goose (2-tier lead-worker) |
| **Per-agent model/provider** (each agent uses different LLM) | Built-in (Sprint 16) | None — all competitors use single model |
| **Agent import/export** (share custom agents as JSON) | Built-in (Sprint 16) | None |
| **Planning pipeline** (Planner → Architect → Lead delegation) | Built-in (Sprint 16) | Plandex (plan-only, no agents) |
| **Worker scope filtering** (each agent only sees relevant files/tools) | Native | Roo Code (mode-based tool groups, similar) |
| **Parallel agent execution** (multiple seniors simultaneously) | Built-in | Cline (5 read-only subagents), Plandex (limited) |
| **Auto-reporting** (workers report up the chain) | Native | Goose (text-only summary return) |
| **User intervention points** (click into any agent's chat) | Desktop UI | None |
| **Doom loop detection** (agent loop + registry level) | Built-in | Goose (RepetitionInspector), Roo Code (ToolRepetitionDetector) |
| **Validator/QA pipeline** (syntax, types, lint, test, review) | 9-file module | Aider (basic lint only) |
| **Codebase intelligence** (PageRank, dependency graph, symbols) | Built-in | Aider (repo map only) |
| **Permission/policy engine** (risk assessment, auto-approval, rules) | Built-in | Goose (3-inspector pipeline, closest match) |
| **Hook system** (PreToolUse, PostToolUse, lifecycle) | Native | Cline (hooks, similar) |
| **Desktop UI** (activity bar, agent cards, animations) | Tauri + SolidJS | Cline/Roo Code (VS Code only) |
| **Plugin marketplace** (Phase 2) | Planned | PI (npm only) |
| **Real subagent execution** (task tool → AgentExecutor) | Built-in + tested | OpenCode (similar), Goose (Rust equivalent) |

---

## Recommended Implementation Roadmap

### Phase 1.5 (Current — Quick Wins) — ALL DONE

| # | Feature | Status | Source |
|---|---------|--------|--------|
| 1 | ~~Cost tracking UI~~ | **DONE** (per-message + session total) | Cline, Aider |
| 2 | ~~Wire checkpoints to UI~~ | **DONE** (create, display, restore) | Cline, Gemini CLI |
| 3 | ~~Iterative lint → fix loop~~ | **DONE** (autoFixLint after file edits) | Aider |
| 4 | ~~Vision/image support~~ | **DONE** (paste, drop, multimodal) | Cline, Aider |

### Phase 2 (Plugin Ecosystem + Gaps)

| # | Feature | Effort | Impact | Source |
|---|---------|--------|--------|--------|
| 5 | ~~**Git auto-commit**~~ | ~~1 week~~ | ~~High~~ **DONE** | Aider |
| 6 | ~~**Weak model support**~~ | ~~1 week~~ | ~~High~~ **DONE** | Aider |
| 7 | ~~**Architect/editor model split**~~ | ~~1 week~~ | ~~Medium~~ **DONE** | Aider |
| 8 | ~~**Streaming tool preview**~~ | ~~1 week~~ | ~~Medium~~ **DONE** | Cline |
| 9 | ~~**File watcher + AI comments**~~ | ~~1 week~~ | ~~Medium~~ **DONE** | Aider, OpenCode |
| 10 | ~~**Message queue**~~ | ~~3-4 days~~ | ~~Medium~~ **DONE** | PI |

### Phase 3+ (Longer Term)

| # | Feature | Effort | Impact | Source |
|---|---------|--------|--------|--------|
| 11 | ~~**Sandbox execution**~~ | ~~2-3 weeks~~ | ~~High~~ **DONE** | Gemini CLI, OpenHands, Goose |
| 12 | ~~**Parallel subagents**~~ | ~~1-2 weeks~~ | ~~High~~ **DONE** | Cline (5 concurrent), OpenCode (batch) |
| 13 | ~~**Lead-worker auto-routing**~~ | ~~1 week~~ | ~~High~~ **DONE** | Goose (auto-selects worker type) |
| 14 | ~~**Batch parallel tool exec**~~ | ~~1 week~~ | ~~Medium~~ **DONE** | OpenCode (25 via Promise.all) |
| 15 | ~~**Security inspector pipeline**~~ | ~~1-2 weeks~~ | ~~Medium~~ **DONE** | Goose (pattern + confidence + audit) |
| 16 | ~~**Visibility metadata**~~ | ~~3-4 days~~ | ~~Medium~~ **DONE** | Goose (user_visible/agent_visible) |
| 17 | **SQLite session storage** | 1 week | Medium | Goose |
| 18 | ~~**Auto-compaction threshold**~~ | ~~3-4 days~~ | ~~Medium~~ **DONE** | Goose (80%), Roo Code (configurable) |
| 19 | **Tree-sitter integration** | 2 weeks | Medium | Aider |
| 20 | ~~**Session step-level undo**~~ | ~~1 week~~ | ~~Medium~~ **DONE** | OpenCode, Cline |
| 21 | ~~**Voice coding**~~ | ~~2 weeks~~ | ~~Medium~~ **DONE** (Web Speech API + MicButton) | Aider |
| 22 | **RPC/SDK mode** | 2-3 weeks | Low | PI |
| 23 | ~~**3-tier agent hierarchy**~~ | ~~2-3 weeks~~ | ~~High~~ **DONE** (Sprint 16: Praxis) | Goose (2-tier), Roo Code (modes) |
| 24 | ~~**Per-agent model config**~~ | ~~1 week~~ | ~~High~~ **DONE** (Sprint 16) | — (unique to AVA) |
| 25 | ~~**Agent import/export**~~ | ~~3-4 days~~ | ~~Medium~~ **DONE** (Sprint 16) | — (unique to AVA) |
| 26 | ~~**Planning pipeline**~~ | ~~1 week~~ | ~~Medium~~ **DONE** (Sprint 16) | Plandex |

---

## Per-Codebase Key Takeaways

### PI Coding Agent (v0.52.9)
- **Philosophy**: Minimal core, maximum extensibility. "No permission popups."
- **Steal**: ~~Mid-session provider switching~~ **DONE**, session branching tree, ~~minimal tool mode~~ **DONE**, runtime skill creation, RPC mode
- **Skip**: Their extension system (we have plugins)

### OpenCode (Deep Audit — 2026-02-14)
- **Architecture**: TypeScript, Hono HTTP server + SSE streaming. Client/server split.
- **Agent system**: Resumable subagents with `task_id` for session continuity. Batch tool executes up to 25 tools in parallel via Promise.all. Rule-based permission system with glob patterns.
- **Steal**: ~~File watcher~~, ~~session revert/step-level undo~~, batch parallel execution (25 tools), formatter abstraction, header-based retry, resumable subagent pattern
- **Skip**: Client/server split (we're desktop-first)

### Cline (Deep Audit — 2026-02-14)
- **Architecture**: VS Code extension with polished IDE integration.
- **Agent system**: Parallel subagents — up to 5 read-only subagents simultaneously. Git-based checkpoints for workspace snapshots. Human-in-the-loop approval for dangerous operations. Hooks system similar to ours.
- **Steal**: ~~Checkpoints (3 restore modes)~~, ~~cost tracking~~, ~~streaming tool preview~~, parallel read-only subagents (5 concurrent), auto-condense tool
- **Skip**: VS Code-specific integrations (not applicable to desktop)

### Roo Code (New — 2026-02-14)
- **Architecture**: VS Code extension, fork of Cline with mode system.
- **Agent system**: 4 built-in modes (Code, Architect, Ask, Debug) + custom modes. Each mode restricts available tools differently. Boomerang pattern: parent agent delegates subtasks to child agents in different modes. `SwitchModeTool` for dynamic mode changes during execution. `ToolRepetitionDetector` for stuck detection at registry level.
- **Steal**: Dynamic mode switching during execution, tool repetition detection at registry level, configurable compaction thresholds
- **Skip**: Mode system itself (we have typed worker roles — same concept, more structured)

### Gemini CLI
- **Philosophy**: Google-scale safety with sandboxing.
- **Steal**: Sandbox execution (5 methods), checkpointing, token caching, trusted folders
- **Skip**: Enterprise RBAC, Google-specific auth

### Aider
- **Philosophy**: Terminal-first, git-centric, model-optimized.
- **Steal**: Git auto-commit, weak models, architect mode, tree-sitter, iterative linting, watch mode, voice
- **Skip**: Copy/paste web bridge (niche), Python-specific patterns

### Goose (Deep Audit — 2026-02-14)
- **Architecture**: Rust core with MCP-native tool architecture. All tools are MCP servers, even built-ins.
- **Agent system**: Lead-worker model with automatic routing — developer → researcher → data-analyst. Three-inspector pipeline: SecurityInspector (pattern-based blocking), PermissionInspector (user approval), RepetitionInspector (stuck detection). Confidence scores on security checks.
- **Sessions**: SQLite-based session storage. Visibility metadata on messages (user_visible vs agent_visible flags for compaction control). Auto-compaction at 80% context usage.
- **Extensibility**: YAML recipe system with cron scheduling, success checks, retry logic. Extension malware checking. Tool prefix namespacing (`ext__tool`).
- **Steal**: ~~Lead-worker auto-routing~~ **DONE** (Sprint 16: Praxis 3-tier surpasses Goose's 2-tier), ~~3-inspector security pipeline~~ **DONE**, ~~visibility metadata~~ **DONE**, SQLite sessions, ~~auto-compaction threshold~~ **DONE**, toolshim for non-tool-calling models
- **Skip**: MCP-native-only architecture (we support both native + MCP), Rust-specific patterns

### OpenHands
- **Philosophy**: Enterprise-grade with containerized execution.
- **Steal**: Containerized sandbox, trigger-based microagents
- **Skip**: Enterprise RBAC, multi-runtime (we're desktop-first)

### Plandex
- **Philosophy**: Plan-focused with version control.
- **Steal**: Version control on plans (rewind/branch), autonomy level matrix, auto-debug
- **Skip**: Their plan-only approach (we have broader scope)

---

*Last updated: 2026-02-28*
