# Frontend

> Desktop app built with SolidJS + Tauri v2. Updated 2026-02-28.

---

## Stack

| Layer | Technology |
|-------|-----------|
| Framework | SolidJS (fine-grained reactivity) |
| Desktop | Tauri v2 (Rust backend, WebKitGTK on Linux) |
| Language | TypeScript (strict) |
| Styling | Tailwind CSS v4 + CSS custom properties |
| Code viewer | CodeMirror 6 (solid-codemirror) |
| Virtual scroll | @tanstack/solid-virtual |
| Icons | lucide-solid |

---

## Status Snapshot

- **All P0–P3-C competitive gap items delivered** (31 features in final sprint)
- **Sprint 16 Praxis**: 3-tier agent hierarchy UI (Commander → Leads → Workers)
- **Gap Analysis Sprint**: 20 items — delegation UI, doom loop banner, bento cards, skill CRUD, session tree, memory browser, trusted folders, marketplace UX, MCP OAuth, plugin wizard
- **Phase 2 Plugin Ecosystem**: SDK, marketplace, lifecycle, sandboxing, wizard, hot reload — all done except registry API backend
- **Remaining**: Manual QA (Linux DE matrix, light mode), plugin registry API backend

---

## Layout

```
┌──────┬──────────────────────────────────┬──────────┐
│      │                                  │          │
│  A   │         Main Area                │ Sidebar  │
│  c   │  ┌──────────────────────────┐    │          │
│  t   │  │   Chat / Code Editor     │    │ Sessions │
│  i   │  │   MessageList            │    │ Explorer │
│  v   │  │   MessageInput + Context │    │          │
│  i   │  └──────────────────────────┘    │          │
│  t   │                                  │          │
│  y   ├──────────────────────────────────┤          │
│      │  Bottom Panel (Memory/Terminal)  │          │
│  B   │                                  │          │
│  a   ├──────────────────────────────────┤          │
│  r   │  Right Panel (Agent Activity)    │          │
└──────┴──────────────────────────────────┴──────────┘
```

- **Activity Bar** (48px, left) — Configurable icons (sessions, explorer), order from settings
- **Main Area** — Chat with LLM, code editor, tool call cards
- **Sidebar** (right) — Sessions list or file explorer (read-only lock icons, context menu)
- **Bottom Panel** — Memory + Terminal tabs (resizable, Ctrl+M toggle)
- **Right Panel** — Agent activity, diff review, file changes (320px, closeable)
- **Status Bar** — CWD switcher, background plan indicator, diagnostics
- **Settings** — Full modal overlay (Ctrl+,) with 12 tabs

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Ctrl+B | Toggle sidebar |
| Ctrl+, | Open settings |
| Ctrl+M | Toggle memory panel |
| Ctrl+N | New session |
| Ctrl+K | Command palette |
| Ctrl+J | Quick session switcher |
| Ctrl+E | Expanded editor |
| Ctrl+O | Quick model picker |
| Ctrl+Shift+E | Export conversation |
| Ctrl+Shift+C | Create checkpoint |
| Ctrl+Shift+S | Stash prompt |
| Ctrl+Shift+R | Restore stashed prompt |
| Ctrl+Shift+Z | Undo file changes |
| Ctrl+Shift+Y | Redo file changes |
| Ctrl+/ | Search conversation |
| Ctrl+` | Toggle terminal |
| Enter or Ctrl+Enter | Send message (configurable) |

---

## File Map

```
src/
├── App.tsx                          # Root: startup, dialogs, deep links, scheduler
├── index.tsx                        # SolidJS mount point
├── index.css                        # Global styles, density utilities, code themes
├── styles/tokens.css                # Design tokens (semantic colors, glass, borders)
│
├── components/
│   ├── layout/
│   │   ├── AppShell.tsx             # 3-panel layout, drag-resize handles, shortcuts
│   │   ├── ActivityBar.tsx          # Left icon bar (dynamic order from settings)
│   │   ├── MainArea.tsx             # Center content area
│   │   ├── SidebarPanel.tsx         # Right sidebar container
│   │   └── StatusBar.tsx            # CWD switcher, background plan badge, diagnostics
│   │
│   ├── chat/
│   │   ├── ChatView.tsx             # Chat orchestrator (messages + input + context + clipboard)
│   │   ├── MessageList.tsx          # Virtual-scrolled list, adaptive limit, scroll backfill
│   │   ├── MessageBubble.tsx        # Single message (markdown, tokens, cost)
│   │   ├── MessageInput.tsx         # Input + toolbar (voice, editor, sandbox, doom loop)
│   │   ├── MessageActions.tsx       # Copy, edit, fork, rewind, delete per message
│   │   ├── MarkdownContent.tsx      # Markdown renderer with syntax highlighting
│   │   ├── ContextBar.tsx           # Token usage progress bar below input
│   │   ├── ToolCallCard.tsx         # Tool execution display card
│   │   ├── ToolCallGroup.tsx        # Grouped tool calls
│   │   ├── tool-call-output.tsx     # Tool output rendering (+ MCP UIResource)
│   │   ├── tool-call-icon.tsx       # Per-tool-type icons
│   │   ├── tool-call-utils.ts       # Tool display helpers
│   │   ├── tool-call-error-boundary.tsx  # Error boundary for tool cards
│   │   ├── active-tool-indicator.tsx # Running tool status indicator
│   │   ├── ApprovalDock.tsx         # Inline tool approval (Enter/Escape, risk levels)
│   │   ├── FocusChainBar.tsx        # Task progress bar (bridges todoread/todowrite)
│   │   ├── GitControlStrip.tsx      # In-chat git: branch, pull, push, PR
│   │   ├── ThinkingBlock.tsx        # Collapsible reasoning/thinking blocks
│   │   ├── MCPResourceRenderer.tsx  # MCP resource widgets (table/form/chart/image)
│   │   ├── SubagentCard.tsx         # Delegation task display (goal, status, timeline)
│   │   ├── ProjectStatsView.tsx     # Project-level usage stats + charts
│   │   ├── UsageDetailsDialog.tsx   # Session/project usage breakdown dialog
│   │   ├── PlanBranchSelector.tsx   # Plan mode branch create/switch/compare/merge
│   │   ├── SearchBar.tsx            # Full-text conversation search + highlighting
│   │   ├── SessionSwitcher.tsx      # Ctrl+J fuzzy session switcher overlay
│   │   ├── ExpandedEditor.tsx       # Ctrl+E full-screen prompt editor
│   │   ├── QuickModelPicker.tsx     # Ctrl+O model picker grouped by provider
│   │   ├── MessageQueueBar.tsx      # Queued messages indicator + expand/remove
│   │   ├── DateSeparator.tsx        # Date dividers between messages
│   │   ├── ModelChangeIndicator.tsx # Model switch mid-session indicator
│   │   ├── ShortcutHint.tsx         # Dynamic Enter/Ctrl+Enter hint
│   │   ├── EditForm.tsx             # Inline message editing + resubmit
│   │   ├── DoomLoopBanner.tsx       # Agent stuck-in-loop warning (stop/retry/switch)
│   │   ├── TypingIndicator.tsx      # LLM typing animation
│   │   │
│   │   ├── message-input/           # MessageInput subcomponents
│   │   │   ├── text-area.tsx        # Auto-resize textarea
│   │   │   ├── toolbar-buttons.tsx  # Toolbar action buttons
│   │   │   ├── model-selector.tsx   # Model picker in input toolbar
│   │   │   ├── file-mention-popover.tsx  # @ file mention autocomplete
│   │   │   ├── attachment-previews.tsx   # File attachment previews
│   │   │   ├── attachments.ts       # Attachment upload logic
│   │   │   └── types.ts             # Message input types
│   │   │
│   │   └── message-list/            # MessageList subcomponents
│   │       ├── message-row.tsx      # Individual message row wrapper
│   │       └── sections.tsx         # Date sections + grouping
│   │
│   ├── sidebar/
│   │   ├── SidebarSessions.tsx      # Session list with search, tree/list toggle, context menu
│   │   ├── SessionBranchTree.tsx    # Collapsible session tree (parentSessionId hierarchy)
│   │   ├── SidebarExplorer.tsx      # File tree (read-only lock, context menu)
│   │   └── SidebarMemory.tsx        # Memory items sidebar
│   │
│   ├── panels/
│   │   ├── CodeEditorPanel.tsx      # CodeMirror 6 file viewer (read-only)
│   │   ├── MemoryPanel.tsx          # Bottom panel: episodic + semantic memory
│   │   ├── TerminalPanel.tsx        # xterm.js interactive terminal
│   │   ├── TeamPanel.tsx            # Dev team hierarchy tree (3-tier Praxis)
│   │   ├── TeamMemberChat.tsx       # Scoped chat per team member
│   │   ├── AgentActivityPanel.tsx   # Right panel: agent status cards
│   │   ├── FileOperationsPanel.tsx  # File change history with undo
│   │   └── MemoryBrowserPanel.tsx   # Cross-session memory browser (search, filter, delete)
│   │
│   ├── settings/
│   │   ├── SettingsModal.tsx        # Full-page modal with sidebar nav (12 tabs)
│   │   ├── settings-modal-config.ts # Tab definitions
│   │   ├── settings-modal-content.tsx # Tab content router
│   │   ├── settings-modal-header.tsx  # Header with search
│   │   ├── settings-modal-sidebar.tsx # Navigation sidebar
│   │   ├── settings-general-section.tsx  # General settings
│   │   ├── settings-about-section.tsx    # About/version info
│   │   ├── settings-agent-edit-modal.tsx # Agent create/edit modal
│   │   ├── settings-keybinding-edit-modal.tsx # Keybinding editor
│   │   ├── settings-field-group.tsx      # Reusable field group component
│   │   ├── SettingsCard.tsx         # Bento card wrapper (icon, title, description, action slot)
│   │   ├── DeviceCodeDialog.tsx     # OAuth device code flow dialog
│   │   ├── OllamaModelBrowser.tsx   # Ollama model list/pull/delete
│   │   └── tabs/
│   │       ├── AppearanceTab.tsx    # Theme presets, accent, fonts, density, sidebar order
│   │       ├── BehaviorTab.tsx      # Send key, clipboard, model aliases, watch mode
│   │       ├── LLMTab.tsx           # Max tokens, temperature, topP, custom instructions
│   │       ├── AgentsTab.tsx        # 3-tier agent configuration (Commander/Lead/Worker)
│   │       ├── PermissionsTab.tsx   # Granular per-tool approval rules
│   │       ├── CommandsTab.tsx      # Custom TOML commands CRUD
│   │       ├── MCPServersTab.tsx    # MCP server browser + management + OAuth status
│   │       ├── MicroagentsTab.tsx   # Built-in skills + custom skill CRUD
│   │       ├── TrustedFoldersTab.tsx # Allow/deny directory boundaries (glob support)
│   │       ├── PluginsTab.tsx       # Plugin manager (catalog, sort, ratings, git install)
│   │       ├── KeybindingsTab.tsx   # Keyboard shortcut customization
│   │       ├── DeveloperTab.tsx     # Dev console + extension testing
│   │       └── providers/           # Provider settings subcomponents
│   │           ├── providers-tab.tsx         # Provider grid layout
│   │           ├── providers-tab-grid.tsx    # Grid container
│   │           ├── providers-tab-header.tsx  # Header with count
│   │           ├── provider-card.tsx         # Provider card (collapsed)
│   │           ├── provider-card-expanded.tsx # Provider card (expanded)
│   │           ├── provider-card-status.tsx  # Connection status indicator
│   │           ├── provider-row-api-key-input.tsx  # API key input
│   │           └── provider-row-clear-confirm.tsx  # Clear credentials confirm
│   │
│   ├── dialogs/
│   │   ├── OnboardingDialog.tsx     # First-run setup wizard
│   │   ├── PermissionDialog.tsx     # Tool permission approval
│   │   ├── WorkspaceSelectorDialog.tsx  # Project/workspace picker
│   │   ├── WorkflowDialog.tsx       # Workflow create/edit dialog
│   │   ├── AddMCPServerDialog.tsx   # MCP server add (presets + manual)
│   │   ├── CheckpointDialog.tsx     # Named session snapshot
│   │   ├── ChangelogDialog.tsx      # "What's New" on update
│   │   ├── UpdateDialog.tsx         # Auto-updater download + install
│   │   ├── ExportOptionsDialog.tsx  # Export with redaction options
│   │   ├── MCPOAuthDialog.tsx       # MCP server OAuth consent flow (PKCE)
│   │   ├── CronPickerDialog.tsx     # Visual cron expression builder
│   │   └── SandboxReviewDialog.tsx  # Staged changes review (accept/reject)
│   │
│   ├── plugins/
│   │   ├── PluginWizard.tsx         # Multi-step plugin creation (4 templates)
│   │   └── PublishDialog.tsx        # Plugin publish flow stub (3-step)
│   │
│   ├── ui/                          # Design system primitives
│   │   ├── Button.tsx, Card.tsx, Badge.tsx
│   │   ├── Input.tsx, Select.tsx, Toggle.tsx, Checkbox.tsx
│   │   ├── Dialog.tsx, AlertDialog.tsx, ConfirmDialog.tsx, InputDialog.tsx
│   │   ├── Toast.tsx, ChatBubble.tsx, Avatar.tsx
│   │   ├── DiffViewer.tsx           # Unified + split diff view
│   │   ├── FileTree.tsx, ContextMenu.tsx
│   │   └── index.ts
│   │
│   ├── sessions/
│   │   ├── SessionList.tsx
│   │   └── SessionListItem.tsx
│   │
│   ├── projects/
│   │   └── ProjectSelector.tsx
│   │
│   ├── CommandPalette.tsx           # Ctrl+K command palette
│   ├── ErrorBoundary.tsx            # Error catch with recovery UI
│   └── SplashScreen.tsx             # Startup splash with logo
│
├── stores/
│   ├── settings/                    # Settings subsystem
│   │   ├── index.ts                 # Re-exports
│   │   ├── settings-types.ts        # AppSettings type definitions
│   │   ├── settings-defaults.ts     # Default values
│   │   ├── settings-persistence.ts  # localStorage read/write
│   │   ├── settings-hydration.ts    # Merge defaults + persisted
│   │   ├── settings-appearance.ts   # CSS variable application
│   │   └── settings-io.ts           # Export/import JSON
│   ├── session.ts                   # Session CRUD, messages, agents, file ops, checkpoints, branching
│   ├── layout.ts                    # Panel visibility, resize, code editor file
│   ├── team.ts                      # Dev team hierarchy (3-tier Praxis)
│   ├── project.ts                   # Current project directory, CWD switching
│   ├── shortcuts.ts                 # Keyboard shortcut registry
│   ├── workflows.ts                 # Workflow CRUD, scheduling, import/export
│   ├── plugins.ts                   # Plugin state, install/uninstall, git install, scope
│   ├── plugins-catalog.ts           # Remote plugin catalog fetch + cache
│   ├── focus-chain.ts               # Task progress tracking (todoread/todowrite bridge)
│   ├── plan-branches.ts             # Plan mode branch management
│   ├── sandbox.ts                   # Sandboxed file write review
│   ├── diagnostics.ts               # LSP diagnostics aggregation
│   ├── terminal.ts                  # Terminal/PTY state
│   └── session-persistence.ts       # Last-session-per-project restore
│
├── hooks/
│   ├── useChat.ts                   # Chat logic (send, stream, context, compaction, queue)
│   ├── useAgent.ts                  # Agent execution (create, run, persist, team mode)
│   └── agent/                       # Agent integration hooks
│       ├── index.ts                 # Re-exports
│       ├── agent-events.ts          # Agent event type definitions
│       ├── agent-team-bridge.ts     # Maps agent events → team hierarchy store
│       ├── agent-tool-activity.ts   # Tool activity tracking during runs
│       └── agent-types.ts           # Agent hook types
│
├── services/
│   ├── database.ts                  # SQLite via Tauri (sessions, messages, agents, files, workflows)
│   ├── migrations.ts                # DB schema V1-V4
│   ├── core-bridge.ts               # Initialize core singletons + settings sync + sandbox middleware
│   ├── extension-loader.ts          # Load 24 built-in extensions + plugin hot reload + sandboxing
│   ├── file-browser.ts              # Tauri FS: readDirectory, readFileContent
│   ├── file-search.ts               # File content search (grep via Tauri shell)
│   ├── file-versions.ts             # Per-session file undo/redo stacks
│   ├── file-watcher.ts              # Watch project for AI comment patterns (// AI!, // AI?)
│   ├── settings-fs.ts               # Tauri FS: settings persistence to disk
│   ├── notifications.ts             # Desktop notifications + AudioContext chime
│   ├── logger.ts                    # Structured logging
│   ├── project-detector.ts          # Auto-detect project type from directory
│   ├── project-database.ts          # Project metadata storage
│   ├── git-actions.ts               # Git operations (branch, pull, push, PR)
│   ├── git-extension.ts             # Clone/update/link/uninstall plugins from GitHub
│   ├── ide-integration.ts           # IDE detection, "Open in" + external editor
│   ├── voice-dictation.ts           # Web Speech API + AudioAnalyser + device selection
│   ├── custom-commands.ts           # TOML custom commands CRUD
│   ├── prompt-stash.ts              # Ctrl+Shift+S/R prompt drafts (localStorage)
│   ├── tool-approval-bridge.ts      # Bridge approval UI ↔ core settings
│   ├── workflows.ts                 # Workflow DB operations
│   ├── workflow-scheduler.ts        # Cron parser + scheduler (setInterval-based)
│   ├── mcp-oauth.ts                 # MCP OAuth PKCE (token store, refresh, revoke)
│   ├── plugins-fs.ts                # Plugin FS operations (install, uninstall, reload)
│   ├── tarball.ts                   # .tar.gz fetch + extract (for plugin install)
│   ├── auto-updater.ts              # @tauri-apps/plugin-updater integration
│   ├── clipboard-watcher.ts         # Clipboard polling for code detection
│   ├── deep-link.ts                 # ava:// URL protocol handler
│   ├── dev-console.ts               # Developer console service
│   ├── pty-bridge.ts                # Terminal PTY bridge (Rust IPC)
│   ├── providers/
│   │   └── model-fetcher.ts         # Dynamic model lists (OpenAI, OpenRouter, Ollama, Google, etc.)
│   ├── auth/
│   │   └── oauth.ts                 # OAuth PKCE flows (Google, GitHub Copilot)
│   └── llm/
│       └── bridge.ts                # Frontend → core LLM bridge
│
├── lib/
│   ├── markdown.ts                  # Markdown parsing + rendering
│   ├── syntax-highlight.ts          # Code block syntax highlighting
│   ├── motion.ts                    # Spring physics presets
│   ├── tool-approval.ts             # Tool approval logic (auto-approve, gate)
│   ├── cost.ts                      # Cost calculation + formatting
│   ├── context-budget.ts            # Context token budget calculations
│   ├── auth-helpers.ts              # OAuth flow helpers
│   ├── export-conversation.ts       # Conversation export with redaction + metadata
│   ├── export-workflow.ts           # Workflow import/export JSON serialization
│   └── simple-diff.ts              # Line-by-line diff for sandbox review
│
├── types/
│   ├── index.ts                     # Core types (Session, Message, Agent, ToolCall, Workflow)
│   ├── llm.ts                       # LLM provider types
│   ├── team.ts                      # TeamMember, TeamDomain, TeamHierarchy
│   ├── project.ts                   # Project types
│   └── plugin.ts                    # PluginPermission, permission metadata
│
├── contexts/
│   ├── theme.tsx                    # Theme context provider
│   └── notification.tsx             # Notification context
│
├── config/
│   ├── constants.ts                 # Defaults, storage keys
│   ├── env.ts                       # Environment detection
│   ├── mcp-presets.ts               # 12 curated MCP server presets
│   └── theme-presets.ts             # 14 named theme presets
│
├── pages/
│   └── DesignSystemPreview.tsx      # Component showcase page
│
└── stubs/
    └── node-stub.ts                 # Node.js module stubs for browser
```

---

## Settings Architecture

Settings are stored in `localStorage` and synced to the core engine via `core-bridge.ts`.

### Settings Groups (13 tabs)

| Tab | Key Settings |
|-----|-------------|
| **General** | App name, version, about |
| **Providers** | 14 LLM providers, API keys, OAuth tokens, base URLs |
| **Appearance** | Mode, dark variant, accent, 14 theme presets, fonts, density, sidebar order |
| **LLM** | Max tokens, temperature, topP, custom instructions, agent limits |
| **Behavior** | Send key, auto-scroll, notifications, clipboard watcher, model aliases, watch mode |
| **Agents** | 3-tier agent config (Commander → Senior Leads → Workers), import/export |
| **Permissions** | Granular per-tool approval rules (allow/ask/deny with globs) |
| **Commands** | Custom TOML commands CRUD |
| **MCP** | MCP server browser (12 presets) + manual config + OAuth status |
| **Microagents** | Built-in skills enable/disable (8 skills) + custom skill CRUD |
| **Trusted Folders** | Allow/deny directory boundaries with glob support |
| **Plugins** | Catalog, sort, ratings, git install, link local, sandboxing, wizard, publish |
| **Shortcuts** | Keyboard shortcut customization |

### Data Management

- **Export** — Download all settings as JSON
- **Import** — Upload JSON, deep-merge with existing
- **Clear All** — `localStorage.clear()` + reload

---

## Appearance System

### Theme Modes
- `light` / `dark` / `system` (auto-follows OS via `matchMedia`)
- Dark variants: `dark` (default), `midnight` (OLED black), `charcoal` (warm dark)

### Theme Presets
14 named presets: catppuccin-mocha, catppuccin-latte, dracula, nord, gruvbox-dark, gruvbox-light, solarized-dark, solarized-light, tokyo-night, rose-pine, one-dark, github-dark, moonlight, everforest

### Accent Colors
6 presets: `violet` (default), `blue`, `green`, `rose`, `amber`, `cyan`
Plus custom hex input with computed accent variants via `hexToAccentVars()`

### Code Themes
6 presets via `[data-code-theme]` attribute + 8 `--syntax-*` CSS variables

### Density
3 levels: `compact`, `default`, `comfortable`
Applied via CSS variables: `--density-py`, `--density-px`, `--density-gap`, `--density-section-py/px`

### Fonts
- Mono: Geist Mono (default), JetBrains Mono, Fira Code
- Sans: Default, Inter, Outfit, Nunito
- Chat font size: 11-20px

---

## Data Flow

```
MessageInput → useChat → core-bridge → core LLM client → Provider API
                                                              ↓
                                              Streaming SSE response
                                                              ↓
                                              MessageList → MessageBubble

Agent delegation (Praxis 3-tier):
  useAgent → agent-team-bridge → Commander → Senior Lead → Worker
                                    ↓              ↓           ↓
                                team.ts        SubagentCard  ToolCallCard
```

### Database (SQLite via Tauri)

| Table | Purpose |
|-------|---------|
| sessions | Session metadata (id, title, model, project, created_at) |
| messages | Chat messages (role, content, tokens, cost, thinking) |
| agents | Agent records (type, status, model, result, tier) |
| file_operations | File changes by agents |
| terminal_executions | Shell command history |
| memory_items | Episodic memory + checkpoints |
| workflows | Saved workflows with schedules |
| project_stats | Aggregated usage stats per project |

### State Management

All stores use SolidJS `createSignal` / `createMemo` / `createStore`. No external state library.

| Store | Responsibility |
|-------|---------------|
| `settings/` | All app settings, appearance, provider credentials |
| `session.ts` | Current session, messages, agents, file ops, checkpoints, branching, read-only files |
| `layout.ts` | Panel visibility, sidebar state, drag-resize, code editor file |
| `team.ts` | Dev team hierarchy (3-tier Praxis), member status |
| `project.ts` | Current working directory, CWD switching |
| `shortcuts.ts` | Keyboard shortcut bindings |
| `workflows.ts` | Workflow CRUD, scheduling, import/export |
| `plugins.ts` | Plugin install state, git install, scope, lifecycle |
| `plugins-catalog.ts` | Remote catalog fetch + localStorage cache (30-min TTL) |
| `focus-chain.ts` | Task progress tracking (bridges todoread/todowrite events) |
| `plan-branches.ts` | Plan mode branch create/switch/compare/merge |
| `sandbox.ts` | Sandboxed file writes pending review |
| `diagnostics.ts` | LSP error/warning aggregation |
| `terminal.ts` | Terminal PTY state |
| `session-persistence.ts` | Per-project last-session restore |

---

## Tauri Integration

### Plugins Used
- `@tauri-apps/plugin-fs` — File system (read, write, watch)
- `@tauri-apps/plugin-sql` — SQLite database
- `@tauri-apps/plugin-dialog` — Native file/folder pickers
- `@tauri-apps/plugin-shell` — Shell commands, PTY bridge
- `@tauri-apps/plugin-window-state` — Remember window size/position
- `@tauri-apps/plugin-opener` — Open URLs/files in default apps
- `@tauri-apps/plugin-updater` — Auto-update (optional, graceful degradation)
- `@tauri-apps/plugin-deep-link` — ava:// URL scheme (optional)

### Security
- **CSP** enabled in `tauri.conf.json`
- **Scoped FS** — Limited to `$APPDATA/**` and `$HOME/.ava/**`
- **Deferred window show** — `visible: false` + `show()` after mount
- **Plugin sandboxing** — Permission model (fs, network, shell, clipboard)

### Lazy Import Pattern
All Tauri plugin imports are lazy to avoid top-level import issues:
```typescript
async function getFsModule() {
  try { return await import('@tauri-apps/plugin-fs') }
  catch { return null }
}
```
Optional plugins use opaque module names to prevent Vite static analysis:
```typescript
const PKG = ['@tauri-apps', 'plugin-updater'].join('/')
const mod = await import(/* @vite-ignore */ PKG)
```

---

## WebKitGTK Gotchas (Linux/Tauri)

| Issue | Fix |
|-------|-----|
| DMABUF ghost rendering (NVIDIA + Wayland) | `WEBKIT_DISABLE_DMABUF_RENDERER=1` in `main.rs` |
| Nested `<button>` crash | `<div role="button" tabIndex={0}>` |
| `pointer-events: none` on fixed pseudo-elements | Don't use fixed overlays |
| Sidebar margin animation bleed | Use `width: 0` + `overflow: hidden`, not `margin-left` |
| Scroll jank | `transform: translateZ(0)` for GPU compositing |
| Long session perf | `content-visibility: auto` + adaptive visible limit + scroll backfill |
| Hover reflow | Use `opacity`/`color` changes, not `translate-y` |
| `transition-all` jank | Use `transition-colors` instead |
| `cc` linker not found (Pop OS) | `src-tauri/.cargo/config.toml` with `linker = "gcc-14"` |

---

## Related Docs

- [Design System](./design-system.md) — Colors, glass, typography, components, motion
- [Changelog](./changelog.md) — What was built, session by session
- [Backlog](./backlog.md) — What's missing, prioritized
- [Architecture](../architecture/) — System design
- [Backend Modules](../backend/modules.md) — Core engine documentation
- [Plugin SDK](../plugins/PLUGIN_SDK.md) — Plugin development guide
