# Frontend Backlog

> What's missing, prioritized. Updated 2026-02-09.

---

## Status Summary

| Phase | Status | Remaining |
|-------|--------|-----------|
| **1: Desktop App** | **Complete** | - |
| **1.5: Desktop Polish** | **Complete** | Manual testing only |
| **2: Plugin Ecosystem** | Not started | THE differentiator |
| **2+: Competitive Gaps** | Not started | High-impact quick wins |

---

## Phase 1.5 — Manual Testing (Ready)

These require running `npm run tauri dev` and manually verifying:

- [ ] Test full app flow (chat, tools, settings, sessions)
- [ ] Verify keyboard shortcuts (Ctrl+B, Ctrl+,, Ctrl+M, Ctrl+N)
- [ ] Test on multiple Linux DEs (GNOME, KDE, Cosmic)
- [ ] Test light mode across all components
- [ ] Test density settings (compact, default, comfortable)
- [ ] Test file explorer with real project directories
- [ ] Test agent persistence (create agent, switch session, verify loaded)

---

## Phase 2 — Competitive Gaps (Quick Wins)

High-impact features that other tools have. Each is ~1 week or less.

### P0: Git Auto-Commit on Edits
**What**: After each successful file edit, auto-commit with AI-generated message. `/undo` reverts last AI commit. Separates user vs AI work in git history.
**Who has it**: Aider, Gemini CLI
**Effort**: 1 week
**Frontend work**:
- Toggle in Settings (Behavior tab): "Auto-commit AI edits"
- Commit message shown in tool call card or notification
- Undo button in message actions or command palette
**Backend**: Wire `git add` + `git commit` after write_file/edit tool success. Weak model for commit message.

### P0: Weak Model for Secondary Tasks
**What**: Every main model has an associated cheap/fast model for commit messages, summaries, classification.
**Who has it**: Aider, PI
**Effort**: 1 week
**Frontend work**:
- "Secondary model" dropdown per provider in Settings (Providers tab)
- Show which model is being used in ToolCallCard (e.g., "Generating commit message with Haiku...")
**Backend**: Add `weakModel` field to provider config. Route secondary tasks to it.

### P1: Streaming Tool Preview
**What**: Show diffs and terminal output while the model is still generating, not after.
**Who has it**: Cline, OpenCode
**Effort**: 1 week
**Frontend work**:
- ToolCallCard renders incrementally as tool call is parsed from streaming response
- DiffViewer appears mid-stream, populated as content arrives
- Terminal output streams in real-time
**Backend**: Parse tool calls from partial streaming response, execute optimistically.

### P1: File Watcher + AI Comments
**What**: Monitor project files for `// AI!` and `// AI?` comments. Agent processes them as instructions.
**Who has it**: Aider, OpenCode
**Effort**: 1 week
**Frontend work**:
- Toggle in Settings (Behavior tab): "Watch for AI comments in files"
- Indicator in status bar when watcher is active
- Notification when AI comment detected + auto-queued
**Backend**: Tauri `notify` crate for file watching. Parse configurable comment patterns.

### P1: Architect + Editor Model Split
**What**: Team Lead uses expensive reasoning model, Junior Devs use cheaper editor model.
**Who has it**: Aider, PI
**Effort**: 1 week
**Frontend work**:
- "Editor model" dropdown in Settings (LLM tab)
- Agent cards show which model each agent is using
- Cost display distinguishes architect vs editor model usage
**Backend**: Add `editorModel` to agent config. Team Lead uses main model, Junior Devs use editor model.

### P2: Message Queue / Steering
**What**: Queue messages while agent is running. "Steering" interrupts, "follow-up" runs after.
**Who has it**: PI
**Effort**: 3-4 days
**Frontend work**:
- MessageInput stays active while agent is running (currently disabled)
- Visual indicator: "Message will be sent as steering/follow-up"
- Toggle between steering (interrupt) and follow-up (queue) mode
**Backend**: Add message queue to agent loop. Steering injects into current context.

### P2: Session Step-Level Undo
**What**: Undo individual steps within a session without forking.
**Who has it**: OpenCode, Cline, Plandex
**Effort**: 1 week
**Frontend work**:
- "Undo" button per tool call in MessageActions
- Shows file diffs that would be reverted
- Confirmation dialog for destructive undos
**Backend**: Track file diffs per step. Revert tool stores reverse diff.

---

## Phase 2 — Plugin Ecosystem (THE DIFFERENTIATOR)

This is what makes Estela "The Obsidian of AI Coding".

### Sprint 2.1: Plugin Format & SDK
- [ ] Define unified plugin manifest (skills + commands + hooks + MCP in one package)
- [ ] Plugin SDK with TypeScript types and helpers
- [ ] Plugin lifecycle (install, enable, disable, uninstall, reload)
- [ ] Plugin sandboxing (what plugins can/can't access)
**Frontend**: None yet (backend-first)

### Sprint 2.2: Plugin Development Experience
- [ ] `estela plugin init` scaffold command
- [ ] Hot reload during plugin development
- [ ] Plugin testing utilities
- [ ] Plugin documentation template
**Frontend**: Plugin dev panel showing reload status, logs

### Sprint 2.3: Built-in Marketplace UI
- [ ] Plugin browser in sidebar (replace SidebarPlugins placeholder — currently deleted)
- [ ] Search, categories, featured plugins
- [ ] Install/uninstall with one click
- [ ] Plugin settings page per plugin
**Frontend**: Major work — new sidebar view, plugin cards, search, install flow

### Sprint 2.4: Plugin Distribution
- [ ] Publish plugins from GitHub repos
- [ ] Plugin registry API
- [ ] Version management and updates
- [ ] Community ratings and reviews
**Frontend**: Publish flow in settings, update notifications

### Sprint 2.5: Starter Plugins
- [ ] 5-10 built-in plugins demonstrating the system
- [ ] Example: "React Patterns" skill plugin
- [ ] Example: "/deploy" command plugin
- [ ] Example: "Auto-commit" hook plugin
**Frontend**: Plugin showcase page

---

## Phase 3+ — Longer Term

| Feature | Effort | Frontend Impact |
|---------|--------|----------------|
| Sandbox / container execution | 2-3 weeks | Toggle in settings, status indicator |
| Tree-sitter for 100+ languages | 2 weeks | Better code highlighting, symbol extraction |
| Voice input | 2 weeks | Microphone button in MessageInput |
| CLI polish | 1-2 weeks | None (CLI-only) |
| ACP editor integration | 2 weeks | Minimal (backend protocol) |
| A2A agent network | 2 weeks | Agent discovery UI, remote agent cards |

---

## What's Complete (No Work Needed)

These were identified as gaps but are now fully implemented:

| Feature | Session | Status |
|---------|---------|--------|
| Checkpointing / time-travel undo | 40 | createCheckpoint, rollbackToCheckpoint, UI |
| Cost & token tracking | 44 | Per-message tokens+cost in bubbles, session total in ContextBar |
| Vision / image support | Multiple | Paste, drop, base64, multimodal API, inline display |
| Iterative lint-fix loop | 44 | autoFixLint setting, biome/eslint after edits, errors fed back |
| Memory recall | 45 | recallSimilar + procedural recall injected into system prompts |
| Auto-compaction | 45 | Sliding window when context > 80%, syncs state + DB + tracker |
| File explorer | 45 | Recursive tree, lazy-load, Tauri FS |
| Code editor file reading | 45 | readFileContent via Tauri FS, auto-open from explorer |
| Agent persistence | 45 | DB CRUD (save, get, update), wired in session store |
| Google models API | 45 | Dynamic fetch with hardcoded fallback |
| DiffViewer split view | 45 | buildSplitPairs, two-column rendering |
| Dark/light/system theme | 41 | With midnight + charcoal dark variants |
| 6 accent colors + custom hex | 41 | hexToAccentVars computes all accent vars |
| 6 code themes | 41 | Via data-code-theme attribute |
| UI density (3 levels) | 42 | 8 components wired |
| Custom instructions | 44 | Injected as system message |
| Desktop notifications | 44 | Unfocused-only + AudioContext chime |
| Settings export/import | 44 | JSON download, file picker, deep merge |

---

## Unique Advantages (Estela vs Everyone)

Features no other AI coding tool has:

| Feature | Status |
|---------|--------|
| Multi-agent hierarchy (Team Lead + Senior Leads + Junior Devs) | Built, visible in UI |
| Worker scope filtering (each agent sees only relevant files/tools) | Built |
| Parallel agent execution | Built |
| Auto-reporting (workers report up the chain) | Built |
| User intervention points (click into any agent's chat) | Built |
| Doom loop detection | Built |
| Validator/QA pipeline (syntax, types, lint, test, review) | Built |
| Codebase intelligence (PageRank, dependency graph, symbols) | Built |
| Memory system (episodic + semantic + procedural + RAG) | Built |
| Permission/policy engine (risk assessment, auto-approval) | Built |
| Hook system (PreToolUse, PostToolUse, lifecycle) | Built |
| Plugin marketplace | Phase 2 (planned) |
| Protocol support (ACP + A2A) | Built (backend) |
