# Active Context

> What we're working on RIGHT NOW

---

## Current Focus

**Desktop App Polish — Chat + tools work end-to-end, ship remaining UX**

LLM chat is streaming (Session 35). Tool approval, working directory, session duplicate, and dead code are all fixed (Session 36). Focus shifts to team delegation visualization and plugin prep.

### What Needs to Happen
- [ ] Team delegation flow visualization
- [ ] Session management UI (list, resume, fork — duplicate works now)
- [ ] Plugin browser UI (Phase 2)

---

## Recently Completed (Session 36, 2026-02-07)

- ✅ **Working directory fix** — `useChat` and `useAgent` now read from `useProject().currentProject().directory` instead of hardcoded `'.'`
- ✅ **Tool approval wired** — Shared `src/lib/tool-approval.ts` with `ApprovalRequest` type, `checkAutoApproval()`, `createApprovalGate()`. Both `useChat` and `useAgent` gated before tool execution. `ChatView` merges both approval sources into `ToolApprovalDialog`.
- ✅ **Session duplicate** — `duplicateSessionMessages()` in database, `duplicateSession()` in session store, right-click "Duplicate" creates actual copy with all messages
- ✅ **Dead code removed** — Deleted `src/services/llm/client.ts`, `src/services/llm/providers/` (anthropic, openrouter), `src/services/auth/credentials.ts` (all replaced by `bridge.ts`)

### Session 35 (2026-02-07)
- ✅ **LLM Integration** — Credential bridge + browser access header → streaming AI responses
  - Root cause: 3 disconnected credential stores

### Previous Sessions (33-34)
- ✅ Sidebar fix, noise texture removal, settings scroll fix, Biome/a11y cleanup
- ✅ 7 MVP sprints, Tauri hardening

### What's Already Built
- IDE-inspired layout (ActivityBar, SidebarPanel, MainArea, BottomPanel)
- Chat UI with streaming + virtual scrolling → **connected to LLM + tool approval**
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
