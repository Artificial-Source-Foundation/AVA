# Frontend Backlog

> What's missing, prioritized. Updated 2026-02-28.

---

## Status Summary

| Phase | Status | Remaining |
|-------|--------|-----------|
| **1: Desktop App** | **Complete** | - |
| **1.5: Desktop Polish** | **Complete** | Manual testing only |
| **2: Plugin Ecosystem** | In progress | UX baseline shipped; runtime validation + parity gaps pending |
| **2+: Competitive Gaps** | **Complete** | All P0 + P1 competitive gaps delivered |

## Ownership Rules

- Source of truth here: frontend-only scope and UI deliverables.
- Cross-cutting frontend-backend work is tracked in `docs/development/backlogs/integration-backlog.md`.
- Roadmap and sprint docs should reference this backlog instead of duplicating frontend status details.

---

## Phase 1.5 — Manual Testing (Ready)

These require running `npm run tauri dev` and manually verifying:

- [ ] Test full app flow (chat, tools, settings, sessions)
- [ ] Verify keyboard shortcuts (Ctrl+B, Ctrl+,, Ctrl+M, Ctrl+N)
- [ ] Test on multiple Linux DEs (GNOME, KDE, Cosmic)
- [ ] Test light mode across all components
- [ ] Test density settings (compact, default, comfortable)
- [ ] Test file explorer with real project directories
- [ ] Test agent persistence (create agent, switch session, verify loaded)

---

## Phase 2+ — Competitive Gaps (Status Normalized)

These gaps were prioritized previously and are now mostly delivered based on changelog + code audit.

### Delivered
- [x] Git auto-commit on AI edits
- [x] Weak/secondary model support
- [x] Streaming tool preview
- [x] File watcher + AI comment patterns
- [x] Architect/editor model split
- [x] Message queue + steering
- [x] Session step-level undo

### Remaining hardening
- [x] Add automated tests for queue/steer/cancel, watcher-triggered flow, and OAuth edge cases (Sprint 1.6)
- [x] Stabilize streaming jitter/flicker and chat overflow behavior in desktop runtime
- [ ] Expand manual Tauri validation on Linux DE variants and light mode polish

### Next execution queue
- [x] Implement session auto-title UX validation and polish
- [x] Execute benchmark-derived frontend gaps FG-001/FG-002/FG-003 (chat git strip, usage details dialog, plugin metadata/trust pass)
- [x] Land inline approval-state UX baseline (FG-005)
- [ ] Final manual QA pass for chat stream UX across long sessions
- [ ] Complete Sprint 2.3 plugin UX runtime wiring/validation from `docs/development/sprints/2026-S2.3-plugin-ux-wiring.md`
- [ ] Complete benchmark-derived frontend gaps FG-004 remainder, FG-006, and FG-007 from `docs/development/status/frontend-gap-matrix-2026-02-15.md`
- [ ] Wire plugin install/uninstall to real backend lifecycle APIs (replace local mock adapter)

---

## Phase 2 — Plugin Ecosystem (THE DIFFERENTIATOR)

This is what makes AVA "The Obsidian of AI Coding".

### Sprint 2.1: Plugin Format & SDK
- [x] Define unified plugin manifest (skills + commands + hooks + MCP in one package) — **DONE** (Sprint 10: `ava-extension.json` manifest format)
- [x] Plugin SDK with TypeScript types and helpers — **DONE** (Sprint 10: `docs/plugins/PLUGIN_SDK.md`, `ExtensionAPI` interface, `defineTool()`)
- [ ] Plugin lifecycle (install, enable, disable, uninstall, reload) — frontend wired to mock adapter; real backend lifecycle still needed
- [ ] Plugin sandboxing (what plugins can/can't access)
**Frontend**: None yet (backend-first)

### Sprint 2.2: Plugin Development Experience
- [x] `ava plugin init` scaffold command — **DONE** (Sprint 10: generates ExtensionAPI source + manifest + tests)
- [ ] Hot reload during plugin development
- [x] Plugin testing utilities — **DONE** (Sprint 10: `createMockExtensionAPI()` + provider test harness)
- [x] Plugin documentation template — **DONE** (Sprint 10: `docs/plugins/PLUGIN_SDK.md`)
**Frontend**: Plugin dev panel showing reload status, logs

### Sprint 2.3: Built-in Marketplace UI
- [x] Plugin browser in sidebar/settings surfaces
- [x] Settings-only plugin manager surface (replace Plugins placeholder)
- [x] Search + category-aware filtering in settings manager
- [x] Install/uninstall + enable/disable controls in settings manager
- [x] Plugin detail/settings panel in settings manager
- [x] Metadata/trust/version/changelog fields surfaced in plugin cards/details
- [x] Featured plugin catalog curation + remote source integration — **DONE** (Sprint 10: remote fetch + localStorage cache with 30-min TTL + fallback catalog)
- [ ] Wire settings manager actions to real backend extension lifecycle APIs (tracked as `INT-001`/`INT-002`/`INT-003` in `docs/development/backlogs/integration-backlog.md`)
**Frontend**: Settings tab plugin manager, search, install flow, detail/settings view. Shipped with shared `plugins` store and mock lifecycle adapter.

---

## Chat & UX Gaps (Goose + OpenCode Comparison — 2026-02-27)

Informed by comprehensive audits of Goose and OpenCode frontends. Ordered by impact.

### P0 — High Impact (Must Have)

- [x] **Inline Tool Approval** — Replaced modal with inline `ApprovalDock` in composer area. Compact row with expand toggle, keyboard shortcuts (Enter/Escape), auto-expand for high/critical risk, always-allow checkbox. — **Large** *(done)*
- [x] **Integrated Terminal (xterm.js)** — Full interactive terminal in bottom panel. Rust PTY backend (`portable-pty`) + Tauri IPC + xterm.js frontend. Tabbed bottom panel (Memory/Terminal/Output). Ctrl+\` toggle. — **Large** *(done)*
- [x] **Aggregate Diff Review Panel** — "Review" tab in right panel aggregates all file changes with DiffViewer. Diff content captured during tool execution (originalContent/newContent). Expand/collapse per file, +/- line counts. — **Large** *(done)*
- [x] **@ File Mention Autocomplete** — `@` in composer triggers fuzzy file picker popover. — **Medium** *(done)*
- [x] **File Changes Sidebar** — Right panel "Files" tab shows file operations during session. — **Medium** *(done)*
- [x] **Conversation Search** — Full-text search with match highlighting and next/prev navigation. — **Medium** *(done)*
- [x] **Conversation Export (Markdown)** — Export chat as `.md`. Command palette + Ctrl+Shift+E. — **Small** *(done)*
- [x] **Context Usage Warning Badge** — Yellow warning icon in token strip at 80% context. — **Small** *(done)*
- [x] **Session Aggregate Cost** — Per-message tokens+cost, session total in ContextBar. — *(done)*
- [x] **"Finished Without Output" Placeholder** — Italic placeholder when assistant has tool calls but no text. — **Tiny** *(done)*

### P1 — Medium Impact

- [x] **Message Queue UI** — `MessageQueueBar.tsx` above composer shows queued message count, expand to view/remove individual messages. Exposed `messageQueue` + `removeFromQueue` from useChat. — **Medium** *(done)*
- [x] **File Tree Change Indicators** — Color-coded dots on modified/created/deleted files in `SidebarExplorer.tsx`. Directories with changed descendants get subtle accent dot. Fed from `fileOperations` store via reactive memo. — **Medium** *(done)*
- [x] **"Open in" IDE Integration** — Auto-detects 8 editors (VS Code, Cursor, Zed, etc.) via `which`. Right-click context menu in file explorer, "Open in" buttons in FileOperationsPanel and DiffReviewPanel. Header button to open project. `ide-integration.ts` service. — **Medium** *(done)*
- [x] **Live Tool Progress Streaming** — Bash tool streams incremental stdout via metadata callback. `streamingOutput` field on ToolCall updated in real-time. `ToolCallCard` shows live output while running. — **Large** *(done)*
- [x] **Undo/Redo File Changes** — `file-versions.ts` service maintains per-session version stacks. Undo/redo write file content via Tauri FS. Keyboard shortcuts Ctrl+Shift+Z/Y. Toast notifications. Integrated with diff capture from stream-lifecycle. — **Large** *(done)*
- [x] **Conversation Branching** — Fork conversation at any message via Branch button (GitFork icon). — **Medium** *(done)*
- [x] **Quick Session Switcher (Ctrl+J)** — Keyboard-driven overlay with fuzzy search. — **Medium** *(done)*
- [x] **Expanded Editor (Ctrl+E)** — Full-screen monospace modal for composing long prompts. — **Medium** *(done)*
- [x] **Auto-Compact Notification** — Toast when context compaction triggers. — **Small** *(done)*
- [x] **Smarter Tool Result Truncation** — Line-based (15 lines) with expand button. — **Small** *(done)*
- [x] **Project Init Command** — Command palette "Initialize Project" sends canned analysis prompt. — **Medium** *(done)*
- [x] **LSP Diagnostics in Status Bar** — Error/warning counts in MessageInput strip. — **Medium** *(done)*

### P2 — Lower Impact / Future

- [x] **Theme Live Preview** — Hover over accent colors, dark styles, code themes, border radius, and density options to preview changes instantly. Uses `previewAppearance`/`restoreAppearance` pattern that applies CSS vars without persisting. — **Small** *(done)*
- [ ] **Workflow/Recipe Creation** — Save a successful session as a reusable workflow. Goose calls these "recipes" and can create them from any completed session. — **Large** *(source: Goose)*
- [ ] Custom commands UI (manage TOML/MD commands) — **Large**
- [x] Faster model picker dialog (Ctrl+O, grouped by provider) — **Small** *(done)*
- [x] Conversation branching (fork at any message) — **Medium** *(done — moved to P1)*
- [x] Prompt library / starter templates — **Medium** *(done)*
- [ ] Voice dictation input — **Medium**
- [x] Panel adaptability (draggable/persisted split ratios) — **Medium** *(done)*

### Legacy Gaps (Still Open)

- **FG-004 (partial):** long-session render-window/backfill hardening for very large histories.
- **INT-001/INT-002/INT-003:** plugin lifecycle runtime validation and failure-path evidence.
- **Manual QA:** Linux DE matrix and light-mode regression pass.

### Sprint 2.4: Plugin Distribution
- [ ] Publish plugins from GitHub repos
- [ ] Plugin registry API
- [ ] Version management and updates
- [ ] Community ratings and reviews
**Frontend**: Publish flow in settings, update notifications

### Sprint 2.5: Starter Plugins
- [x] 5-10 built-in plugins demonstrating the system — **DONE** (Sprint 10: 5 example plugins with tests in `docs/examples/plugins/`)
- [x] Example: timestamp-tool (registerTool + Zod schema)
- [x] Example: file-stats (registerTool + platform.fs)
- [x] Example: polite-middleware (addToolMiddleware + priority)
- [x] Example: session-notes (registerCommand + storage API)
- [x] Example: event-logger (api.on + emit + events + storage)
- [ ] Example: "React Patterns" skill plugin
- [ ] Example: "/deploy" command plugin
**Frontend**: Plugin showcase page

---

## Phase 3+ — Longer Term

| Feature | Effort | Frontend Impact |
|---------|--------|----------------|
| Sandbox / container execution | 2-3 weeks | Toggle in settings, status indicator |
| Tree-sitter for 100+ languages | 2 weeks | Better code highlighting, symbol extraction |
| Voice input | 2 weeks | Microphone button in MessageInput |
| CLI polish | 1-2 weeks | None (CLI-only) |
| ACP editor integration | 2 weeks | Minimal (backend protocol) |
| A2A agent network | 2 weeks | Agent discovery UI, remote agent cards |

---

## What's Complete (No Work Needed)

These were identified as gaps but are now fully implemented:

| Feature | Session | Status |
|---------|---------|--------|
| Checkpointing / time-travel undo | 40 | createCheckpoint, rollbackToCheckpoint, UI |
| Cost & token tracking | 44 | Per-message tokens+cost in bubbles, session total in ContextBar |
| Vision / image support | Multiple | Paste, drop, base64, multimodal API, inline display |
| Iterative lint-fix loop | 44 | autoFixLint setting, biome/eslint after edits, errors fed back |
| Memory recall | 45 | recallSimilar + procedural recall injected into system prompts |
| Auto-compaction | 45 | Sliding window when context > 80%, syncs state + DB + tracker |
| File explorer | 45 | Recursive tree, lazy-load, Tauri FS |
| Code editor file reading | 45 | readFileContent via Tauri FS, auto-open from explorer |
| Agent persistence | 45 | DB CRUD (save, get, update), wired in session store |
| Google models API | 45 | Dynamic fetch with hardcoded fallback |
| Copilot provider defaults + model fetch | 58 | Github icon, real model IDs (gpt-4.1 default), dynamic fetch with fallback |
| DiffViewer split view | 45 | buildSplitPairs, two-column rendering |
| Dark/light/system theme | 41 | With midnight + charcoal dark variants |
| 6 accent colors + custom hex | 41 | hexToAccentVars computes all accent vars |
| 6 code themes | 41 | Via data-code-theme attribute |
| UI density (3 levels) | 42 | 8 components wired |
| Custom instructions | 44 | Injected as system message |
| Desktop notifications | 44 | Unfocused-only + AudioContext chime |
| Settings export/import | 44 | JSON download, file picker, deep merge |
| Project hub screen + resume/open flow | 54 | Full-screen hub, open-folder CTA, resume current project |
| Project-scoped session restore | 54 | Last-session persistence per project + startup restore |
| Sidebar quick project switching | 54 | Hub shortcut, open-project action, project switch dropdown |
| Plugin browser in settings + sidebar | 55 | Shared plugin store, search, categories, featured, quick actions |
| Plugin install/uninstall + settings entry | 55 | One-click actions with AVA/legacy install-state compatibility |
| Plugin scaffold CLI foundation | 56 | `ava plugin init` command + generated package template docs |
| Plugin SDK + test utilities | 57 | `createMockExtensionAPI()`, provider test harness, 5 example plugins |
| Remote plugin catalog | 57 | Fetch + localStorage cache + fallback, `PluginCatalogItem` extended fields |
| Conversation branching | 60+ | Fork at any message via GitFork button, creates new session |
| Quick session switcher (Ctrl+J) | 60+ | Fuzzy search overlay, keyboard-driven |
| Expanded editor (Ctrl+E) | 60+ | Full-screen monospace modal, Ctrl+Enter to apply |
| Prompt library / starter templates | 60+ | 4 template cards in empty chat state |
| Panel adaptability | 60+ | Draggable right panel, persisted width (250-600px) |
| Conversation search | 60+ | Full-text search, match highlighting, next/prev navigation |
| Conversation export (Markdown) | 60+ | Command palette + Ctrl+Shift+E |
| Project init command | 60+ | Command palette "Initialize Project" |
| LSP diagnostics in status bar | 60+ | Error/warning counts in MessageInput strip |
| @ file mention autocomplete | 60+ | Fuzzy file picker popover on `@` |
| File changes sidebar | 60+ | Right panel "Files" tab with file operations |
| Context usage warning badge | 60+ | Yellow warning at 80% context |
| Scroll performance (WebKitGTK) | 60+ | Passive scroll listeners, removed bad CSS hacks |
| Inline tool approval dock | 65+ | ApprovalDock replaces modal, keyboard shortcuts, auto-expand |
| Integrated terminal (xterm.js) | 65+ | Rust PTY + Tauri IPC + xterm.js, tabbed bottom panel, Ctrl+` |
| Aggregate diff review panel | 65+ | Review tab, diff capture in tool execution, DiffViewer per file |
| Message queue UI | 65+ | MessageQueueBar with count, expand, remove individual messages |
| File tree change indicators | 65+ | Color-coded dots on changed files, directory change propagation |
| "Open in" IDE integration | 65+ | Auto-detect editors, context menu, open file/project in VS Code etc. |
| Live tool progress streaming | 65+ | Bash stdout streams via metadata callback, live output in ToolCallCard |
| Theme live preview | 65+ | Hover to preview accent, dark style, code theme, radius, density |
| Undo/redo file changes | 65+ | Per-session version stacks, Ctrl+Shift+Z/Y, toast notifications |

---

## Unique Advantages (AVA vs Everyone)

Features no other AI coding tool has:

| Feature | Status |
|---------|--------|
| Multi-agent hierarchy (Team Lead + Senior Leads + Junior Devs) | Built, visible in UI |
| Worker scope filtering (each agent sees only relevant files/tools) | Built |
| Parallel agent execution | Built |
| Auto-reporting (workers report up the chain) | Built |
| User intervention points (click into any agent's chat) | Built |
| Doom loop detection | Built |
| Validator/QA pipeline (syntax, types, lint, test, review) | Built |
| Codebase intelligence (PageRank, dependency graph, symbols) | Built |
| Memory system (episodic + semantic + procedural + RAG) | Built |
| Permission/policy engine (risk assessment, auto-approval) | Built |
| Hook system (PreToolUse, PostToolUse, lifecycle) | Built |
| Plugin marketplace | Phase 2 (planned) |
| Protocol support (ACP + A2A) | Built (backend) |
