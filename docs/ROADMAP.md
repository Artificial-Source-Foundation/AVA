# Roadmap

> Desktop-first AI coding app with dev team and community plugins

---

## Phase Overview

| Phase | Status | Focus |
|-------|--------|-------|
| **1: Desktop App** | **Done** | Core UI, LLM chat, settings, team flow |
| **1.5: Desktop Polish** | **Done** | Settings, appearance, wiring, gap closing |
| **2: Plugin Ecosystem** | Next | THE differentiator — Obsidian-style plugins |
| **3: Community & CLI** | Future | CLI interface, docs site, templates |
| **4: Integrations** | Future | Editor (ACP), agent network (A2A), voice, vision |

---

## Phase 1: Desktop App (COMPLETE)

Everything needed for a working desktop AI coding app.

### What's Built
- IDE-inspired layout (Activity Bar, Sidebar, Main Area, Bottom Panel)
- Chat UI with streaming + virtual scrolling, connected to LLM
- Credential sync bridge (Settings UI -> core credential store)
- Settings page: 14 LLM providers (4 OAuth flows), agents, MCP servers, keybindings
- Session management: create, switch, fork, duplicate, message count
- Team delegation flow visualization (SVG lines, parallel badge, phase timeline)
- Plugin browser shell (placeholder for Phase 2)
- Splash screen with logo, status text, version
- Spring physics animations, glassmorphism design
- Code viewer (CodeMirror 6)

### Core Engine (29,500+ lines, 1778 tests)
| Category | Modules |
|----------|---------|
| Agent System | Agent loop, Commander, Parallel execution, Validator |
| Tools | 22 tools (file, shell, web, browser, agents, patch, batch, search) |
| Intelligence | Codebase understanding, context management, memory, LSP |
| Extensibility | Extensions, commands, hooks, skills, MCP |
| Safety | Permissions, policy engine, trusted folders |
| Infrastructure | 12+ LLM providers, config, sessions, auth, bus |
| Protocols | ACP (editor integration), A2A (agent network) |

---

## Phase 1.5: Desktop Polish (COMPLETE)

Bug fixes, WebKitGTK compatibility, UX refinements, and competitive gap closing.

All development work is done. Only manual Tauri testing remains.

### What's Built
- WebKitGTK fixes (DMABUF ghost rendering, nested button crash, cargo linker)
- Splash screen (logo, status, version, mesh gradient)
- Layout rework (settings modal, activity bar slimmed, bottom/right panels)
- Appearance system (light/dark/system, 3 dark variants, 6 accents + custom hex, 6 code themes, 3 density levels, ligatures, high contrast, UI scale, chat font size, mono + sans font selectors)
- Settings hardening (16 settings across LLM + Behavior tabs, custom instructions, send key, notifications + sound)
- Settings data management (export JSON, import with deep merge, clear all)
- Core frontend wiring (core-bridge, settings sync, context tracking, ContextBar, checkpoints, agent memory)
- Cost tracking (per-message tokens + cost in bubbles, session total in ContextBar)
- Vision/image support (paste, drop, base64, multimodal API, inline display)
- Iterative lint-fix loop (autoFixLint after file edits, errors fed back to LLM)
- Memory recall + auto-compaction (sliding window at 80% context)
- File explorer (recursive tree, Tauri FS lazy-load, expand/collapse)
- Code editor file reading (readFileContent, auto-open from explorer)
- Agent persistence (DB CRUD — save, get, update — wired in session store)
- Google models API (dynamic fetch with hardcoded fallback)
- DiffViewer split view (buildSplitPairs, two-column rendering)
- Chat rendering (markdown + syntax highlighting, tool call cards, date separators, model change indicators)
- Message queue + steering (queue follow-ups during streaming, steer to cancel + send, Ctrl+Shift+Enter)
- File watcher (AI comments: `// AI!` execute, `// AI?` question — 6 patterns, 30+ extensions, Tauri FS watch)
- Step-level undo (Undo button in toolbar, git revert of last auto-committed AI edit)
- Streaming tool preview (live tool call cards with status transitions during streaming)
- Backend tests (1778 tests across 64 files)
- 0 TS errors, 0 Biome errors, vite build passes, 0 TODOs in src/

### Manual Testing (Before Phase 2)
- [ ] Test full app flow in Tauri dev (chat, tools, settings, sessions)
- [ ] Verify keyboard shortcuts (Ctrl+B, Ctrl+,, Ctrl+M, Ctrl+N)
- [ ] Test on multiple Linux DEs (GNOME, KDE, Cosmic)
- [ ] Test light mode across all components
- [ ] Test file explorer + code editor with real projects

See [Frontend Changelog](docs/frontend/changelog.md) for session-by-session details.
See [Frontend Backlog](docs/frontend/backlog.md) for what's next.

---

## Phase 2: Plugin Ecosystem (NEXT — THE DIFFERENTIATOR)

This is what makes Estela "The Obsidian of AI Coding". Easy to create, discover, install.

### Sprint 2.1: Plugin Format & SDK
- [ ] Define unified plugin manifest (skills + commands + hooks + MCP in one package)
- [ ] Plugin SDK with TypeScript types and helpers
- [ ] Plugin lifecycle (install, enable, disable, uninstall, reload)
- [ ] Plugin sandboxing (what plugins can/can't access)

**Key files:** `packages/core/src/extensions/`, `packages/core/src/hooks/`, `packages/core/src/commands/toml.ts`

### Sprint 2.2: Plugin Development Experience
- [ ] `estela plugin init` scaffold command
- [ ] Hot reload during plugin development
- [ ] Plugin testing utilities
- [ ] Plugin documentation template

### Sprint 2.3: Built-in Marketplace UI
- [ ] Plugin browser in sidebar (upgrade from shell placeholder)
- [ ] Search, categories, featured plugins
- [ ] Install/uninstall with one click
- [ ] Plugin settings page per plugin

**Key files:** `src/components/sidebar/SidebarPlugins.tsx` (current placeholder)

### Sprint 2.4: Plugin Distribution
- [ ] Publish plugins from GitHub repos
- [ ] Plugin registry API
- [ ] Version management and updates
- [ ] Community ratings and reviews

### Sprint 2.5: Starter Plugins
- [ ] 5-10 built-in plugins that demonstrate the system
- [ ] Example: "React Patterns" skill plugin
- [ ] Example: "/deploy" command plugin
- [ ] Example: "Auto-commit" hook plugin

---

## Phase 3: Community & CLI

### Sprint 3.1: CLI Interface
- [ ] Polish CLI as secondary interface (`cli/` directory)
- [ ] Feature parity with essential desktop features
- [ ] Plugin management from CLI

**Key files:** `cli/src/`, already builds with `npm run build:cli`

### Sprint 3.2: Plugin Creation Wizard
- [ ] "Vibe-code your own plugins" — describe what you want, AI builds the plugin
- [ ] Template gallery
- [ ] One-click publish to marketplace

### Sprint 3.3: Community & Docs
- [ ] Documentation website
- [ ] Community templates and starter plugins
- [ ] Contributing guide for plugin developers

---

## Phase 4: Integrations

### Sprint 4.1: Editor Integration (ACP)
- [ ] VS Code extension (backend already at `packages/core/src/acp/`)
- [ ] Cursor/Windsurf support
- [ ] Session sync between desktop app and editor

**Key files:** `packages/core/src/acp/`, `cli/src/acp/agent.ts`

### Sprint 4.2: Agent Network (A2A)
- [ ] Agent-to-agent HTTP communication (server already at `packages/core/src/a2a/`)
- [ ] Remote agent discovery
- [ ] Cross-agent task delegation

**Key files:** `packages/core/src/a2a/`

### Sprint 4.3: Advanced I/O
- [ ] Voice input
- [ ] Vision models (screenshot understanding)
- [ ] Multi-modal conversations

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
