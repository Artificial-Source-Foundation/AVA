# Frontend Changelog

> Session-by-session record of what was built in the desktop app.

---

## Session 56 — Sprint 2.4 Plugin Devx Foundation (2026-02-14)

- **Plugin scaffold command** — new `ava plugin init` in `cli/src/commands/plugin.ts` creates plugin starter packages
- **CLI routing** — `cli/src/index.ts` now dispatches `plugin` subcommands and shows plugin usage in help output
- **AVA command alias** — `cli/package.json` now exposes `ava` bin while keeping `estela` for backward compatibility
- **Scaffold tests** — `cli/src/commands/plugin.test.ts` validates file generation and non-empty directory guard behavior
- **Developer template docs** — new `docs/plugins/PLUGIN_TEMPLATE.md` describes generated structure, conventions, and workflow
- **Auth/help consistency** — `cli/src/commands/auth.ts` usage examples updated to `ava` naming

---

## Session 55 — Sprint 2.3 Plugin Browser Surfaces (2026-02-14)

- **Shared plugin domain store** — `src/stores/plugins.ts` now owns catalog, filters, install state, and settings target intent
- **Marketplace data adapter** — `src/services/plugins/catalog.ts` provides typed plugin catalog used by both surfaces
- **Lifecycle adapter** — `src/services/plugins/lifecycle.ts` adds install/uninstall persistence with AVA + legacy key mirroring
- **Settings plugin browser** — new `src/components/settings/tabs/PluginsTab.tsx` replaces placeholder with search, categories, featured, and install controls
- **Sidebar plugin surface** — new `src/components/sidebar/SidebarPlugins.tsx` adds compact quick install/uninstall and settings entry
- **Activity wiring** — `ActivityBar`, `SidebarPanel`, and `layout` store now support `plugins` activity
- **Plugin types** — `src/types/plugin.ts` + `src/types/index.ts` export plugin domain interfaces
- **Tests** — added `src/stores/plugins.test.ts`, updated `PluginsTab.smoke.test.tsx`, and expanded `layout.test.ts` coverage for plugins activity

---

## Session 54 — Project Hub + Project-Scoped Sessions (2026-02-14)

- **Project hub screen** — `ProjectHub.tsx` adds startup hub with open-project and resume actions
- **Auto-resume behavior** — `App.tsx` now restores the last session for the active project on startup
- **Session persistence map** — `session-persistence.ts` stores `lastSessionByProject` in localStorage
- **Project-safe session fallback** — `session.ts` archive/delete fallback now creates replacement sessions in the active project
- **Sidebar quick switching** — `SidebarSessions.tsx` gets Hub/Open actions and a compact project switch dropdown
- **No-project guards** — `useChat.ts` and `useAgent.ts` block execution until a project is selected
- **Layout state** — persisted hub/workspace toggle in `layout.ts` + tests in `layout.test.ts`
- New files: `ProjectHub.tsx`, `session-persistence.ts`, `session.test.ts`
- Modified: `App.tsx`, `SidebarSessions.tsx`, `layout.ts`, `session.ts`, `constants.ts`

---

## Session 53 — File Watcher + Step-Level Undo (2026-02-09)

- **File watcher service** — `src/services/file-watcher.ts` (~270 lines) watches project dir via Tauri FS `watch()` (500ms debounce, recursive)
- **6 AI patterns** — `// AI!`, `// AI?`, `# AI!`, `# AI?`, `-- AI!`, `-- AI?` across 30+ scannable extensions
- **Dedup tracking** — `processedHashes` Set prevents re-triggering same comment (key: `filePath:lineNumber:content`)
- **ChatView wiring** — `createEffect` starts/stops watcher based on `settings().behavior.fileWatcher` + `currentProject()?.directory`; `onComment` auto-sends as chat message with file context
- **Settings toggle** — `fileWatcher: boolean` in BehaviorSettings, toggle in Behavior tab
- **FS permissions** — `fs:allow-watch` and `fs:allow-unwatch` added to Tauri capabilities
- **Undo button** — Undo2 icon in MessageInput toolbar, calls `chat.undoLastEdit()` → `undoLastAutoCommit()` (git revert), 2.5s status feedback
- **Undo visibility** — Only shows when git auto-commit is enabled in settings
- New file: `src/services/file-watcher.ts`
- Modified: `ChatView.tsx`, `MessageInput.tsx`, `BehaviorTab.tsx`, `settings.ts`

---

## Session 52 — Message Queue + Steering (2026-02-09)

- **Message queue** — `useChat` `messageQueue` signal queues follow-up messages when streaming; `processQueue()` auto-dequeues in `finally` block
- **Steer function** — `steer()` replaces queue with single message, aborts current stream; `processQueue` picks it up
- **Cancel clears queue** — `cancel()` now clears queue + aborts (stop = stop everything)
- **Type-ahead** — Textarea stays enabled during streaming so user can type ahead
- **Queue badge** — Shows queued message count in toolbar
- **Send/Queue button** — Changes style during streaming to indicate queue mode
- **Keyboard shortcut** — `Ctrl+Shift+Enter` triggers steer (cancel + send immediately)
- **Session switch** — `createEffect` watching session ID calls `clearQueue()`
- Modified: `useChat.ts`, `MessageInput.tsx`

---

## Session 51 — OAuth Fix + Error Logging (2026-02-09)

- **Root cause** — OpenAI OAuth tokens stored as plain API keys → core saw `type: 'api-key'` → wrong endpoint
- **Fix** — `storeOAuthCredentials()` routes by provider: Anthropic → API key, OpenAI/Copilot → `setStoredAuth(type:'oauth')` with `accountId` from JWT
- **JWT parsing** — `decodeJwtPayload()` + `extractAccountId()` for ChatGPT account ID from `id_token`
- **Scopes** — Reverted incorrect `model.request` scope
- **CSP** — Added `https://chatgpt.com` to `connect-src`
- **OAuth disconnect UI** — "Connected via OAuth" badge + LogOut button in ProvidersTab
- **Error logging** — Structured logging via file logger across entire OAuth flow
- **Browser opener** — `@tauri-apps/plugin-shell` → `@tauri-apps/plugin-opener`
- Modified: `oauth.ts`, `ProvidersTab.tsx`, `tauri.conf.json`

---

## Session 50 — Architect + Editor Model Split (2026-02-09)

- **Core config** — `editorModel` + `editorModelProvider` optional fields on `ProviderSettings`
- **Helper** — `getEditorModelConfig()` in `llm/client.ts`, exported from `llm/index.ts`
- **Commander wired** — `commander/executor.ts` auto-applies editor model to workers when no per-worker override
- **Frontend** — `editorModel` field in `GenerationSettings`, dropdown in LLMTab with 8 editor model presets
- **Auto-pair** — Button suggests editor model based on primary (Opus → Sonnet, Sonnet → Haiku, o1/o3 → GPT-4o)
- **Settings sync** — `pushSettingsToCore()` bridges `editorModel` to core `ProviderSettings`
- Modified: `config/types.ts`, `config/schema.ts`, `llm/client.ts`, `llm/index.ts`, `commander/executor.ts`, `settings.ts`, `LLMTab.tsx`

---

## Session 49 — Weak Model for Secondary Tasks (2026-02-09)

- **Core config** — `weakModel` + `weakModelProvider` optional fields on `ProviderSettings`
- **Helper** — `getWeakModelConfig()` in `llm/client.ts`, reads settings and infers provider from model name prefix
- **Planner wired** — `agent/planner.ts` uses `getWeakModelConfig()` instead of hardcoded `claude-sonnet-4-20250514`
- **Self-review wired** — `validator/self-review.ts` uses weak model for code review
- **Frontend** — `weakModel` field in `GenerationSettings`, dropdown in LLMTab with 9 model presets
- **Auto-pair** — Button suggests cheap model based on active primary (Sonnet → Haiku, GPT-4o → GPT-4o-mini)
- **Settings sync** — `pushSettingsToCore()` bridges `weakModel` to core `ProviderSettings`
- Modified: `config/types.ts`, `config/schema.ts`, `llm/client.ts`, `llm/index.ts`, `agent/planner.ts`, `validator/self-review.ts`, `settings.ts`, `LLMTab.tsx`

---

## Session 48 — Git Auto-Commit (2026-02-09)

- **Auto-commit module** — `packages/core/src/git/auto-commit.ts` stages + commits after file-modifying tools
- **Tool registry wiring** — PostToolUse in `registry.ts` calls `autoCommitIfEnabled()` for write locations
- **Undo action** — `undoLastAutoCommit()` reverts the most recent estela-prefixed commit via `git revert --no-edit`
- **Frontend settings** — `GitSettings` interface (enabled, autoCommit, commitPrefix) with BehaviorTab UI
- **Settings sync** — `pushSettingsToCore()` bridges frontend git settings to core `SettingsManager`
- New file: `packages/core/src/git/auto-commit.ts`
- Modified: `config/types.ts`, `tools/registry.ts`, `git/index.ts`, `settings.ts`, `BehaviorTab.tsx`, `useChat.ts`

---

## Session 47 — Backend Gaps + Polish (2026-02-09)

**4 backend gaps fixed** + paste collapse + docs reorg.

- **Paste collapse** — Large text pastes (>5 lines) collapsed into expandable chips in MessageInput; user messages >8 lines collapse in MessageBubble
- **Tool approval bridge** — Core agent loop `TOOL_CONFIRMATION_REQUEST` → SolidJS signal → ToolApprovalDialog → `TOOL_CONFIRMATION_RESPONSE` back to bus
- **MCP settings CRUD** — `mcpServers: MCPServerConfig[]` in settings store with `addMcpServer()`, `removeMcpServer()`, `updateMcpServer()`; SettingsModal maps to MCPServersTab
- **FS scope expansion** — Runtime `allow_project_path` Rust command via `FsExt` for project file access
- **Shell timeout** — `Promise.race()` wrapper in `TauriShell.exec()` when `options.timeout` is set
- **OAuth fix** — Corrected Anthropic (client ID, port 1455, API key minting) and OpenAI (port 1455, `/auth/callback`, extra params) configs
- **Dead mock removal** — Removed hardcoded `defaultMCPServers` from MCPServersTab (now uses real settings state)
- Commits: `0c9388c`, `28ba7ed`, `7d3e1a6`, `55caf7a`

---

## Session 45 — Frontend Gaps (2026-02-09)

**5 gaps closed** across 1 new file + 7 modified files.

- **File explorer** — `SidebarExplorer.tsx` rewritten with recursive `FileTreeNode`, lazy-load children via Tauri FS, dirs-first sort, hidden file filtering
- **Code editor file reading** — `CodeEditorPanel.tsx` now reads actual files via `readFileContent()`, auto-opens from explorer via `codeEditorFile` layout signal
- **Agent persistence** — `saveAgent()`, `getAgents()`, `updateAgentInDb()` in `database.ts`; wired in `session.ts` (`switchSession` loads, `addAgent`/`updateAgent` persist fire-and-forget)
- **Google models API** — `fetchGoogleModels()` via `generativelanguage.googleapis.com` with hardcoded fallback
- **DiffViewer split view** — `buildSplitPairs()` pairs remove+add lines; two-column table rendering with `mode='split'`
- New file: `src/services/file-browser.ts` (FileEntry, readDirectory, readFileContent)

---

## Session 44 — Settings Hardening (2026-02-08)

**16 new settings** across 4 sub-interfaces.

- **LLM tab** — maxTokens, temperature, topP, custom instructions, agent max turns, agent max time
- **Behavior tab** — sendKey (Enter vs Ctrl+Enter), autoScroll, autoTitle, lineNumbers, wordWrap, notifications, sound
- **Custom instructions** — Injected as system message in `buildApiMessages()` via `msgs.unshift()`
- **Send key** — Configurable in MessageInput + dynamic `ShortcutHint` component
- **Notifications** — Desktop notification (only when unfocused) + AudioContext chime with configurable volume
- **Code block settings** — `[data-line-numbers]` CSS counter + `[data-word-wrap]` pre-wrap
- **Data management** — Export (JSON download), Import (file picker + deep merge), Clear All
- New files: `LLMTab.tsx`, `BehaviorTab.tsx`, `src/services/notifications.ts`

---

## Session 42 — Density + Font Wiring (2026-02-08)

- **Density recalibrated** — compact 4/8px, default 6/12px, comfortable 8/16px
- **Section density** — `--density-section-py` / `--density-section-px` for panels/containers
- **8 components wired** — MessageBubble, MessageInput, MessageList, ContextBar, SidebarSessions, SidebarExplorer, MemoryPanel, TerminalPanel
- **CSS utility classes** — `.density-py/px/gap/section-py/section-px/section` in index.css
- **Chat font size** — Also applies to MessageInput textarea (was only MessageBubble)
- **Ligatures hint** — "(Fira Code, JetBrains Mono)" in toggle description

---

## Session 41 — Appearance Expansion (2026-02-08)

**8 new appearance features.**

- **System theme** — `mode: 'light' | 'dark' | 'system'`, `setupSystemThemeListener()` re-applies on OS change
- **Dark variants** — `darkStyle: 'dark' | 'midnight' | 'charcoal'` (midnight=OLED black, charcoal=warm dark)
- **Code themes** — 6 presets via `[data-code-theme]` + 8 `--syntax-*` vars
- **Custom accent** — `hexToAccentVars()` computes all 6 accent vars from hex input
- **Sans font** — `SansFont = 'default' | 'inter' | 'outfit' | 'nunito'`, sets `--font-sans`
- **Chat font size** — 11-20px via `--chat-font-size` in MessageBubble + MessageInput
- **High contrast** — `[data-high-contrast]` selector, stronger text/borders
- **localStorage bridge** — `saveSettings()` writes `estela-mode` for flash prevention

---

## Session 40 — Core Frontend Wiring (2026-02-08)

**Connected frontend to core engine.**

- **Core bridge** — `src/services/core-bridge.ts` initializes 5 core singletons (SettingsManager, ContextTracker, WorkerRegistry, MemoryManager)
- **Settings sync** — `pushSettingsToCore()` maps frontend AppSettings to core SettingsManager categories
- **Context tracking** — `useChat` tracks tokens via ContextTracker on send/complete
- **ContextBar** — `src/components/chat/ContextBar.tsx` shows token usage with progress bar
- **Session checkpoints** — `createCheckpoint()` / `rollbackToCheckpoint()` using memoryItems DB table
- **Agent memory** — Episodic memory recorded on successful agent runs via `getCoreMemory().remember()`
- New files: `core-bridge.ts`, `ContextBar.tsx`

---

## Session 39 — Appearance Tab (2026-02-08)

**Dedicated appearance settings tab.**

- **Dark/light mode** — Working toggle with CSS token overrides in `[data-mode="light"]`
- **Accent presets** — 6 colors via `[data-accent="X"]`: violet, blue, green, rose, amber, cyan
- **UI scale** — Slider 85%-120%, changes `html { font-size }` (all rem-based sizes scale)
- **Mono font selector** — Geist Mono (default), JetBrains Mono, Fira Code
- **`applyAppearance()`** — Exported from settings.ts, called on startup + every change
- **Settings tabs redesigned** — All tabs rewritten to flat minimal rows
- **Permission button** — Moved from ChatInfoBar to MessageInput toolbar
- Deleted: `ChatInfoBar.tsx`; Created: `AppearanceTab.tsx`

---

## Session 38 — Layout Rework (2026-02-08)

**Major layout restructuring.**

- **Activity bar slimmed** — 7 icons to 2 (sessions + explorer)
- **Settings modal** — Full-page settings replaced with OpenCode-inspired modal overlay
- **Right panel** — Agent activity on demand (320px, closeable)
- **Bottom panel** — Memory panel (resizable 100-400px, Ctrl+M toggle)
- **Model selector** — Dropdown in chat input, reads providers from settings
- Deleted: `navigation.ts`, `SettingsPage.tsx`, `SidebarAgents.tsx`, `SidebarPlugins.tsx`
- Created: `SettingsModal.tsx` (~600 lines)

---

## Session 37 — Phase 1 Completion (2026-02-07)

**Desktop app feature-complete.**

- **Provider expansion** — 14 providers in Settings UI (was 4), Google + Copilot OAuth, DeviceCodeDialog
- **Team delegation flow** — SVG animated lines, ParallelBadge, PhaseTimeline
- **Session fork** — "Fork from here" context menu, message count in session rows
- **Plugin browser shell** — Plugins tab in ActivityBar (placeholder for Phase 2)

---

## Session 36 — LLM Integration Fix (2026-02-07)

**Chat streaming working end-to-end.**

- **Root cause** — 3 disconnected credential stores (Settings UI, core config, LLM clients)
- **Fix** — `syncProviderCredentials()` + `syncAllApiKeys()` bridge; `anthropic-dangerous-direct-browser-access: true`
- **Working directory** — `useChat` + `useAgent` read from `useProject().currentProject().directory`
- **Tool approval** — Shared `src/lib/tool-approval.ts` (ApprovalRequest, checkAutoApproval, createApprovalGate)
- **Session duplicate** — `duplicateSessionMessages()` in database
- Deleted dead code: `src/services/llm/client.ts`, `src/services/llm/providers/`, `src/services/auth/credentials.ts`

---

## Earlier Sessions (Phase 1)

| Session | Focus | Key Deliverables |
|---------|-------|-----------------|
| 35 | Splash screen | Logo, status text, version, mesh gradient, min display time |
| 34 | WebKitGTK fixes | DMABUF ghost fix, nested button crash, cargo linker |
| 33 | Chat UI | MessageList virtual scroll, streaming, MessageBubble |
| 32 | Settings page | Provider tabs, API key inputs, OAuth flows |
| 31 | Team panel | Dev team hierarchy tree, SVG delegation lines |
| 30 | Code editor | CodeMirror 6 integration, One Dark theme |
| 29 | Session management | Create, switch, persistence, message history |
| 28 | Database | SQLite via Tauri, migrations V1-V4 |
| 27 | Layout foundation | AppShell, ActivityBar, SidebarPanel, resizable panels |

---

## Build Status

As of 2026-02-09:
- **0 TypeScript errors** (`tsc --noEmit`)
- **0 Biome errors** (3 intentional `!important` warnings in reduce-motion CSS)
- **Vite build passes** in ~8s with code splitting
- **1778 backend tests** across 64 files
- **0 TODO/FIXME markers** in `src/`
