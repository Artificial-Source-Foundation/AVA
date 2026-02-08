# Active Context

> What we're working on RIGHT NOW

---

## Current Focus

**Desktop App — Phase 1 Complete, ready for testing**

All Phase 1 items are done. LLM chat streams (Session 35), frontend gaps fixed (Session 36), providers expanded + team delegation + session fork + plugin shell (Session 37). Ready for `npm run tauri dev` testing.

### Phase 1 Status: COMPLETE
- [x] Team delegation flow visualization (SVG lines, parallel badge, phase timeline)
- [x] Session management UI (list, resume, fork, duplicate, stats)
- [x] Plugin browser UI shell (placeholder for Phase 2)
- [x] Provider expansion (14 providers visible, 4 OAuth flows)

---

## Recently Completed (Session 37, 2026-02-07)

- ✅ **Provider expansion** — ProvidersTab shows all 14 providers (was 4). Added Google, Copilot, xAI, Mistral, Groq, DeepSeek, Cohere, Together, Kimi, Zhipu/GLM. Google + Copilot OAuth buttons added. DeviceCodeDialog for Copilot device code flow.
- ✅ **Team delegation flow** — SVG animated dash lines from Team Lead to teams, ParallelBadge when 2+ teams active, PhaseTimeline (plan→delegate→execute→validate→done), delegation context display, fixed Junior Dev parentId bug.
- ✅ **Session fork** — "Fork from here" in context menu, copies messages to new session, message count in session rows.
- ✅ **Plugin browser shell** — Plugins activity bar icon, SidebarPlugins with "Coming in Phase 2" banner, built-in skills list, disabled Browse/Create buttons.
- ✅ **PI Coding Agent research** — `docs/research/pi-coding-agent.md` analysis (minimalism, provider switching, session branching, self-extension).

### Session 36 (2026-02-07)
- ✅ Working directory fix, tool approval wired, session duplicate, dead code removed

### Session 35 (2026-02-07)
- ✅ LLM Integration — Credential bridge + browser access header → streaming AI responses

### Previous Sessions (33-34)
- ✅ Sidebar fix, noise texture removal, settings scroll fix, Biome/a11y cleanup
- ✅ 7 MVP sprints, Tauri hardening

### What's Already Built
- IDE-inspired layout (ActivityBar, SidebarPanel, MainArea, BottomPanel)
- Chat UI with streaming + virtual scrolling → **connected to LLM + tool approval**
- Agent Activity panel, File Operations panel, Terminal panel
- Code viewer (CodeMirror 6)
- Spring physics animations, glassmorphism design
- Settings page with 4 tabs (Providers [14 providers], Agents, MCP Servers, Keybindings)
- Team panel (TeamPanel + TeamMemberChat)
- All 29,500+ lines of core engine (agent, tools, intelligence, safety, plugins)

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
