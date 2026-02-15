# Frontend

> Desktop app built with SolidJS + Tauri v2

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
│      │  Bottom Panel (Memory, Ctrl+M)   │          │
│  B   │                                  │          │
│  a   ├──────────────────────────────────┤          │
│  r   │  Right Panel (Agent Activity)    │          │
└──────┴──────────────────────────────────┴──────────┘
```

- **Activity Bar** (48px, left) — 2 icons: Sessions, Explorer
- **Main Area** — Chat with LLM, code editor, tool call cards
- **Sidebar** (right) — Sessions list or file explorer
- **Bottom Panel** — Memory panel (resizable 100-400px, Ctrl+M toggle)
- **Right Panel** — Agent activity (320px, closeable)
- **Settings** — Full modal overlay (Ctrl+,)

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Ctrl+B | Toggle sidebar |
| Ctrl+, | Open settings |
| Ctrl+M | Toggle memory panel |
| Ctrl+N | New session |
| Enter or Ctrl+Enter | Send message (configurable) |

---

## File Map

```
src/
├── App.tsx                          # Root + startup (core-bridge init, splash, onboarding)
├── index.tsx                        # SolidJS mount point
├── index.css                        # Global styles, density utilities, code themes
├── styles/tokens.css                # Design tokens (semantic colors, glass, borders)
│
├── components/
│   ├── layout/
│   │   ├── AppShell.tsx             # 3-panel layout, resizable, keyboard shortcuts
│   │   ├── ActivityBar.tsx          # Left icon bar (sessions + explorer)
│   │   ├── MainArea.tsx             # Center content area
│   │   ├── SidebarPanel.tsx         # Right sidebar container
│   │   └── StatusBar.tsx            # Bottom status line
│   │
│   ├── chat/
│   │   ├── ChatView.tsx             # Chat orchestrator (messages + input + context)
│   │   ├── MessageList.tsx          # Virtual-scrolled message list
│   │   ├── MessageBubble.tsx        # Single message (markdown, tokens, cost)
│   │   ├── MessageInput.tsx         # Input with model selector, permission, attachments
│   │   ├── MessageActions.tsx       # Copy, edit, fork, delete actions per message
│   │   ├── MarkdownContent.tsx      # Markdown renderer with syntax highlighting
│   │   ├── ContextBar.tsx           # Token usage progress bar below input
│   │   ├── ToolCallCard.tsx         # Tool execution display card
│   │   ├── ToolCallGroup.tsx        # Grouped tool calls
│   │   ├── DateSeparator.tsx        # Date dividers between messages
│   │   ├── ModelChangeIndicator.tsx # Shows when model switches mid-session
│   │   ├── ShortcutHint.tsx         # Dynamic Enter/Ctrl+Enter hint
│   │   ├── EditForm.tsx             # Inline message editing
│   │   └── TypingIndicator.tsx      # LLM typing animation
│   │
│   ├── sidebar/
│   │   ├── SidebarSessions.tsx      # Session list with search, context menu
│   │   ├── SidebarExplorer.tsx      # File tree (Tauri FS, lazy-load, expand/collapse)
│   │   └── SidebarMemory.tsx        # Memory items sidebar
│   │
│   ├── panels/
│   │   ├── CodeEditorPanel.tsx      # CodeMirror 6 file viewer (read-only)
│   │   ├── MemoryPanel.tsx          # Bottom panel: episodic + semantic memory
│   │   ├── TerminalPanel.tsx        # Terminal execution history
│   │   ├── TeamPanel.tsx            # Dev team hierarchy tree
│   │   ├── TeamMemberChat.tsx       # Scoped chat per team member
│   │   ├── AgentActivityPanel.tsx   # Right panel: agent status cards
│   │   └── FileOperationsPanel.tsx  # File change history
│   │
│   ├── settings/
│   │   ├── SettingsModal.tsx        # Full-page modal with left sidebar nav
│   │   ├── DeviceCodeDialog.tsx     # OAuth device code flow dialog
│   │   └── tabs/
│   │       ├── ProvidersTab.tsx     # 14 LLM providers, API keys, OAuth
│   │       ├── AppearanceTab.tsx    # Theme, accent, fonts, density, scale
│   │       ├── LLMTab.tsx           # Max tokens, temperature, topP, custom instructions
│   │       ├── BehaviorTab.tsx      # Send key, auto-scroll, notifications, sound
│   │       ├── AgentsTab.tsx        # Agent configuration
│   │       ├── MCPServersTab.tsx    # MCP server management
│   │       └── KeybindingsTab.tsx   # Keyboard shortcut customization
│   │
│   ├── dialogs/
│   │   ├── OnboardingDialog.tsx     # First-run setup wizard
│   │   ├── PermissionDialog.tsx     # Tool permission approval
│   │   ├── ToolApprovalDialog.tsx   # Tool execution approval
│   │   ├── ModelSelectorDialog.tsx  # Model picker dropdown
│   │   └── WorkspaceSelectorDialog.tsx
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
│   ├── settings.ts                  # AppSettings (localStorage), appearance, sync to core
│   ├── session.ts                   # Session CRUD, messages, agents, file ops, checkpoints
│   ├── layout.ts                    # Panel visibility, code editor file signal
│   ├── team.ts                      # Dev team hierarchy (TeamMember, delegation)
│   ├── project.ts                   # Current project directory
│   └── shortcuts.ts                 # Keyboard shortcut registry
│
├── hooks/
│   ├── useChat.ts                   # Chat logic (send, stream, context tracking, compaction)
│   └── useAgent.ts                  # Agent execution (create, run, persist to DB)
│
├── services/
│   ├── database.ts                  # SQLite via Tauri (sessions, messages, agents, files)
│   ├── migrations.ts                # DB schema V1-V4
│   ├── core-bridge.ts               # Initialize core singletons + settings sync
│   ├── file-browser.ts              # Tauri FS: readDirectory, readFileContent
│   ├── settings-fs.ts               # Tauri FS: settings persistence to disk
│   ├── notifications.ts             # Desktop notifications + AudioContext chime
│   ├── platform.ts                  # Platform detection + initialization
│   ├── project-detector.ts          # Auto-detect project type from directory
│   ├── project-database.ts          # Project metadata storage
│   ├── logger.ts                    # Structured logging
│   ├── providers/
│   │   └── model-fetcher.ts         # Dynamic model lists (OpenAI, OpenRouter, Ollama, Google, Anthropic)
│   ├── auth/
│   │   └── oauth.ts                 # OAuth PKCE flows (Google, GitHub Copilot)
│   └── llm/
│       └── bridge.ts                # Frontend → core LLM bridge
│
├── lib/
│   ├── markdown.ts                  # Markdown parsing + rendering
│   ├── syntax-highlight.ts          # Code block syntax highlighting
│   ├── motion.ts                    # Spring physics presets
│   └── tool-approval.ts             # Tool approval logic (auto-approve, gate)
│
├── types/
│   ├── index.ts                     # Core types (Session, Message, Agent, Settings)
│   ├── llm.ts                       # LLM provider types
│   ├── team.ts                      # TeamMember, TeamDomain, TeamHierarchy
│   └── project.ts                   # Project types
│
├── contexts/
│   ├── theme.tsx                    # Theme context provider
│   └── notification.tsx             # Notification context
│
├── config/
│   ├── constants.ts                 # Defaults, storage keys
│   └── env.ts                       # Environment detection
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

### Settings Groups (7 tabs)

| Tab | Key Settings |
|-----|-------------|
| **Providers** | 14 LLM providers, API keys, OAuth tokens, base URLs |
| **Appearance** | Mode (light/dark/system), dark variant (dark/midnight/charcoal), accent (6 presets + custom hex), mono font, sans font, UI scale, chat font size, ligatures, high contrast, density |
| **LLM** | Max tokens, temperature, topP, custom instructions, agent max turns, agent max time |
| **Behavior** | Send key (Enter/Ctrl+Enter), auto-scroll, auto-title, line numbers, word wrap, notifications, sound |
| **Agents** | Agent configuration |
| **MCP** | MCP server management |
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

### Accent Colors
6 presets: `violet` (default), `blue`, `green`, `rose`, `amber`, `cyan`
Plus custom hex input with computed accent variants via `hexToAccentVars()`

### Code Themes
6 presets via `[data-code-theme]` attribute + 8 `--syntax-*` CSS variables

### Density
3 levels: `compact`, `default`, `comfortable`
Applied via CSS variables: `--density-py`, `--density-px`, `--density-gap`, `--density-section-py/px`
8 components wired: MessageBubble, MessageInput, MessageList, ContextBar, SidebarSessions, SidebarExplorer, MemoryPanel, TerminalPanel

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
```

### Database (SQLite via Tauri)

| Table | Purpose |
|-------|---------|
| sessions | Session metadata (id, title, model, created_at) |
| messages | Chat messages (role, content, tokens, cost) |
| agents | Agent records (type, status, model, result) |
| file_operations | File changes by agents |
| terminal_executions | Shell command history |
| memory_items | Episodic memory + checkpoints |

### State Management

All stores use SolidJS `createSignal` / `createMemo`. No external state library.

| Store | Responsibility |
|-------|---------------|
| `settings.ts` | All app settings, appearance, provider credentials |
| `session.ts` | Current session, messages, agents, file ops, checkpoints |
| `layout.ts` | Panel visibility, sidebar state, code editor file |
| `team.ts` | Dev team hierarchy, member status |
| `project.ts` | Current working directory |
| `shortcuts.ts` | Keyboard shortcut bindings |

---

## Tauri Integration

### Plugins Used
- `@tauri-apps/plugin-fs` — File system (read directory, read files, write settings)
- `@tauri-apps/plugin-sql` — SQLite database
- `@tauri-apps/plugin-dialog` — Native file/folder pickers
- `@tauri-apps/plugin-window-state` — Remember window size/position

### Security
- **CSP** enabled in `tauri.conf.json`
- **Scoped FS** — Limited to `$APPDATA/**` and `$HOME/.ava/**`
- **Deferred window show** — `visible: false` + `show()` after mount

### Lazy Import Pattern
All Tauri plugin imports are lazy to avoid top-level import issues:
```typescript
async function getFsModule() {
  try { return await import('@tauri-apps/plugin-fs') }
  catch { return null }
}
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
