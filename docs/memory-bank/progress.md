# Progress Log

> Quick status overview — detailed history in [sessions/archive.md](./sessions/archive.md)

---

## Current Session

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
| 2025-01-28 | Project scaffold (Tauri + SolidJS + SQLite) |
| 2025-01-29 | Epic 1: Multi-provider LLM streaming |
| 2025-01-30 | Epic 2: File tools (7 tools) |
| 2026-02-02 | Epics 3-7: ACP monorepo, safety, context, DX, platform |
| 2026-02-03 | Epics 8-17: Agent system, commander, parallel, validator, codebase, config, memory, tools |
| 2026-02-04 | Epics 19-21: Hooks, browser, providers. Feature parity sprints 1-7 |
| 2026-02-05 | Epics 25-26: ACP/A2A protocols, Gemini CLI features. UI modernization. IDE layout. Vision alignment |
| 2026-02-07 | **LLM integration working. Phase 1 complete.** |
| 2026-02-08 | **WebKitGTK fixes. Splash. Layout rework. Appearance tab. Backend tests (536). Core frontend wiring (1072 total tests).** |

---

## What's Left

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 1: Desktop App | **Done + polish** | Core UI, chat, LLM, settings, team flow |
| Phase 1.5: Polish | **In progress** | Splash screen, WebKitGTK fixes, layout cleanup, testing |
| Phase 2: Plugin Ecosystem | Next | Plugin SDK, marketplace, community |
| Phase 3: Polish & Community | Future | CLI, plugin wizard, docs site |
| Phase 4: Integrations | Future | ACP (editor), A2A (agents), voice, vision |

See `docs/ROADMAP.md` for sprint-level breakdown of each phase.
