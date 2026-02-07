# Active Context

> What we're working on RIGHT NOW

---

## Current Focus

**Desktop App Polish — LLM chat works, now ship the remaining UX gaps**

Chat → streaming LLM response is connected and working (Session 35). Focus shifts to tool execution, session management, and cleanup.

### What Needs to Happen
- [ ] Tool execution in the UI (file reads, writes, shell commands via agent mode)
- [ ] Working directory resolution (`useChat.ts:34` hardcoded to `'.'`)
- [ ] Session management UI (list, resume, fork)
- [ ] Team delegation flow visualization
- [ ] Dead code cleanup (unused `src/services/llm/client.ts`, `src/services/llm/providers/`, `src/services/auth/credentials.ts`)
- [ ] Plugin browser UI (Phase 2)

---

## Recently Completed (Session 35, 2026-02-07)

- ✅ **LLM Integration** — App sends messages and gets streaming AI responses
  - Credential bridge: `syncProviderCredentials()` writes API keys from Settings UI to core credential store
  - Startup hydration: `syncAllApiKeys()` called after `initializePlatform()` in App.tsx
  - Browser access header: `anthropic-dangerous-direct-browser-access: true` in core Anthropic provider
  - Root cause: 3 disconnected credential stores (`estela_settings` vs `estela_credentials` vs `estela_cred_*`)

### Previous Sessions (33-34)
- ✅ Sidebar fix, noise texture removal, settings scroll fix, Biome/a11y cleanup
- ✅ 7 MVP sprints, Tauri hardening

### What's Already Built
- IDE-inspired layout (ActivityBar, SidebarPanel, MainArea, BottomPanel)
- Chat UI with streaming + virtual scrolling → **now connected to LLM**
- Agent Activity panel, File Operations panel, Terminal panel
- Code viewer (CodeMirror 6)
- Spring physics animations, glassmorphism design
- Settings page with 4 tabs (Providers, Agents, MCP Servers, Keybindings)
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
