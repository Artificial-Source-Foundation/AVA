# Backend Changelog

> Development history of `packages/core/`. Most recent first.

---

## 2026-02-08

### Session 43 — Backend Test Coverage Phase 2
- **706 new tests** across 15 test files + 2 helper files
- Agent: evaluator (35), events (53), recovery (107), planner (32), plan mode (36) = 263
- Tools: utils (95), sanitize (76), truncation (22), locks (33), completion (31), validation (31), define (40), todo (19), edit-replacers (65) = 412
- LLM: client (31)
- Fixed 5 missing module exports in `index.ts` (a2a named exports, policy named exports)
- **Total: 1778 tests** across 64 files (was 1072)

### Session 42 — Density + Font Wiring Fix
- Density values recalibrated (compact 4/8px, default 6/12px, comfortable 8/16px)
- Section density added (`--density-section-py/px`)
- 8 frontend components wired with density CSS variables
- Chat font size applied to MessageInput textarea

### Session 41 — Appearance Expansion
- 8 new appearance features: system theme, dark variants, code themes, ligatures, chat font size, custom accent, sans font, high contrast
- `setupSystemThemeListener()` for OS theme sync
- `hexToAccentVars()` for custom accent color computation
- localStorage bridge for flash prevention

### Session 40 — Core Frontend Wiring
- `core-bridge.ts` — initializes 5 core singletons at startup
- `pushSettingsToCore()` — maps frontend settings to core SettingsManager
- `ContextBar.tsx` — token usage progress bar
- Session checkpoints (`createCheckpoint`, `rollbackToCheckpoint`)
- Agent memory recording via `getCoreMemory().remember()`

### Session 39 — Backend Testing Phase 1
- **536 new tests** across 24 files covering Config, Context, Memory, Session, Commander
- Appearance tab (dark/light mode, 6 accent colors, UI scale, mono font)
- Settings redesign (all tabs rewritten to flat minimal rows)

---

## 2026-02-07

### Session 37 — Phase 1 Completion
- Provider expansion: 14 providers in Settings UI (was 4)
- Google + Copilot OAuth (device code flow)
- Team delegation flow visualization (SVG animated lines)
- Session fork ("Fork from here" context menu)
- Plugin browser shell placeholder

### Session 36 — Frontend Gaps
- Working directory fix for `useChat` and `useAgent`
- Tool approval wired (`ApprovalRequest`, `checkAutoApproval`, `createApprovalGate`)
- Session duplicate implementation
- Dead code removed: `-975 lines` (old LLM client, providers, credentials)

### Session 35 — LLM Integration Working
- Root cause: 3 disconnected credential stores
- Fix: `syncProviderCredentials()` + `syncAllApiKeys()` bridge
- Anthropic `dangerous-direct-browser-access: true` header
- Chat → streaming LLM response now working end-to-end

---

## 2026-02-05

### Sessions 32-33 — Vision + MVP Sprints
- Defined "The Obsidian of AI Coding" vision
- 7 MVP sprints defined
- Tauri hardening (CSP, scoped FS, deferred window, release profile)
- Code splitting (solid 116KB, icons 20KB, app 408KB, vendor 2.3MB)

### Session 30 — Epics 25-26
- Epic 25: ACP + A2A protocols (97 tests)
- Epic 26: Gemini CLI feature parity (337 tests)

---

## 2026-02-04

### Epics 19-21 — MVP Polish
- Epic 19: Hooks system (PreToolUse, PostToolUse, etc.)
- Epic 20: Browser tool (Puppeteer automation)
- Epic 21: Provider expansion
- Feature parity sprints 1-7

---

## 2026-02-03

### Epics 8-17 — Agent System Build
- Epic 8: Agent loop (autonomous execution)
- Epic 9: Commander (hierarchical delegation)
- Epic 10: Parallel execution (batch, scheduling, conflict detection)
- Epic 11: Validator (QA pipeline)
- Epic 12: Codebase understanding (symbols, imports, PageRank)
- Epic 13: Config system (settings, credentials, migration)
- Epic 14: Memory system (episodic, semantic, procedural)
- Epic 15-17: Enhancement (OpenCode features, missing tools)

---

## 2026-02-02

### Epics 3-7 — Infrastructure
- Epic 3: ACP monorepo structure
- Epic 4: Safety system (permissions, policy, trust)
- Epic 5: Context management (tracking, compaction)
- Epic 6: Developer experience
- Epic 7: Platform abstraction (Node, Tauri, browser)

---

## 2026-01-29 — 2026-01-30

### Epics 1-2 — Foundation
- Epic 1: Multi-provider LLM streaming
- Epic 2: File tools (7 tools initially)

---

## 2025-01-28

### Project Scaffold
- Tauri + SolidJS + SQLite initial setup
- Monorepo with packages/core, packages/platform-node, packages/platform-tauri

---

*Last updated: 2026-02-08*
