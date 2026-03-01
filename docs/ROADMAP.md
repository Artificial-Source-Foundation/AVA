# Roadmap

> Desktop-first AI coding app with dev team and community plugins

---

## Phase Overview

| Phase | Status | Focus |
|-------|--------|-------|
| **1: Desktop App** | **Done** | Core UI, LLM chat, settings, team flow |
| **1.5: Desktop Polish** | **Done** | Settings, appearance, wiring, gap closing |
| **2: Plugin Ecosystem** | Nearly complete | Marketplace UI, SDK, hot reload, wizard all shipped. Backend registry API remaining |
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

### Core Engine (~54,500 lines original + ~5K core-v2 + ~25 extensions, latest baseline: ~3,668 tests across 211 files)
| Category | Modules |
|----------|---------|
| Agent System | Agent loop, Praxis 3-tier hierarchy (13 agents), Parallel execution, Validator |
| Tools | 28 tools (file, shell, web, agents, patch, batch, search, delegate, memory) |
| Intelligence | Codebase understanding, context management, memory, LSP, symbol extraction |
| Extensibility | Extensions, commands, hooks, skills, MCP (OAuth + reconnect) |
| Safety | Permissions, policy engine, trusted folders |
| Infrastructure | 14 LLM providers, config, sessions, auth, bus |
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
- Backend and integration baseline tests (3302 tests across 162 files)
- 0 TS errors, 0 Biome errors, vite build passes, 0 TODOs in src/

### Manual Testing (Before Phase 2)
- [ ] Test full app flow in Tauri dev (chat, tools, settings, sessions)
- [ ] Verify keyboard shortcuts (Ctrl+B, Ctrl+,, Ctrl+M, Ctrl+N)
- [ ] Test on multiple Linux DEs (GNOME, KDE, Cosmic)
- [ ] Test light mode across all components
- [ ] Test file explorer + code editor with real projects

See [Frontend Changelog](frontend/changelog.md) for session-by-session details.
See [Frontend Backlog](frontend/backlog.md) for what's next.

---

## Sprint 1.6: Testing & Debug (COMPLETE)

Hardening sprint focused on tests, logging, and parity with PI Coding Agent.
This sprint is partially implemented and now tracked as active.

Active execution docs:
- [Current Focus](development/status/current-focus.md)
- [Sprint 1.6 Execution](development/sprints/2026-S1.6-testing-hardening-closeout.md)
- [Sprint DX-1 Docs Hardening](development/sprints/2026-DX-1-docs-architecture-hardening.md)

### Sprint 1.6.1: OAuth Test Suite
- Unit tests for JWT decode + accountId extraction
- Integration tests for credential routing + storage
- Status: automated coverage expanded with `src/services/auth/oauth-flow.test.ts`

### Sprint 1.6.2: Message Flow Test Suite
- Queue/steer/cancel unit tests
- Stream integration tests + watcher-triggered messages
- Status: initial automation added via `src/hooks/useChat.integration.test.ts` and `src/components/chat/ChatView.integration.test.tsx`

### Sprint 1.6.3: Debug Logging Coverage
- Structured logs across chat/agent/core/session/settings/file-watcher

### Sprint 1.6.4: PI Coding Agent Parity — COMPLETE
- [x] Mid-session provider switching (Sprint B9 — `requestProviderSwitch()` on AgentExecutor)
- [x] Session branching tree (Gap Analysis Batch 6 — `SessionBranchTree.tsx`, `parentSessionId`, tree/list toggle)
- [x] Minimal tool mode (Sprint B8 — 9-tool subset, per-session state, plan mode pattern)
- [x] Runtime skill creation (Gap Analysis Batch 5 — Custom skill CRUD in `MicroagentsTab.tsx`)

### Sprint 1.6.5: Manual Tauri Testing
- OAuth browser flow, callback, token exchange
- Chat stream validation per provider

### Sprint 1.6 Verification Workflow
- Added `npm run verify:mvp` to run lint + typecheck + full test suite.
- Current status: verification pipeline is green in the latest readiness run (`verify:mvp` passed, full suite `3302 tests / 162 files`).

### Next Build Steps (Immediate)
- [x] Implement automatic session titles from first user message for new chats
- [x] Finish chat streaming polish (stream-end/start micro-jitter stabilized)
- [x] Execute benchmark P0 frontend gap baseline (`FG-001`/`FG-002`/`FG-003`)
- [ ] Complete manual Tauri OAuth validation (connect/disconnect + send flow per provider)
- [x] Complete Sprint 2.3 frontend-backend lifecycle runtime validation — **DONE** (Gap Analysis: plugin tests, hot reload, permission sandboxing)
- [x] Execute remaining frontend gaps (`FG-004` remainder, `FG-006`, `FG-007`) — **DONE** (all 7 FG items delivered)
- [ ] Continue DX-1 docs architecture hardening from [execution sprint doc](development/sprints/2026-DX-1-docs-architecture-hardening.md)

---

## Phase 2: Plugin Ecosystem (NEARLY COMPLETE — THE DIFFERENTIATOR)

This is what makes AVA "The Obsidian of AI Coding". Easy to create, discover, install.

### Sprint 2.1: Plugin Format & SDK — COMPLETE
- [x] Define plugin manifest + parsing/validation (`packages/core/src/extensions/manifest.ts`)
- [x] Plugin lifecycle core (`install`, `enable`, `disable`, `uninstall`, `reload`)
- [x] Extension persistence + tests
- [x] Plugin SDK packaging/docs pass — **DONE** (`docs/plugins/PLUGIN_SDK.md`, `ExtensionAPI` interface, `defineTool()`)
- [x] Plugin sandboxing policy hardening — **DONE** (PluginPermission type, sandboxed API wrapper, permission confirmation dialog)

### Sprint 2.2: Plugin Development Experience — COMPLETE
- [x] `ava plugin init` scaffold command — **DONE** (Sprint 10)
- [x] Hot reload during plugin development — **DONE** (Gap Analysis: `reloadPlugin()` in `extension-loader.ts`)
- [x] Plugin testing utilities — **DONE** (Sprint 10: `createMockExtensionAPI()` + provider test harness)
- [x] Plugin documentation template — **DONE** (Sprint 10: `docs/plugins/PLUGIN_SDK.md`)

### Sprint 2.3: Built-in Marketplace UI — COMPLETE
- [x] Project Hub + project-scoped session restore + sidebar quick project switching
- [x] Settings-only plugin manager surface (replace inline placeholder)
- [x] Search + category-aware filtering in settings manager
- [x] Install/uninstall + enable/disable controls (settings manager MVP)
- [x] Plugin detail/settings panel in settings manager
- [x] Metadata/trust/version/changelog surfaced in plugin cards/details
- [x] Featured plugin catalog curation + remote source — **DONE** (remote fetch + localStorage cache with 30-min TTL + fallback catalog)
- [x] Runtime validation closeout — **DONE** (INT-001/002/003 all complete: real FS, Blob URL import, state persistence, lifecycle tests)

**Key files:** `src/components/settings/tabs/PluginsTab.tsx`, `src/stores/plugins.ts`, `src/services/plugins/`

### Sprint 2.4: Plugin Distribution — PARTIALLY COMPLETE
- [x] `ava plugin init` scaffold command + plugin template docs
- [x] Publish flow stub (`PublishDialog.tsx`) — **DONE** (Gap Analysis)
- [x] Plugin creation wizard (`PluginWizard.tsx` — 4 templates) — **DONE** (Gap Analysis)
- [x] Marketplace sort (popular/rated/recent/name) + download/rating display — **DONE** (Gap Analysis)
- [ ] Plugin registry API (backend)
- [ ] Version management and updates
- [ ] Community ratings backend

### Sprint 2.5: Starter Plugins — COMPLETE
- [x] 5-10 built-in plugins that demonstrate the system — **DONE** (Sprint 10: 5 example plugins with tests)
- [x] Example: "React Patterns" skill plugin — **DONE**
- [x] Example: "/deploy" command plugin — **DONE**
- [x] Example: "Auto-commit" hook plugin — covered by git extension

---

## Phase 3: Community & CLI

### Sprint 3.1: CLI Interface
- [ ] Polish CLI as secondary interface (`cli/` directory)
- [ ] Feature parity with essential desktop features
- [ ] Plugin management from CLI

**Key files:** `cli/src/`, already builds with `npm run build:cli`

### Sprint 3.2: Plugin Creation Wizard — PARTIALLY DONE
- [x] Template gallery — **DONE** (4 templates: Custom Tool, Slash Command, LLM Provider, Context Skill in `PluginWizard.tsx`)
- [ ] "Vibe-code your own plugins" — describe what you want, AI builds the plugin
- [ ] One-click publish to marketplace (backend registry needed)

### Sprint 3.3: Community & Docs
- [ ] Documentation website
- [ ] Community templates and starter plugins
- [ ] Contributing guide for plugin developers

---

## Phase 4: Integrations

### Sprint 4.1: Editor Integration
- [ ] VS Code extension (ACP protocol — needs reimplementation)
- [ ] Cursor/Windsurf support
- [ ] Session sync between desktop app and editor

### Sprint 4.2: Agent Network
- [ ] Agent-to-agent HTTP communication (A2A protocol — needs reimplementation)
- [ ] Remote agent discovery
- [ ] Cross-agent task delegation

*Note: ACP and A2A modules were removed as dead code (available in git history). Reimplementation needed when these features are prioritized.*

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
