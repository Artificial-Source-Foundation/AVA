# Competitive Gap Analysis

> Estela vs 7 reference codebases + PI Coding Agent. Compiled 2026-02-08.
>
> Sources analyzed: **OpenCode**, **Aider**, **Cline**, **Gemini CLI**, **Goose**, **OpenHands**, **Plandex**, **PI Coding Agent**

---

## Executive Summary

Estela has strong foundations in multi-agent orchestration, codebase intelligence, memory, and permissions — areas where most competitors have nothing. But there are **15 high-impact gaps** that multiple codebases implement and Estela lacks entirely.

**Biggest gaps by frequency** (how many codebases have it):

| Gap | Who Has It | Priority |
|-----|-----------|----------|
| ~~Checkpointing / time-travel undo~~ | Cline, Gemini CLI, Plandex, Aider | ~~Critical~~ **DONE** |
| ~~Cost/token tracking UI~~ | Cline, Aider, PI, Gemini CLI | ~~Critical~~ **DONE** |
| ~~Vision/image support~~ | Cline, Aider, PI | ~~High~~ **DONE** |
| Git auto-commit on edits | Aider, Gemini CLI | **High** |
| Weak model for secondary tasks | Aider, PI | **High** |
| Sandbox/container execution | Gemini CLI, OpenHands, Goose | **High** |
| ~~Iterative lint → fix loop~~ | Aider, Cline, Gemini CLI | ~~High~~ **DONE** |
| Streaming tool preview | Cline, OpenCode | **Medium** |
| File watcher / AI comments | Aider, OpenCode | **Medium** |
| Architect + editor model split | Aider, PI | **Medium** |
| Session revert/step-level undo | OpenCode, Cline, Plandex | **Medium** |
| Message queue (steering/follow-up) | PI | **Medium** |
| Tree-sitter (100+ languages) | Aider | **Medium** |
| RPC/SDK mode for embedding | PI, Aider | **Low** |
| Telemetry/analytics | Cline, Gemini CLI | **Low** |

---

## Gap Details

### 1. Checkpointing / Time-Travel Undo (CRITICAL)

**What**: Snapshot workspace + session state at every step. User can rollback to any previous point — files, messages, or both.

**Who has it**:
- **Cline**: 3 restore modes (task only, workspace only, both). `CheckpointOverlay` UI with confirmation dialog.
- **Gemini CLI**: Full checkpoint system with git-based snapshots. `/checkpoint` command.
- **Plandex**: Version control on plans with rewind/branch. Every plan change creates a versioned snapshot.
- **Aider**: Git auto-commit = implicit checkpoint. `/undo` reverts last commit.

**Estela status**: Session forking exists ("Fork from here") but it's one-time branching, not iterative checkpointing. `createCheckpoint()` / `rollbackToCheckpoint()` were scaffolded in Session 40 but not wired to UI.

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

**Estela status**: Internal `ContextTracker` counts tokens. `ContextBar.tsx` shows a progress bar. No cost calculation, no per-request metrics, no cache tracking.

**Impact**: Users managing API budgets need cost visibility. #1 requested feature in most AI coding tools.

**Implementation**: Add cost calculation per provider (price per 1K tokens). Show in `ContextBar.tsx` or new `CostDisplay` component.

---

### 3. Vision / Image Support (HIGH)

**What**: Accept image uploads in chat (paste, drag-drop, file picker). Send to vision-capable models. Display inline.

**Who has it**:
- **Cline**: Image uploads, clipboard paste, vision model detection, lazy-load display.
- **Aider**: `/add image.png`, `/paste` from clipboard, auto-detects vision models.
- **PI**: Image input support in chat.

**Estela status**: Browser tool takes screenshots but images can't be input to chat. No vision model detection.

**Impact**: Visual debugging (screenshot → fix), mockup → code, diagram → implementation.

**Implementation**: Add image handling to `MessageInput.tsx`, detect vision-capable models, base64 encode for API.

---

### 4. Git Auto-Commit on Edits (HIGH)

**What**: After each successful edit, auto-commit with AI-generated message. Separates user work from AI work. Enables undo.

**Who has it**:
- **Aider**: Auto-commit after every edit. Weak model generates commit message. `/undo` reverts last. Author attribution: `"User (aider)"`.
- **Gemini CLI**: Git checkpoint integration for time-travel.

**Estela status**: `git/` module exists with snapshot capabilities but no auto-commit workflow.

**Impact**: Safety net. Never lose work. Git history shows exactly what AI changed vs what user changed.

**Implementation**: After tool `write_file`/`edit` succeeds → `git add` + `git commit` with weak model message.

---

### 5. Weak Model for Secondary Tasks (HIGH)

**What**: Every main model has an associated cheap/fast model for secondary tasks (commit messages, summaries, simple fixes).

**Who has it**:
- **Aider**: `weak_model_name` per model. Claude Sonnet → Claude Haiku for commits.
- **PI**: Different models for different sub-tasks.

**Estela status**: Model selector in UI lets user choose main model. No concept of secondary model for cheaper tasks.

**Impact**: 50-80% cost reduction on secondary tasks (commit messages, summaries, simple classification).

**Implementation**: Add `weakModel` field to provider config. Use for: commit messages, task summaries, error classification.

---

### 6. Sandbox / Container Execution (HIGH)

**What**: Run code in isolated environment (Docker, gVisor, nsjail) to prevent unsafe operations on host.

**Who has it**:
- **Gemini CLI**: 5 sandbox methods (Docker, gVisor, nsjail, SELinux, manual).
- **OpenHands**: Full containerized sandbox — every agent runs in Docker with mounted workspace.
- **Goose**: Extension-based sandboxing.

**Estela status**: No sandboxing. Bash tool executes directly on host. Permission system is the safety layer.

**Impact**: Safety for autonomous agents. Critical for "let it run overnight" workflows.

**Implementation**: Phase 2+. Could use Docker or Tauri's IPC to isolate bash execution.

---

### 7. Iterative Lint → Fix Loop (HIGH)

**What**: After edit → run linter → if errors → send errors back to model → model fixes → repeat until clean.

**Who has it**:
- **Aider**: `linter.py` with `find_filenames_and_linenums()`. Auto-retry loop.
- **Cline**: Editor integration with VS Code Problems panel.
- **Gemini CLI**: Auto-test + auto-lint with feedback loop.

**Estela status**: `validator/` module has linting checks (syntax, TypeScript, eslint) but it's **read-only** — doesn't feed errors back to the model for retry.

**Impact**: Dramatically improves first-time code correctness. Agent fixes its own mistakes.

**Implementation**: After edit → run `validator` → if errors → inject into next LLM turn → retry. Max 3 retries.

---

### 8. Streaming Tool Preview (MEDIUM)

**What**: Show tool results (diffs, terminal output) while the model is still generating its response, not after.

**Who has it**:
- **Cline**: `handlePartialBlock()` in tool handlers. See diffs appearing in real-time.
- **OpenCode**: Streaming tool execution with live terminal output.

**Estela status**: Streams LLM text but tool results appear only after complete response.

**Impact**: Better perceived performance. User sees progress immediately.

**Implementation**: Parse tool calls from partial streaming response, execute optimistically.

---

### 9. File Watcher / AI Comments in Code (MEDIUM)

**What**: Monitor project files for special comments (`// AI!`, `// AI?`). When detected, agent processes the comment as an instruction.

**Who has it**:
- **Aider**: `watch.py` — `AI!` triggers edit, `AI?` triggers question. Works with any language.
- **OpenCode**: File watcher for reactive workflows.

**Estela status**: No file monitoring. Desktop app doesn't watch user's editor files.

**Impact**: Seamless IDE integration without VS Code extension. User writes comment, switches to Estela, sees result.

**Implementation**: Add file watcher via Tauri's `notify` crate. Parse comments with configurable patterns.

---

### 10. Architect + Editor Model Split (MEDIUM)

**What**: Use expensive reasoning model for planning, cheaper model for actual file edits. Separates "what to do" from "how to write it".

**Who has it**:
- **Aider**: `architect_coder.py`. o1 (architect) + Sonnet (editor). `editor-diff` format.
- **PI**: Extensible model routing for different task phases.

**Estela status**: Team Lead plans and delegates, but all agents use the same model. No explicit architect/editor split.

**Impact**: Better results with reasoning models (o1, DeepSeek R1) that plan well but edit poorly. Cost savings.

**Implementation**: Add `editorModel` to agent config. Team Lead uses main model, Junior Devs use editor model.

---

### 11. Session Revert / Step-Level Undo (MEDIUM)

**What**: Undo individual steps within a session, not just fork from a point.

**Who has it**:
- **OpenCode**: Session revert with step-level granularity.
- **Cline**: Checkpoint restore (task, workspace, or both).
- **Plandex**: Plan rewind/branch with version control.

**Estela status**: Fork creates a new session from a point. No in-place undo.

**Impact**: Lighter than forking — just undo last step without creating new session.

**Implementation**: Track file diffs per step. `/undo` reverts last step's changes.

---

### 12. Message Queue / Steering Interrupts (MEDIUM)

**What**: Queue messages to interrupt agent mid-execution (steering) or run after current task (follow-up).

**Who has it**:
- **PI**: Message queue with steering (interrupt) and follow-up (after completion) modes.

**Estela status**: No message queue. User must wait for agent to finish before sending next message.

**Impact**: Better for long-running tasks. User can steer without canceling.

**Implementation**: Add message queue to `useChat`. If agent is running, queue as steering or follow-up.

---

### 13. Tree-Sitter for 100+ Languages (MEDIUM)

**What**: Use tree-sitter grammars for symbol extraction instead of LSP (which requires language servers).

**Who has it**:
- **Aider**: `repomap.py` with tree-sitter. 100+ language support. Disk-based tag cache.

**Estela status**: `codebase/` module uses LSP-based symbols (5 languages: TS, Python, Go, Rust, Java).

**Impact**: Support any language without installing language servers. Faster than LSP for simple symbol extraction.

**Implementation**: Add `web-tree-sitter` with WASM grammars. Cache symbols to SQLite.

---

### 14. RPC/SDK Mode for Embedding (LOW)

**What**: Run as a library or RPC server for embedding in other applications.

**Who has it**:
- **PI**: `createAgentSession()` SDK + `--mode rpc` for stdin/stdout protocol.
- **Aider**: Python library mode.

**Estela status**: Planned for Phase 4. Core is importable as library but no RPC server.

**Impact**: Enables embedding Estela in CI/CD, custom tools, other apps.

---

### 15. Telemetry / Analytics (LOW)

**What**: Track feature usage, error rates, session metrics for product improvement.

**Who has it**:
- **Cline**: PostHog + OpenTelemetry.
- **Gemini CLI**: Enterprise-grade telemetry.

**Estela status**: None. Privacy-first philosophy.

**Impact**: Product improvement data. Optional/opt-in is fine.

---

## What Estela Has That Others Don't

These are Estela's **unique advantages** — features no other codebase implements:

| Feature | Estela | Closest Competitor |
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
| 5 | **Git auto-commit** | 1 week | High | Aider |
| 6 | **Weak model support** | 1 week | High | Aider |
| 7 | **Architect/editor model split** | 1 week | Medium | Aider |
| 8 | **Streaming tool preview** | 1 week | Medium | Cline |
| 9 | **File watcher + AI comments** | 1 week | Medium | Aider, OpenCode |
| 10 | **Message queue** | 3-4 days | Medium | PI |

### Phase 3+ (Longer Term)

| # | Feature | Effort | Impact | Source |
|---|---------|--------|--------|--------|
| 11 | **Sandbox execution** | 2-3 weeks | High | Gemini CLI, OpenHands |
| 12 | **Tree-sitter integration** | 2 weeks | Medium | Aider |
| 13 | **Session step-level undo** | 1 week | Medium | OpenCode, Cline |
| 14 | **Voice coding** | 2 weeks | Medium | Aider |
| 15 | **RPC/SDK mode** | 2-3 weeks | Low | PI |

---

## Per-Codebase Key Takeaways

### PI Coding Agent (v0.52.9)
- **Philosophy**: Minimal core, maximum extensibility. "No permission popups."
- **Steal**: Message queue (steering + follow-up), RPC mode, scoped models, hot reload
- **Skip**: Their extension system (we have plugins)

### OpenCode
- **Philosophy**: Terminal-first with rich TUI. Client/server architecture.
- **Steal**: File watcher, session revert/step-level undo, formatter abstraction, header-based retry
- **Skip**: Client/server split (we're desktop-first)

### Cline
- **Philosophy**: VS Code extension with polished IDE integration.
- **Steal**: Checkpoints (3 restore modes), cost tracking, streaming tool preview, auto-condense tool
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

*Last updated: 2026-02-09*
