# Frontend Backlog

> What's missing, prioritized. Updated 2026-03-02 (Sprint 23 frontend completion).

---

## Status Summary

| Phase | Status | Remaining |
|-------|--------|-----------|
| **1: Desktop App** | **Complete** | - |
| **1.5: Desktop Polish** | **Complete** | Manual testing only |
| **2: Plugin Ecosystem** | Nearly complete | Backend registry API remaining (all frontend UX shipped) |
| **2+: Competitive Gaps** | **Complete** | All P0–P3-C delivered (31 items in final sprint) |
| **Sprint 16: Praxis** | **Complete** | 3-tier agent hierarchy UI (tier grouping, agent edit modal, import/export) |
| **Gap Analysis Sprint** | **Complete** | 20 items: delegation UI, doom loop, bento cards, skill CRUD, session tree, memory browser, trusted folders, marketplace, OAuth, plugin wizard |
| **Sprint 21: Frontend ↔ Core-v2** | **Complete** | Settings sync, event hooks, budget sync, chat→AgentExecutor |
| **Sprint 22: Backend Parity** | Backend complete | Frontend wiring mostly done (Sprint 23) |
| **Sprint 23: Frontend Completion** | **Complete** | Launch-ready: DB migration, session bridge, archive/busy/slug UI, structured output, Explorer agent, plugin version tracking, smoke tests + QA checklist |

## Sprint 21 — Frontend ↔ Core-v2 Integration (DONE)

All integration gaps between desktop app and core-v2 closed:
- [x] Plugin SessionManager dedup (shared `getCoreSessionManager()`)
- [x] Bidirectional settings sync (`settings-sync.ts`, loop prevention)
- [x] Extension event bridge hooks (`useExtensionEvent`, `useExtensionEvents`, `useExtensionEventLog`)
- [x] Model status hook (`useModelStatus`)
- [x] Context budget sync (reactive `budgetTick` on `context:compacting`/`agent:finish`)
- [x] Chat → AgentExecutor unification (full middleware chain: permissions, hooks, sandbox, checkpoints)

## Sprint 23 — Frontend Completion (DONE)

Launch-ready sprint closing all remaining desktop↔core-v2 gaps:

- [x] **DB Migration V6** — `parent_session_id`, `slug`, `busy_since` columns (fixes session fork crash bug)
- [x] **DB Migration V7** — `plugin_installs` table for persistent plugin tracking
- [x] **DesktopSessionStorage** — Adapter bridging core-v2 SessionManager with desktop SQLite (`desktop-session-storage.ts`)
- [x] **Session status bridge** — `session:status` events sync busy/idle state to desktop DB via `core-bridge.ts`
- [x] **Archive/Unarchive UI** — Context menu "Archive" option, collapsible archived section in sidebar
- [x] **Busy indicator** — `Loader2` spinner replaces `MessageSquare` icon when agent is executing
- [x] **Slug display** — Human-readable slug shown as subtitle below session name
- [x] **Structured output renderer** — `StructuredOutputView.tsx` — collapsible JSON tree for `__structured_output` tool
- [x] **Explorer agent preset** — Read-only worker with 7 tools (read_file, glob, grep, ls, repo_map, websearch, webfetch), Compass icon
- [x] **B-084 wontfix** — Dual-stack toggle unnecessary (desktop already uses core-v2 exclusively)
- [x] **Plugin version tracking** — `refreshCatalog()` (force bypass), `getPluginVersion()`, `hasUpdate()`, `pluginsWithUpdates`
- [x] **Smoke tests** — 17 automated tests covering migrations, session types, slug, structured output, plugins, agent presets
- [x] **QA checklist** — `docs/qa-checklist.md` with 40+ manual test items

New files: `desktop-session-storage.ts`, `StructuredOutputView.tsx`, `smoke.test.ts`, `qa-checklist.md`

## Sprint 22 Backend Features — Frontend Wiring Status

| Backend Feature | Frontend Work | Status |
|----------------|---------------|--------|
| ~~Session archival + busy state + slug~~ | ~~Archive/unarchive in session list, busy indicator, auto-slug titles~~ | **DONE** (Sprint 23: context menu archive, collapsible archived section, Loader2 spinner, slug subtitle) |
| 6 new LSP tools (documentSymbols, workspaceSymbols, codeActions, rename, refs, completions) | Code viewer context menus, symbol outline panel | OPEN |
| ~~PTY tool (core-v2)~~ | ~~Verify xterm.js terminal uses core-v2 PTY~~ | **DONE** (TauriPTY already wired in platform-tauri) |
| File watcher extension (core-v2) | Wire to existing `file-watcher.ts` or replace with core-v2 version | LOW |
| ~~Structured output tool~~ | ~~Render validated JSON responses differently in chat~~ | **DONE** (Sprint 23: StructuredOutputView.tsx — collapsible JSON tree in tool-call-output) |
| ~~Explore subagent (15th agent)~~ | ~~Add to AgentsTab, delegation UI~~ | **DONE** (Sprint 23: Explorer preset with Compass icon, 7 read-only tools) |
| Prompt caching / model-family prompts | Transparent — no UI needed | — |

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
- [x] Complete Sprint 2.3 plugin UX runtime wiring/validation from `docs/development/sprints/2026-S2.3-plugin-ux-wiring.md` — **DONE** (plugin validation tests, hot reload, permission sandboxing)
- [x] Complete benchmark-derived frontend gaps FG-004 remainder, FG-006, and FG-007 from `docs/development/status/frontend-gap-matrix-2026-02-15.md` — **DONE** (FG-004 adaptive scroll + content-visibility, FG-006 export redaction + metadata, FG-007 verified)
- [x] Wire plugin install/uninstall to real backend lifecycle APIs (Tauri FS + dynamic import)

---

## Phase 2 — Plugin Ecosystem (THE DIFFERENTIATOR)

This is what makes AVA "The Obsidian of AI Coding".

### Sprint 2.1: Plugin Format & SDK
- [x] Define unified plugin manifest (skills + commands + hooks + MCP in one package) — **DONE** (Sprint 10: `ava-extension.json` manifest format)
- [x] Plugin SDK with TypeScript types and helpers — **DONE** (Sprint 10: `docs/plugins/PLUGIN_SDK.md`, `ExtensionAPI` interface, `defineTool()`)
- [x] Plugin lifecycle (install, enable, disable, uninstall, reload) — real FS operations via Tauri FS plugin, dynamic import via Blob URL, state persistence to ~/.ava/plugins/state.json
- [x] Plugin sandboxing (what plugins can/can't access) — **DONE** (PluginPermission type, sandboxed API wrapper, permission confirmation dialog)
**Frontend**: Permission badges, install confirmation for sensitive permissions

### Sprint 2.2: Plugin Development Experience
- [x] `ava plugin init` scaffold command — **DONE** (Sprint 10: generates ExtensionAPI source + manifest + tests)
- [x] Hot reload during plugin development — **DONE** (watchPluginDirectory, reloadPlugin, dev mode toggle per plugin)
- [x] Plugin testing utilities — **DONE** (Sprint 10: `createMockExtensionAPI()` + provider test harness)
- [x] Plugin documentation template — **DONE** (Sprint 10: `docs/plugins/PLUGIN_SDK.md`)
**Frontend**: Plugin dev panel showing reload status, console logs — **DONE**

### Sprint 2.3: Built-in Marketplace UI
- [x] Plugin browser in sidebar/settings surfaces
- [x] Settings-only plugin manager surface (replace Plugins placeholder)
- [x] Search + category-aware filtering in settings manager
- [x] Install/uninstall + enable/disable controls in settings manager
- [x] Plugin detail/settings panel in settings manager
- [x] Metadata/trust/version/changelog fields surfaced in plugin cards/details
- [x] Featured plugin catalog curation + remote source integration — **DONE** (Sprint 10: remote fetch + localStorage cache with 30-min TTL + fallback catalog)
- [x] Wire settings manager actions to real backend extension lifecycle APIs (INT-001/002/003: real FS download, extraction, dynamic import via Blob URL, state persistence)
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
- [x] **Workflow/Recipe Creation** — Save session as reusable workflow. DB table + CRUD service + reactive store. WorkflowDialog from command palette, WorkflowCards in empty chat state. Extracts user messages as prompt. Usage tracking + sort by frequency. — **Large** *(done)*
- [x] **Custom Commands UI** — Settings → Commands tab lists/creates/edits/deletes TOML command files in `~/.config/ava/commands/`. Service layer with inline TOML parser, edit form with name/description/prompt/tools/mode fields, expand to preview prompt. — **Large** *(done)*
- [x] Faster model picker dialog (Ctrl+O, grouped by provider) — **Small** *(done)*
- [x] Conversation branching (fork at any message) — **Medium** *(done — moved to P1)*
- [x] Prompt library / starter templates — **Medium** *(done)*
- [x] **Voice Dictation Input** — Web Speech API wrapper with continuous dictation. MicButton in toolbar strip (red pulse when recording, hidden if unsupported). Auto-restart on browser timeout, error mapping, cleanup on unmount. — **Medium** *(done)*
- [x] Panel adaptability (draggable/persisted split ratios) — **Medium** *(done)*

### Legacy Gaps (Still Open)

- ~~**FG-004 (partial):** long-session render-window/backfill hardening for very large histories.~~ **DONE** — adaptive visibleLimit, scroll-up backfill, content-visibility CSS.
- ~~**INT-001/INT-002/INT-003:** plugin lifecycle runtime validation and failure-path evidence.~~ **DONE** — real FS download, Blob import, state persistence.
- **Manual QA:** Linux DE matrix and light-mode regression pass.

> All P0–P3-C competitive gap items are now delivered. Only manual QA and Sprint 2.4 (plugin registry API) remain.

---

## P3 — Remaining Competitive Gaps (7-Tool Audit, 2026-02-28)

Comprehensive audit of Goose, OpenCode, Cline, Gemini CLI, Aider, OpenHands, and Plandex.
Features below are things **competitors ship that AVA does not yet have**.

### P3-A — High Value (real user-facing gaps)

- [x] **MCP Marketplace UI** — AddMCPServerDialog with preset browser (12 curated servers) + manual config (transport, command, args, URL, env, trust). Wired into SettingsModal. — **Large** *(done)*
- [x] **Checkpoint / Snapshot Restore** — CheckpointDialog from command palette (Ctrl+Shift+C). Saves named checkpoint via `createCheckpoint()`. Existing checkpoint badges in MessageRow support restore. — **Medium** *(done)*
- [x] **Rewind Conversation** — Undo2 button on non-last messages. Rewind dialog with two options: "Rewind conversation only" (truncate) and "Rewind and revert files" (truncate + restore original file content). — **Medium** *(done)*
- [x] **Focus Chain / Task Progress UI** — FocusChainBar above message list. Reactive store bridges todowrite/todoread events to SolidJS signals. Progress bar + "Task N/Total" + expandable checklist. — **Medium** *(done)*
- [x] **Prompt History Navigation** — ArrowUp/ArrowDown in empty input cycles through past user messages. Saves draft on enter, restores on exit. Resets on manual typing. — **Small** *(done)*
- [x] **Message Edit & Resubmit** — Pencil button on user messages → EditForm → `editAndResend()` drops messages after + regenerates. — **Medium** *(already implemented)*
- [x] **Thinking/Reasoning Toggle** — ThinkingBlock collapsible per-message. `ThinkingToggle` in toolbar. Global `ui.hideThinking` toggle (Eye/EyeOff button) hides all thinking blocks. — **Small** *(already implemented, global hide added)*
- [x] **Granular Auto-Approve Rules** — Permissions settings tab with global mode selector, per-tool rules table (allow/ask/deny with globs), always-approved tools list. Synced to core via `pushSettingsToCore`. — **Medium** *(done)*
- [x] **Conversation Limit / Manual Compact** — Configurable `compactionThreshold` (50-95%) in Behavior settings. "Compact" button appears in toolbar when threshold exceeded. — **Small** *(done)*
- [x] **Prompt Stash / Drafts** — Ctrl+Shift+S stashes input, Ctrl+Shift+R restores. localStorage-based (max 20). Badge indicator in toolbar strip when stash non-empty. — **Small** *(done)*

### P3-B — Medium Value (nice-to-have, polish)

- [x] **Extension Registry (Install from Git)** — Install from GitHub URL, update, link local, uninstall. Per-scope enable/disable (global/project). Git source badges + detail view in PluginsTab. — **Large** *(done)*
- [x] **Plan Sandbox (Apply/Reject)** — Sandbox mode toggle intercepts file writes. SandboxReviewDialog with per-file accept/reject, diff view. Apply Selected / Apply All / Reject All. — **Large** *(done)*
- [x] **Subagent Status UI** — SubagentCard.tsx replaces generic ToolCallCard for `task` tool. Shows agent goal, status badge (running/completed/failed), elapsed time, nested tool calls timeline. — **Medium** *(done)*
- [x] **Workflow Scheduling (Cron)** — CronPickerDialog with presets + custom builder. workflow-scheduler.ts with parseCron/getNextRun/startScheduler. Schedule/unschedule methods in workflow store. — **Large** *(done)*
- [x] **Workflow Import/Export** — Export single/all workflows as JSON, import from file picker. Buttons in WorkflowCards header + command palette entries. — **Medium** *(done)*
- [ ] **Session Sharing (URL)** — Generate shareable read-only link for a session. — **Medium** *(source: Goose, OpenCode)*
- [x] **Watch Mode for AI Comments** — file-watcher.ts scans for `// AI!`, `// AI?` patterns in 23 file types. Toggle in BehaviorTab settings. — **Medium** *(verified existing)*
- [x] **Session Save/Resume Checkpoints** — CheckpointDialog + createCheckpoint/rollbackToCheckpoint in session store. Checkpoint badges in MessageRow. — **Medium** *(verified existing)*
- [x] **Auto-Updater** — auto-updater.ts service + UpdateDialog. Check on startup + "Check for Updates" command. @tauri-apps/plugin-updater integration. — **Medium** *(done)*
- [x] **Local Model Download UI (Ollama)** — OllamaModelBrowser.tsx: list/pull/delete models via Ollama API. Model details (size, family, quantization). — **Medium** *(done)*
- [x] **Stats / Usage Dashboard** — UsageDetailsDialog enhanced with Session/Project tabs. Project tab shows aggregated stats, model breakdown table, daily usage bar chart. DB queries for project-level aggregation. — **Small** *(done)*
- [x] **Code Block Collapse Toggle** — Already implemented (CSS in index.css, JS in MarkdownContent.tsx). — **Small** *(already done)*
- [x] **In-App Changelog / Announcements** — ChangelogDialog with version check + "What's New" command palette entry. Auto-shows on first launch after update. — **Small** *(done)*
- [x] **External Editor for Prompt** — openInExternalEditor() in ide-integration.ts. Invokes $EDITOR/$VISUAL with temp file. Button in MessageInput strip. — **Small** *(done)*
- [x] **Copy-Paste Mode** — clipboard-watcher.ts polls navigator.clipboard.readText(). Toggle in BehaviorTab. Code detection + toast notification in ChatView. — **Small** *(done)*
- [x] **Model Aliases / Packs** — modelAliases in settings. Alias → model ID table with add/remove in BehaviorTab. — **Small** *(done)*

### P3-C — Low Value (niche / cosmetic)

- [x] MCP rich UI rendering — MCPResourceRenderer.tsx renders table/form/chart/image/markdown widgets. Integrated into tool-call-output.tsx. — **Large** *(done)*
- [x] Deep link protocol — deep-link.ts with ava:// URL parsing. Routes: settings/<tab>, session/<id>, workflow/<id>. @tauri-apps/plugin-deep-link. — **Medium** *(done)*
- [x] More theme presets — 14 named themes (catppuccin-mocha/latte, dracula, nord, gruvbox-dark/light, solarized-dark/light, tokyo-night, rose-pine, one-dark, github-dark, moonlight, everforest). Gallery in AppearanceTab. — **Medium** *(done)*
- [x] Sidebar layout customization — sidebarOrder in UISettings. Reorder with up/down arrows in AppearanceTab. ActivityBar reads from settings. — **Small** *(done)*
- [x] ~~Interruption handler (pause/redirect)~~ — **Removed** (Gap Analysis Sprint: adds complexity without value). Pause/resume UI, signals, and store methods deleted.
- [x] Working directory switcher — CWD button in StatusBar with Tauri dialog.open. setCurrentDirectory in project store. — **Small** *(done)*
- [x] Waveform visualizer for voice — AudioAnalyser + AnalyserNode in voice-dictation.ts. 8 animated bars near MicButton. — **Tiny** *(done)*
- [ ] Browser automation UI — Screenshot display, action history, session tracking. — **Medium** *(out of scope: browser tool removed)*
- [x] Voice device selection — getAudioDevices() in voice-dictation.ts. Device picker dropdown in MessageInput strip. voiceDeviceId in settings. — **Tiny** *(done)*
- [x] Read-only file context — readOnlyFiles signal in session store. Lock icon + context menu in SidebarExplorer. — **Small** *(done)*
- [x] Plan branch management — plan-branches.ts store + PlanBranchSelector.tsx. Create/switch/compare/merge/delete branches. Inline in plan mode strip. — **Large** *(done)*
- [x] Background plan execution — backgroundPlanActive/progress signals in session store. "Run in Background" button + StatusBar indicator. — **Medium** *(done)*
- [x] Microagent management UI — MicroagentsTab.tsx in settings. 8 built-in skills with toggle enable/disable. Wired into SettingsModal. — **Medium** *(done)*
- [ ] Enterprise integrations UI — GitHub/Slack/Jira/Linear integration panels. — **Large** *(enterprise scope, deferred)*

### What competitors have that AVA already matches

| Their feature | Source | AVA equivalent |
|---------------|--------|----------------|
| Recipe/workflow creation | Goose | Workflow creation from session (DB + dialog + cards) |
| Voice dictation | Aider, Goose | Web Speech API + MicButton in toolbar strip |
| Custom commands | Gemini CLI, Goose | Custom commands UI (Settings tab + TOML CRUD) |
| Hooks system (pre/post tool) | Cline, Gemini CLI | Hook system in extensions (`addToolMiddleware`) |
| Skills system | Cline, Gemini CLI | Skills extension (auto-invoked by context) |
| Architect mode (2-model) | Aider | Architect/editor model split |
| Repo map with PageRank | Aider | Codebase extension with PageRank + dependency graph |
| Plan mode | Gemini CLI, Plandex | Plan mode slider in toolbar |
| Memory system | Gemini CLI | Memory recall (episodic + semantic + procedural + RAG) |
| Inline tool approval | Cline, Goose | ApprovalDock with Enter/Escape + always-allow |
| File mention autocomplete | Cline | @ mention popover with fuzzy matching |
| Message queue / steering | Goose | MessageQueueBar with expand/remove |
| Cost tracking | Cline, Goose | Per-message + session aggregate cost |
| Conversation branching | Goose | Fork at any message via Branch button |
| Session switcher | Goose, OpenCode | Quick session switcher (Ctrl+J) |
| Expanded editor | OpenCode | Expanded editor modal (Ctrl+E) |
| Conversation search | OpenCode | Full-text search with highlighting |
| Export to Markdown | Goose | Command palette + Ctrl+Shift+E |
| File changes panel | Cline, Goose | Right panel "Files" tab |
| Diff review panel | Cline, OpenHands, Plandex | Aggregate diff review with DiffViewer |
| Integrated terminal | OpenHands | xterm.js + Rust PTY in bottom panel |
| IDE integration ("Open in") | Cline, Gemini CLI | Auto-detect 8 editors, context menu |
| Live tool streaming | Cline | Bash stdout streams in ToolCallCard |
| Undo/redo file changes | Goose | Per-session version stacks + shortcuts |
| Plugin/extension manager | Gemini CLI | Settings tab with search, install, detail |
| Multi-agent hierarchy | OpenHands | Praxis 3-tier: Commander → Leads → Workers (13 agents) |
| Per-agent model routing | — | Each agent configurable with different model/provider |
| Agent import/export | — | Share custom agents as JSON files |
| Subagent spawning (backend) | Cline | Task tool + Praxis delegation chain |
| 16 LLM providers | all | 16 providers with API key/OAuth management |
| Theme live preview | Goose | Hover to preview without persisting |
| Starter templates | Goose | 4 template cards in empty chat |
| Context warning | Cline, OpenCode | Yellow badge at 80% |
| Auto-compaction notification | OpenCode | Toast on compaction |
| Model picker | all | Ctrl+O grouped by provider |
| UI density settings | — | 3 levels, 8 components wired |
| Keyboard shortcuts | Gemini CLI | Configurable keybindings tab |
| Settings export/import | Goose | JSON download + upload |
| LSP diagnostics | — | Error/warning counts in status bar |
| File tree change indicators | Cline | Color-coded dots on modified files |
| Git auto-commit | Aider, Plandex | Auto-commit on AI file edits |
| Permission modes | Cline, Gemini CLI | Ask/Auto/Bypass badge in toolbar |
| Notification sounds | Aider | Desktop notifications + AudioContext chime |
| Sandbox execution (backend) | OpenHands, Gemini CLI | Sandbox extension (Docker) |
| TODO tracking | Cline, OpenHands | todoread/todowrite tools (backend) |
| Progressive rendering | — | Render window for long sessions |
| Onboarding wizard | Cline | OnboardingScreen (theme, mode, API keys) |
| Context token tracking | Cline, Plandex | ContextBar with token counts |
| Web scraping | Aider | webfetch + browser tools |

### Sprint 2.4: Plugin Distribution
- [x] Publish flow stub (PublishDialog.tsx) — **DONE** (Gap Analysis Sprint)
- [x] Plugin creation wizard (PluginWizard.tsx — 4 templates) — **DONE** (Gap Analysis Sprint)
- [x] Marketplace sort (popular/rated/recent/name) + download/rating display — **DONE** (Gap Analysis Sprint)
- [x] Plugin hot reload (reloadPlugin in extension-loader) — **DONE** (Gap Analysis Sprint)
- [x] Local version tracking + update detection — **DONE** (Sprint 23: `plugin_installs` DB table, `hasUpdate()`, `pluginsWithUpdates`, `refreshCatalog()`)
- [ ] Plugin registry API (backend) — real remote publishing
- [ ] Community ratings backend
**Frontend**: Publish flow, wizard, sort, ratings, version tracking all shipped. Remote registry API still needed.

### Sprint 2.5: Starter Plugins
- [x] 5-10 built-in plugins demonstrating the system — **DONE** (Sprint 10: 5 example plugins with tests in `docs/examples/plugins/`)
- [x] Example: timestamp-tool (registerTool + Zod schema)
- [x] Example: file-stats (registerTool + platform.fs)
- [x] Example: polite-middleware (addToolMiddleware + priority)
- [x] Example: session-notes (registerCommand + storage API)
- [x] Example: event-logger (api.on + emit + events + storage)
- [x] Example: "React Patterns" skill plugin — **DONE** (triggers on .tsx/.jsx, provides component/hooks/state/performance guidance)
- [x] Example: "/deploy" command plugin — **DONE** (/deploy with staging/production/preview targets + --dry-run flag)
**Frontend**: Plugin showcase page

---

## Gap Analysis Sprint (2026-02-28) — All 20 Items

| # | Item | Status | Batch |
|---|------|--------|-------|
| NEW-1 | Remove pause/redirect functionality | **Done** | 1 |
| NEW-3 | Add 'thinking' capability to Codex models | **Done** | 1 |
| H3 | Tauri bridge for core-v2 | **Verified** | 1 |
| M3 | Session transcript export (FG-006) | **Verified** | 1 |
| M5 | Smart tool call grouping | **Verified** | 1 |
| L2 | Geist Sans font | **Verified** | 1 |
| NEW-2 | Wire delegation events to chat UI | **Done** | 2 |
| M4 | Doom loop warning banner | **Done** | 3 |
| L1 | Settings bento cards | **Done** | 4 |
| M2 | Custom skill CRUD in MicroagentsTab | **Done** | 5 |
| H2 | Plugin lifecycle runtime tests | **Done** | 5 |
| M1 | Visual session branch tree | **Done** | 6 |
| M6 | Persistent cross-session memory browser | **Done** | 7 |
| L4 | Trusted folder boundaries | **Done** | 7 |
| H1 | Plugin marketplace full UX (sort, ratings, downloads) | **Done** | 8 |
| L5 | Plugin hot reload enhancement (reloadPlugin) | **Done** | 8 |
| M7 | MCP OAuth flows | **Done** | 9 |
| L3 | Plugin creation wizard (4 templates) | **Done** | 9 |
| L6 | Documentation website | Deferred | — |
| L7 | A2A agent network UI | Deferred | — |

New files created:
- `src/components/chat/DoomLoopBanner.tsx`
- `src/components/settings/SettingsCard.tsx`
- `src/components/sidebar/SessionBranchTree.tsx`
- `src/components/panels/MemoryBrowserPanel.tsx`
- `src/components/settings/tabs/TrustedFoldersTab.tsx`
- `src/components/plugins/PublishDialog.tsx`
- `src/components/plugins/PluginWizard.tsx`
- `src/components/dialogs/MCPOAuthDialog.tsx`
- `src/services/mcp-oauth.ts`

---

## Phase 3+ — Longer Term

| Feature | Effort | Frontend Impact | Source |
|---------|--------|----------------|--------|
| ~~MCP marketplace UI~~ | ~~2-3 weeks~~ | ~~Browse, install, manage MCP servers~~ | **DONE (P3-A)** |
| ~~Checkpoint / rewind system~~ | ~~2 weeks~~ | ~~Named snapshots, restore points, rewind UI~~ | **DONE (P3-A)** |
| ~~Granular auto-approve rules~~ | ~~1-2 weeks~~ | ~~Per-tool policy editor in settings~~ | **DONE (P3-A)** |
| ~~Focus chain / task progress UI~~ | ~~1 week~~ | ~~Visual todo progress bar in chat header~~ | **DONE (P3-A)** |
| ~~Extension install from Git~~ | ~~2 weeks~~ | ~~Install/link/update extensions from repos~~ | **DONE** |
| ~~Plan sandbox (apply/reject)~~ | ~~2-3 weeks~~ | ~~Staged changes review before applying~~ | **DONE** |
| Sandbox / container execution | 2-3 weeks | Toggle in settings, status indicator | OpenHands, Gemini CLI |
| ~~Auto-updater~~ | ~~1 week~~ | ~~Settings section + Tauri updater plugin~~ | **DONE** |
| Tree-sitter for 100+ languages | 2 weeks | Better code highlighting, symbol extraction | Plandex |
| CLI polish | 1-2 weeks | None (CLI-only) | — |
| ACP editor integration | 2 weeks | Minimal (backend protocol) | — |
| A2A agent network | 2 weeks | Agent discovery UI, remote agent cards | — |
| ~~Deep link protocol (ava://)~~ | ~~1-2 weeks~~ | ~~Extension/workflow install from URLs~~ | **DONE** |
| ~~MCP rich UI rendering~~ | ~~2-3 weeks~~ | ~~Render interactive widgets from MCP tools~~ | **DONE** |

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
| Custom commands UI | 70+ | Settings tab, TOML CRUD, edit form, prompt preview |
| Voice dictation input | 70+ | Web Speech API, MicButton in toolbar strip, continuous dictation |
| Workflow/recipe creation | 70+ | DB table, CRUD, WorkflowDialog, workflow cards in empty chat |
| Bidirectional settings sync | Sprint 21 | core-v2 SettingsManager ↔ frontend via CustomEvent bridge |
| Extension event bridge hooks | Sprint 21 | `useExtensionEvent`, `useExtensionEvents`, `useExtensionEventLog` |
| Model status hook | Sprint 21 | `useModelStatus` — reactive modelCount, lastUpdate, refresh |
| Context budget sync | Sprint 21 | Reactive budgetTick on context:compacting + agent:finish events |
| Chat → AgentExecutor unification | Sprint 21 | Full middleware chain (permissions, hooks, sandbox, checkpoints) |

---

## Unique Advantages (AVA vs Everyone)

Features no other AI coding tool has:

| Feature | Status |
|---------|--------|
| **Praxis 3-tier agent hierarchy** (Commander → Leads → Workers) | Built (Sprint 16), visible in UI |
| Per-agent model/provider (each agent can use different LLM) | Built (Sprint 16) |
| Planning pipeline (Planner → Architect → Lead delegation) | Built (Sprint 16) |
| Agent import/export (share custom agents as JSON) | Built (Sprint 16) |
| 15 built-in specialized agents with tier-based delegation | Built (Sprint 16+22) |
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
| Plugin marketplace | Built (UI complete, registry API pending) |
| Protocol support (ACP + A2A) | Built (backend) |
