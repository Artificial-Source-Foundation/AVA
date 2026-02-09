# Roadmap

> Desktop-first AI coding app with dev team and community plugins

---

## Phase Overview

| Phase | Status | Focus |
|-------|--------|-------|
| **1: Desktop App** | **Done** | Core UI, LLM chat, settings, team flow |
| **1.5: Desktop Polish** | **In Progress** | Testing, bug fixes, UX refinements |
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

## Phase 1.5: Desktop Polish (IN PROGRESS)

Bug fixes, WebKitGTK compatibility, UX refinements, and competitive gap closing before moving to plugins.

### Done
- [x] WebKitGTK DMABUF ghost rendering fix (NVIDIA + Wayland compositors)
- [x] Nested button crash fix (div+role=button pattern)
- [x] Cargo linker fix for Pop OS / Cosmic
- [x] Splash screen (logo, status, version, mesh gradient, min display time)
- [x] Layout refactoring (navigation store removed, sidebar slimmed, settings as modal)
- [x] CSS performance (transition-colors, GPU compositing for scroll containers)
- [x] Appearance tab (dark/light mode, 6 accents, UI scale, mono font, density)
- [x] Appearance expansion (system theme, dark variants, code themes, ligatures, chat font size, custom accent, sans font, high contrast)
- [x] Density + font wiring (8 components, CSS utility classes, section density vars)
- [x] Backend tests Phase 1 (536 tests for Config, Context, Memory, Session, Commander)
- [x] Backend tests Phase 2 (706 tests for Agent, Tools, LLM — 1778 total across 64 files)
- [x] Core frontend wiring (core-bridge, settings sync, context tracking, ContextBar, checkpoints, memory recording)
- [x] Settings hardening (16 new settings, LLM + Behavior tabs, generation/agent/behavior/notification controls)
- [x] Custom instructions (injected as system message in buildApiMessages)
- [x] Configurable send key (Enter vs Ctrl+Enter) with dynamic ShortcutHint
- [x] Desktop notifications + sound chime (AudioContext, unfocused-only, configurable volume)
- [x] Settings data management (export JSON, import with deep merge, clear all)
- [x] Cost tracking UI (per-message cost + tokens in bubbles, session total in ContextBar)
- [x] Vision/image support (paste, drop, base64, multimodal API, inline display)
- [x] Iterative lint→fix loop (autoFixLint setting, biome/eslint after file edits, errors fed back to LLM)
- [x] Checkpoint UI (create button, inline display with restore, full DB rollback)
- [x] Per-message token display in bubbles
- [x] 0 TS errors, 0 Biome errors, vite build passes

- [x] Memory recall injected into system prompts (recallSimilar + procedural recall → system message)
- [x] Auto-compaction when context > 80% (sliding window, syncs state + DB + tracker)

### Remaining
- [ ] Test full app flow in Tauri dev (chat, tools, settings, sessions)
- [ ] Verify all keyboard shortcuts work (Ctrl+B, Ctrl+,, Ctrl+M)
- [ ] Test on multiple Linux DEs (GNOME, KDE, Cosmic)

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
