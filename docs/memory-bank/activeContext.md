# Active Context

> What we're working on RIGHT NOW

---

## Current Focus

**LLM Integration — Make the app actually talk to an LLM**

The core engine (29,500+ lines) and desktop UI are built, but the app can't send a message to an LLM yet. The critical path is connecting the Tauri frontend to the core provider system.

### What Needs to Happen
- [ ] Bridge Tauri frontend → core LLM providers (API key → streaming response)
- [ ] Chat actually works: type message → get streaming AI response
- [ ] Provider configuration UI connects to real provider clients
- [ ] Tool execution visible in the UI (file reads, writes, shell commands)

---

## Recently Completed (Session 33-34, 2026-02-07)

- ✅ **Sidebar fix** — Width-based toggle (not margin-left, which broke WebKitGTK)
- ✅ **Noise texture removal** — `#root::after` overlay blocked all clicks in WebKitGTK
- ✅ **Settings scroll fix** — GPU layer promotion + transition-colors
- ✅ **Biome/a11y cleanup** — All lint errors fixed, pre-commit hooks pass
- ✅ **7 MVP sprints** — Settings persistence, tool approval, design tokens, team UI
- ✅ **Tauri hardening** — CSP, scoped FS, deferred window show, release profile

### What's Already Built
- IDE-inspired layout (ActivityBar, SidebarPanel, MainArea, BottomPanel)
- Chat UI with streaming + virtual scrolling
- Agent Activity panel, File Operations panel, Terminal panel
- Code viewer (CodeMirror 6)
- Spring physics animations, glassmorphism design
- Settings page with 4 tabs (Providers, Agents, MCP Servers, Keybindings)
- Team panel (TeamPanel + TeamMemberChat)
- All 29,500+ lines of core engine (agent, tools, intelligence, safety, plugins)

### What's Still Missing (After LLM Integration)
- [ ] Session management UI (list, resume, fork)
- [ ] Team delegation flow visualization
- [ ] Plugin browser UI (Phase 2)

---

## Platform Priority

```
1. Desktop App (Tauri)     ← CURRENT FOCUS
2. Plugin Ecosystem        ← NEXT
3. CLI                     ← Secondary
4. Editor Integration      ← Future
5. Agent Network           ← Future
```

---

## Naming Convention

| Old Name | New Name | Role |
|----------|----------|------|
| Commander | Team Lead | Plans, delegates, coordinates |
| Worker | Senior Lead | Domain specialist, leads a group |
| Operator | Junior Dev | Executes specific tasks |

---

## Completed

| What | Status |
|------|--------|
| Epics 1-21 | ✅ Foundation through Enhancement (~25,000 lines) |
| Epic 25 Sprints 1-3 | ✅ ACP + A2A (194 tests) |
| Epic 26 | ✅ Gemini CLI Feature Parity (337 tests) |
| Feature Parity Sprints 1-7 | ✅ Cline + OpenCode features |
| UI Modernization | ✅ 8 phases + IDE layout redesign |
| Vision alignment | ✅ Docs reorganized |
