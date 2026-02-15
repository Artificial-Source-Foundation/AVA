# Competitive Gap Analysis

> AVA vs 7 reference codebases + PI Coding Agent. Compiled 2026-02-08.
>
> Sources analyzed: **OpenCode**, **Aider**, **Cline**, **Gemini CLI**, **Goose**, **OpenHands**, **Plandex**, **PI Coding Agent**

---

## Executive Summary

AVA has strong foundations in multi-agent orchestration, codebase intelligence, memory, and permissions — areas where most competitors have nothing. But there are **15 high-impact gaps** that multiple codebases implement and AVA lacks entirely.

**Closed since last update (Sessions 48-53):** message queue, file watcher, step-level undo, git auto-commit, weak/editor model split, streaming tool preview.

**Biggest gaps by frequency** (how many codebases have it):

| Gap | Who Has It | Priority |
|-----|-----------|----------|
| ~~Checkpointing / time-travel undo~~ | Cline, Gemini CLI, Plandex, Aider | ~~Critical~~ **DONE** |
| ~~Cost/token tracking UI~~ | Cline, Aider, PI, Gemini CLI | ~~Critical~~ **DONE** |
| ~~Vision/image support~~ | Cline, Aider, PI | ~~High~~ **DONE** |
| ~~Git auto-commit on edits~~ | Aider, Gemini CLI | ~~**High**~~ **DONE** |
| ~~Weak model for secondary tasks~~ | Aider, PI | ~~**High**~~ **DONE** |
| ~~Architect + editor model split~~ | Aider, PI | ~~**Medium**~~ **DONE** |
| Sandbox/container execution | Gemini CLI, OpenHands, Goose | **High** |
| ~~Iterative lint → fix loop~~ | Aider, Cline, Gemini CLI | ~~High~~ **DONE** |
| ~~Streaming tool preview~~ | Cline, OpenCode | ~~**Medium**~~ **DONE** |
| ~~File watcher / AI comments~~ | Aider, OpenCode | ~~**Medium**~~ **DONE** |
| ~~Architect + editor model split~~ | Aider, PI | ~~**Medium**~~ **DONE** |
| ~~Session revert/step-level undo~~ | OpenCode, Cline, Plandex | ~~**Medium**~~ **DONE** |
| ~~Message queue (steering/follow-up)~~ | PI | ~~**Medium**~~ **DONE** |
| Tree-sitter (100+ languages) | Aider | **Medium** |
| RPC/SDK mode for embedding | PI, Aider | **Low** |
| Telemetry/analytics | Cline, Gemini CLI | **Low** |

### New Gaps Identified (2026-02-10 Audit)

| Gap | Source | Priority |
|-----|--------|----------|
| Mid-session provider switching | PI | **Medium** |
| Session branching tree (true forks, shared prefix) | PI | **Medium** |
| Minimal tool mode (4-tool subset) | PI | Low |
| Runtime skill/tool creation + hot reload | PI | Medium |
| MCP OAuth flows (auth + refresh + storage) | Cline, Gemini CLI, Goose | Medium |
| Remote browser support | Cline | Medium |

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

### 3. Vision / Image Support (HIGH)

**What**: Accept image uploads in chat (paste, drag-drop, file picker). Send to vision-capable models. Display inline.

**Who has it**:
- **Cline**: Image uploads, clipboard paste, vision model detection, lazy-load display.
- **Aider**: `/add image.png`, `/paste` from clipboard, auto-detects vision models.
- **PI**: Image input support in chat.

**AVA status**: Browser tool takes screenshots but images can't be input to chat. No vision model detection.

**Impact**: Visual debugging (screenshot → fix), mockup → code, diagram → implementation.

**Implementation**: Add image handling to `MessageInput.tsx`, detect vision-capable models, base64 encode for API.

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

**AVA status**: No sandboxing. Bash tool executes directly on host. Permission system is the safety layer.

**Impact**: Safety for autonomous agents. Critical for "let it run overnight" workflows.

**Implementation**: Phase 2+. Could use Docker or Tauri's IPC to isolate bash execution.

---

### 7. Iterative Lint → Fix Loop (HIGH)

**What**: After edit → run linter → if errors → send errors back to model → model fixes → repeat until clean.

**Who has it**:
- **Aider**: `linter.py` with `find_filenames_and_linenums()`. Auto-retry loop.
- **Cline**: Editor integration with VS Code Problems panel.
- **Gemini CLI**: Auto-test + auto-lint with feedback loop.

**AVA status**: `validator/` module has linting checks (syntax, TypeScript, eslint) but it's **read-only** — doesn't feed errors back to the model for retry.

**Impact**: Dramatically improves first-time code correctness. Agent fixes its own mistakes.

**Implementation**: After edit → run `validator` → if errors → inject into next LLM turn → retry. Max 3 retries.

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
| **Multi-agent hierarchy** (Team Lead → Seniors → Juniors) | Built-in with visible UI | None (all are single-agent) |
| **Worker scope filtering** (each agent only sees relevant files/tools) | Native | None |
| **Parallel agent execution** (multiple seniors simultaneously) | Built-in | Plandex (limited) |
| **Auto-reporting** (workers report up the chain) | Native | None |
| **User intervention points** (click into any agent's chat) | Desktop UI | None |
| **Doom loop detection** (prevent agents getting stuck) | Built-in | Gemini CLI (basic) |
| **Validator/QA pipeline** (syntax, types, lint, test, review) | 9-file module | Aider (basic lint) |
| **Codebase intelligence** (PageRank, dependency graph, symbols) | Built-in | Aider (repo map only) |
| **Memory system** (episodic + semantic + procedural + RAG) | Built-in | None |
| **Permission/policy engine** (risk assessment, auto-approval, rules) | Built-in | None (PI defers to user) |
| **Hook system** (PreToolUse, PostToolUse, lifecycle) | Native | None |
| **Desktop UI** (activity bar, agent cards, animations) | Tauri + SolidJS | Cline (VS Code only) |
| **Plugin marketplace** (Phase 2) | Planned | PI (npm only) |
| **Protocol support** (ACP + A2A) | Built-in | None |

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
| 11 | **Sandbox execution** | 2-3 weeks | High | Gemini CLI, OpenHands |
| 12 | **Tree-sitter integration** | 2 weeks | Medium | Aider |
| 13 | ~~**Session step-level undo**~~ | ~~1 week~~ | ~~Medium~~ **DONE** | OpenCode, Cline |
| 14 | **Voice coding** | 2 weeks | Medium | Aider |
| 15 | **RPC/SDK mode** | 2-3 weeks | Low | PI |

---

## Per-Codebase Key Takeaways

### PI Coding Agent (v0.52.9)
- **Philosophy**: Minimal core, maximum extensibility. "No permission popups."
- **Steal**: Mid-session provider switching, session branching tree, minimal tool mode, runtime skill creation, RPC mode
- **Skip**: Their extension system (we have plugins)

### OpenCode
- **Philosophy**: Terminal-first with rich TUI. Client/server architecture.
- **Steal**: ~~File watcher~~, ~~session revert/step-level undo~~, formatter abstraction, header-based retry
- **Skip**: Client/server split (we're desktop-first)

### Cline
- **Philosophy**: VS Code extension with polished IDE integration.
- **Steal**: ~~Checkpoints (3 restore modes)~~, ~~cost tracking~~, ~~streaming tool preview~~, auto-condense tool
- **Skip**: VS Code-specific integrations (not applicable to desktop)

### Gemini CLI
- **Philosophy**: Google-scale safety with sandboxing.
- **Steal**: Sandbox execution (5 methods), checkpointing, token caching, trusted folders
- **Skip**: Enterprise RBAC, Google-specific auth

### Aider
- **Philosophy**: Terminal-first, git-centric, model-optimized.
- **Steal**: Git auto-commit, weak models, architect mode, tree-sitter, iterative linting, watch mode, voice
- **Skip**: Copy/paste web bridge (niche), Python-specific patterns

### Goose
- **Philosophy**: Extension-based with YAML recipes.
- **Steal**: Recipe workflows (declarative task sequences), MCP allowlists
- **Skip**: Extension system specifics (we have plugins)

### OpenHands
- **Philosophy**: Enterprise-grade with containerized execution.
- **Steal**: Containerized sandbox, trigger-based microagents
- **Skip**: Enterprise RBAC, multi-runtime (we're desktop-first)

### Plandex
- **Philosophy**: Plan-focused with version control.
- **Steal**: Version control on plans (rewind/branch), autonomy level matrix, auto-debug
- **Skip**: Their plan-only approach (we have broader scope)

---

*Last updated: 2026-02-10*
