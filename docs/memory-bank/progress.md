# Progress Log

> Quick status overview — detailed history in [sessions/archive.md](./sessions/archive.md)

---

## Current Session

**Session 55** (2026-02-10) — Debug Logging Pass
- Added structured logging for chat flow (send/queue/steer/cancel/stream errors)
- Added core bridge init/dispose logging
- Added file watcher start/stop/dedup logging
- Replaced console warnings in settings/session with file logger
- Logged tool approval resolution in ChatView

**Session 54** (2026-02-10) — Planning & Audit
- Codebase audit: LOC, tests, typecheck, Biome, TODO/FIXME, console.log scan, git status, file size violations
- Reference comparisons: PI, Cline, Gemini CLI, OpenCode, Aider, Goose, Plandex, OpenHands
- Test plans drafted: OAuth flows + message flow (queue/steer/cancel/watch)
- Logging audit across chat/agent/core/session/settings/file-watcher/ChatView
- Sprint 1.6 planning + tickets (testing + debug logging + PI parity)

**Session 53** (2026-02-09) — File Watcher + Step-Level Undo
- File watcher service (`src/services/file-watcher.ts`) — Tauri FS `watch()`, 6 AI patterns, 30+ extensions, dedup
- ChatView wiring — starts/stops watcher based on settings + project dir, auto-sends AI comments as chat messages
- Undo button in MessageInput toolbar — calls `undoLastAutoCommit()` (git revert), 2.5s feedback, only visible when auto-commit enabled
- FS permissions — `fs:allow-watch`, `fs:allow-unwatch` in Tauri capabilities
- Streaming tool preview confirmed already working (reactive chain: onToolUpdate → ToolCallGroup → ToolCallCard)
- Gap scorecard: 12/15 DONE. Only sandbox, tree-sitter, RPC, telemetry remain (Phase 3+)
- 0 TS errors, 0 Biome errors, vite build passes

**Session 52** (2026-02-09) — Message Queue + Steering Interrupts
- Message queue in `useChat` — queues follow-ups during streaming, auto-dequeues after completion
- `steer()` cancels current stream + sends new message immediately
- Type-ahead enabled during streaming, queue badge, send/queue button style change
- `Ctrl+Shift+Enter` = steer, cancel clears queue, session switch clears queue
- 0 TS errors, 0 Biome errors, vite build passes

**Session 51** (2026-02-09) — OAuth Fix + Error Logging
- Fixed OpenAI OAuth "insufficient permissions" — tokens stored as `type: 'oauth'` via `setStoredAuth()`
- JWT parsing for ChatGPT `accountId`, reverted `model.request` scope, CSP updated
- OAuth disconnect UI, structured error logging, browser opener fix, PKCE guard
- 0 TS errors, 0 Biome errors, vite build passes

**Session 50** (2026-02-09) — Architect + Editor Model Split
- `editorModel` + `editorModelProvider` on core `ProviderSettings`
- `getEditorModelConfig()` helper, commander executor auto-applies to workers
- Frontend: dropdown in LLMTab with auto-pair suggestions (Opus → Sonnet, etc.)

**Session 49** (2026-02-09) — Weak Model for Secondary Tasks
- `weakModel` + `weakModelProvider` on core `ProviderSettings`
- `getWeakModelConfig()` helper, planner + self-review wired
- Frontend: dropdown in LLMTab with auto-pair suggestions (Sonnet → Haiku, etc.)

**Session 48** (2026-02-09) — Git Auto-Commit
- `packages/core/src/git/auto-commit.ts` — stages + commits after file-modifying tools
- Tool registry PostToolUse wiring, `undoLastAutoCommit()` for revert
- Frontend: `GitSettings` in BehaviorTab (enabled, autoCommit, commitPrefix)

**Session 47** (2026-02-09) — Backend Gaps Fix + Docs Reorg
- **Paste collapse** — Large text pastes collapsed into chips, user messages >8 lines collapse in bubble
- **Tool approval bridge** — Message bus → SolidJS signal → ToolApprovalDialog → response back to bus
- **MCP settings CRUD** — `mcpServers[]` with add/remove/update, wired to SettingsModal
- **FS scope expansion** — Rust `allow_project_path` command via `FsExt` for project file access
- **Shell timeout** — `Promise.race()` wrapper in TauriShell.exec()
- **OAuth fix** — Corrected Anthropic + OpenAI configs (client IDs, ports, scopes)
- **Docs reorg** — 8 priority fixes: README, techContext, architecture, database-schema, VISION, docs index, research index, Epic 25 moved to completed
- 0 TS errors, 0 Biome errors, vite build passes

**Session 46** (2026-02-09) — Settings Hardening + Gap Closure
- **16 new settings** across 4 sub-interfaces: GenerationSettings, AgentLimitSettings, BehaviorSettings, NotificationSettings
- **2 new tabs** — LLM (maxTokens, temperature, topP, custom instructions, agent limits) + Behavior (sendKey, autoScroll, autoTitle, lineNumbers, wordWrap, notifications, sound)
- **3 new files** — `LLMTab.tsx`, `BehaviorTab.tsx`, `src/services/notifications.ts`
- **4 hardcoded values wired** — maxTokens/temperature to useChat, agentMaxTurns/maxTimeMinutes to useAgent
- Custom instructions injected as system message in `buildApiMessages()` via `msgs.unshift()`
- Configurable send key (Enter vs Ctrl+Enter) in MessageInput + dynamic ShortcutHint
- Desktop notifications (unfocused-only) + AudioContext chime with configurable volume
- Code block settings: `[data-line-numbers]` CSS counter + `[data-word-wrap]` pre-wrap
- Data management: export (JSON download), import (file picker + deep merge), clear all
- **Gap closure**: Cost tracking UI (per-message + session), vision/image support (paste/drop/multimodal), iterative lint→fix (autoFixLint after file edits), checkpoint UI (create/display/restore), per-message token display
- Appearance expansion: system theme, dark variants, code themes, ligatures, chat font size, custom accent, sans font, high contrast
- Density recalibration: compact/default/comfortable, 8 components wired, CSS utility classes
- 0 TS errors, 0 Biome errors, vite build passes

**Session 45** (2026-02-08) — Competitive Gap Analysis
- Analyzed 8 reference codebases via 6 parallel subagents
- Created `docs/backend/gap-analysis.md` — 300+ line competitive comparison
- **15 gaps identified**, **14 unique advantages** documented
- Prioritized roadmap: 4 items for Phase 1.5, 6 for Phase 2, 5 for Phase 3+

**Session 44** (2026-02-08) — Backend Documentation
- Created `docs/backend/` folder with 5 files (937 lines)
- Architecture overview, per-module docs, test coverage, backlog, changelog

**Session 43** (2026-02-08) — Backend Test Coverage Phase 2
- **706 new tests** across 15 new test files (+ 2 helper files) covering Agent, Tools, LLM modules
- **Agent tests:** evaluator (35), events (53), recovery (107), planner (32), plan mode (36) = **263 tests**
- **Tools tests:** utils (95), sanitize (76), truncation (22), locks (33), completion (31), validation (31), define (40), todo (19), edit-replacers (65) = **412 tests**
- **LLM tests:** client (31) = **31 tests**
- **Total: 1778 tests** across 64 test files (was 1072)
- Fixed 5 missing module exports in `packages/core/src/index.ts` (bus, custom-commands, extensions + collision fixes for a2a, policy)
- 0 TS errors, 0 biome errors, vite build passes

**Session 40** (2026-02-08) — Core Frontend Wiring
- **Core bridge** — `src/services/core-bridge.ts` initializes 5 core singletons (SettingsManager, ContextTracker, WorkerRegistry, MemoryManager) at startup
- **Settings sync** — `pushSettingsToCore()` maps frontend AppSettings → core SettingsManager categories (provider, permissions, context, memory)
- **Context tracking** — `useChat` tracks tokens via ContextTracker on send/complete; `session.ts` contextUsage falls back to rough estimate when tracker unavailable
- **ContextBar** — `src/components/chat/ContextBar.tsx` shows token usage with progress bar below chat input
- **Session checkpoints** — `createCheckpoint()` / `rollbackToCheckpoint()` using memoryItems DB table (type: 'checkpoint')
- **Agent memory** — Episodic memory recorded on successful agent runs via `getCoreMemory().remember()`
- **New files:** `src/services/core-bridge.ts`, `src/components/chat/ContextBar.tsx`
- **Modified:** `App.tsx`, `settings.ts`, `useChat.ts`, `session.ts`, `useAgent.ts`, `ChatView.tsx`, `MemoryPanel.tsx`, `types/index.ts`

**Session 39** (2026-02-08) — Backend Testing + Appearance Tab
- **536 backend tests** across 24 files for Config, Context, Memory, Session, Commander modules
- **Appearance tab** — Dark/light mode, 6 accent colors, UI scale, mono font, border radius, density, reduce motion
- **Settings redesign** — All 4 tabs rewritten to flat minimal rows, OpenCode-inspired modal
- **Layout rework** — Activity bar slimmed (7→2 icons), model selector, right panel, bottom panel
- **Permission button** — Moved to MessageInput toolbar, ChatInfoBar deleted

---

## Recent Sessions

**Session 38** (2026-02-08) — WebKitGTK + Splash + Layout
- DMABUF ghost rendering fix, nested button crash fix, Cargo linker fix
- Splash screen, layout refactoring (navigation store deleted, sidebar slimmed)



**Session 37** (2026-02-07) — Phase 1 Completion
- Provider expansion (14 providers, Google + Copilot OAuth, DeviceCodeDialog)
- Team delegation flow (SVG animated lines, ParallelBadge, PhaseTimeline, parentId fix)
- Session fork ("Fork from here" context menu, message count display)
- Plugin browser shell (Plugins activity tab, built-in skills list)
- PI Coding Agent research (`docs/research/pi-coding-agent.md`)

**Session 36** (2026-02-07) — Frontend Gaps
- Working directory fix, tool approval wired, session duplicate, dead code removed (-975 lines)

**Session 35** (2026-02-07) — LLM Integration
- Credential bridge + browser access header -> streaming AI responses working

**Session 34** (2026-02-07) — Polish
- Sidebar width toggle fix, noise texture removal, settings scroll GPU promotion, Biome/a11y cleanup

**Session 33** (2026-02-05) — MVP Sprints + Hardening
- 7 MVP sprints, Tauri hardening (CSP, scoped FS, deferred window, release profile)

**Session 32** (2026-02-05) — Vision Alignment
- Defined "The Obsidian of AI Coding", rewrote all docs, deleted 60+ stale files

**Session 31** (2026-02-05) — IDE Layout Redesign
- Activity Bar, contextual sidebar, bottom panel, keyboard shortcuts

**Session 30** (2026-02-05) — Epic 25 + 26
- Gemini CLI Feature Parity (337 tests), ACP + A2A protocols (97 tests)

**Session 29** (2026-02-05) — UI Modernization
- 8 phases: glass tokens, spring physics, glassmorphism, resizable panels, CodeMirror, polish

---

## Milestones

| Date | Milestone |
|------|-----------|
| 2026-01-28 | Project scaffold (Tauri + SolidJS + SQLite) |
| 2026-01-29 | Epic 1: Multi-provider LLM streaming |
| 2026-01-30 | Epic 2: File tools (7 tools) |
| 2026-02-02 | Epics 3-7: ACP monorepo, safety, context, DX, platform |
| 2026-02-03 | Epics 8-17: Agent system, commander, parallel, validator, codebase, config, memory, tools |
| 2026-02-04 | Epics 19-21: Hooks, browser, providers. Feature parity sprints 1-7 |
| 2026-02-05 | Epics 25-26: ACP/A2A protocols, Gemini CLI features. UI modernization. IDE layout. Vision alignment |
| 2026-02-07 | **LLM integration working. Phase 1 complete.** |
| 2026-02-08 | **WebKitGTK fixes. Splash. Layout rework. Appearance tab. Backend tests (1778). Core frontend wiring.** |
| 2026-02-09 | **Settings hardening (16 settings). Gap closure: cost tracking, vision, lint→fix, checkpoints, per-message tokens.** |
| 2026-02-09 | **Sessions 48-53: git auto-commit, weak/editor models, OAuth fix, message queue, file watcher, step-level undo. 12/15 gaps DONE.** |
| 2026-02-10 | **Session 54: Audit + planning for Sprint 1.6 (testing, logging, PI parity).** |
| 2026-02-10 | **Session 55: Debug logging pass across chat/core/session/settings/file-watcher.** |

---

## What's Left

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 1: Desktop App | **Done** | Core UI, chat, LLM, settings, team flow |
| Phase 1.5: Polish | **Feature-complete** | All features done. Manual testing remains (Tauri dev, shortcuts, multi-DE) |
| Phase 2: Plugin Ecosystem | Next | Plugin SDK, marketplace, community |
| Phase 3: Polish & Community | Future | CLI, plugin wizard, docs site |
| Phase 4: Integrations | Future | ACP (editor), A2A (agents), voice, vision |

See `docs/ROADMAP.md` for sprint-level breakdown of each phase.
