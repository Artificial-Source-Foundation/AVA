# Roadmap

> Desktop-first AI coding app with dev team and community plugins

---

## Current Priority: Phase 1 — Desktop App

The Tauri desktop app is the primary product. Everything else is secondary.

---

## What's Built (29,500+ lines, 531 tests)

### Core Engine (Complete)
All backend modules are implemented and tested:

| Category | Modules | Status |
|----------|---------|--------|
| Agent System | Agent loop, Commander, Parallel execution, Validator | Done |
| Tools | 19 tools (file, shell, web, browser, agents) | Done |
| Intelligence | Codebase understanding, context management, memory, LSP | Done |
| Extensibility | Extensions, commands, hooks, skills, MCP | Done |
| Safety | Permissions, policy engine, trusted folders | Done |
| Infrastructure | 12+ LLM providers, config, sessions, auth, bus | Done |
| Protocols | ACP (editor integration), A2A (agent network) | Done |

### Desktop App (In Progress)
- IDE-inspired layout with Activity Bar, Sidebar, Bottom Panel
- Chat UI with streaming, virtual scrolling
- Agent Activity panel, File Operations panel
- Code viewer (CodeMirror 6)
- Spring physics animations, glassmorphism design
- Settings page with provider configuration

### What's Missing for Phase 1
- [ ] **LLM connection** — Bridge Tauri frontend to core provider system (critical path)
- [ ] **Working chat** — Type a message, get streaming AI response
- [ ] Session management UI (list, resume, fork)
- [ ] Team delegation flow visualization
- [ ] Plugin browser UI (prepare for Phase 2)

---

## Phase 2: Plugin Ecosystem

- [ ] Unified plugin format (skills + commands + hooks in one package)
- [ ] Plugin SDK with dead-simple creation flow
- [ ] Built-in marketplace UI in the desktop app
- [ ] Publish plugins from GitHub repos
- [ ] Community ratings, search, categories

---

## Phase 3: Polish & Community

- [ ] CLI interface (secondary way to use Estela)
- [ ] Plugin creation wizard ("vibe-code your own plugins")
- [ ] Community templates and starter plugins
- [ ] Documentation website

---

## Phase 4: Integrations

- [ ] Editor integration (ACP — VS Code/Cursor backend)
- [ ] Agent network (A2A — remote agent calls)
- [ ] Voice input
- [ ] Vision models

---

## Engineering History

All backend epics are complete. See `docs/development/completed/` for details.

| Epics | Scope |
|-------|-------|
| 1-3 | Foundation: Chat, File Tools, Monorepo |
| 4-7 | Infrastructure: Safety, Context, DX, Platform |
| 8-15 | Agent System: Loop, Commander, Parallel, Validator, Codebase, Config, Memory |
| 16-17 | Enhancement: OpenCode features, Missing tools |
| 19-21 | MVP Polish: Hooks, Browser, Providers |
| 25 | Protocols: ACP agent, A2A server |
| 26 | Gemini CLI: Policy, Bus, Resume, Commands, Trust, Extensions, Compression |
| Sprints 1-7 | Feature Parity: Security, Approvals, Edits, UX, Cline, OpenCode features |
