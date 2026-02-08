# Progress Log

> Quick status overview — detailed history in [sessions/archive.md](./sessions/archive.md)

---

## Current Session

**Session 38** (2026-02-08)
- **WebKitGTK ghost rendering fix** — DMABUF renderer produces ghost/shadow copies of DOM elements on Cosmic/Hyprland/Sway + NVIDIA. Fixed by setting `WEBKIT_DISABLE_DMABUF_RENDERER=1` in `src-tauri/src/main.rs` before WebKitGTK init.
- **Nested button crash fix** — WebKitGTK crashes on nested `<button>` elements. Changed outer buttons to `<div role="button" tabIndex={0}>` in SettingsPage, SessionListItem, TerminalPanel.
- **Cargo linker fix** — Pop OS 24.04 has `gcc-14` but no `cc` symlink. Added `.cargo/config.toml` with `linker = "gcc-14"`.
- **Splash screen** — New `SplashScreen.tsx` with diamond logo placeholder, "ESTELA" title, "AI Coding Companion" tagline, animated loading dots, real-time init status text, version number (`v0.1.0`), mesh gradient background, 800ms minimum display, fade-out transition. Window shows early so splash is visible during init.
- **Layout refactoring** — Deleted `navigation.ts` store. Sidebar slimmed to 2 activities (sessions, explorer). Settings moved to modal pattern in layout store. Added right panel, bottom panel, bottom panel height state. New shortcuts: `Ctrl+,` (settings), `Ctrl+M` (bottom panel).
- **New files:** `src/components/SplashScreen.tsx`
- **Deleted:** `src/stores/navigation.ts`
- **Modified:** `App.tsx`, `index.tsx`, `index.html`, `index.css`, `layout.ts`, `layout.test.ts`, `ActivityBar.tsx`, `MainArea.tsx`, `SidebarPanel.tsx`, `SidebarSessions.tsx`, `SettingsPage.tsx`, `constants.ts`, `src-tauri/src/main.rs`, `src-tauri/.cargo/config.toml`

---

## Recent Sessions

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
| 2026-02-08 | **WebKitGTK fixes. Splash screen. Layout refactoring.** |

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
