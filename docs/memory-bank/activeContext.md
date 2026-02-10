# Active Context

> What we're working on RIGHT NOW

---

## Current Focus

**Session 54 — Planning & Audit: Testing, Debug Logging, PI Parity**

Phase 1.5 remains feature-complete. Focus is now a full audit, test planning (OAuth + message flow), debug logging coverage, and PI Coding Agent parity planning before Phase 2.

### What Just Happened (2026-02-10)

**Audit + Planning (Session 54):**
- Ran full codebase audit (LOC, tests, typecheck, Biome, TODO/FIXME/HACK, console.log scan, git status, file length violations)
- Compared reference codebases and identified new gaps (PI parity items, MCP OAuth, remote browser, minimal mode)
- Drafted OAuth and message flow test plans
- Audited logging gaps in chat/agent/core/session/settings/file-watcher/ChatView
- Planned Sprint 1.6: Testing & Debug; created ticket list

**Debug Logging Pass (Session 55):**
- Added structured logging in `useChat` (send/queue/steer/cancel/stream errors)
- Added structured logging in core bridge init/dispose
- Added structured logging in file watcher start/stop/dedup
- Replaced console warnings in settings/session store with file logger
- Logged tool approval resolution in ChatView

### What Just Happened (2026-02-09)

**File Watcher + Step-Level Undo (Session 53):**
- **File watcher service** — `src/services/file-watcher.ts` (~200 lines) watches project directory via Tauri FS plugin
- **6 AI patterns** — `// AI!`, `// AI?`, `# AI!`, `# AI?`, `-- AI!`, `-- AI?` for multi-language support
- **Smart filtering** — 30+ scannable extensions, IGNORE_DIRS (node_modules, .git, dist, etc.)
- **Dedup tracking** — processedHashes Set prevents re-triggering same comment
- **Settings toggle** — `fileWatcher: boolean` in BehaviorSettings, toggle in Behavior tab
- **ChatView wiring** — `createEffect` starts/stops watcher based on settings + project dir; `onComment` auto-sends as chat message
- **FS permissions** — `fs:allow-watch` and `fs:allow-unwatch` added to Tauri capabilities
- **Undo button** — Undo2 icon in MessageInput toolbar, calls `chat.undoLastEdit()` (git revert), 2.5s status feedback
- **Undo visibility** — Only shows when git auto-commit is enabled in settings
- **1 new file** — `src/services/file-watcher.ts`
- **Streaming tool preview** — Confirmed already implemented (reactive chain: onToolUpdate → session.updateMessage → ToolCallGroup/ToolCallCard)
- **Gap scorecard** — 12/15 gaps DONE. Phase 2 roadmap fully complete. Only sandbox, tree-sitter, RPC, telemetry remain (Phase 3+)
- **1 new file** — `src/services/file-watcher.ts`
- **4 modified files** — `ChatView.tsx`, `BehaviorTab.tsx`, `MessageInput.tsx`, `settings.ts`
- 0 TS errors, 0 biome errors, vite build passes

**Message Queue / Steering Interrupts (Session 52):**
- **Message queue** — `useChat` queues follow-up messages when streaming; auto-dequeues after completion
- **Steer function** — `steer()` cancels current stream and sends new message immediately
- **Type-ahead** — Textarea stays enabled during chat streaming so user can type ahead
- **Queue badge** — Shows queued message count in toolbar
- **Keyboard shortcut** — `Ctrl+Shift+Enter` triggers steer (cancel + send immediately)
- **Cancel clears queue** — Stop button cancels stream AND clears queued messages
- **Session switch clears queue** — `createEffect` watching session ID
- **Send/Queue button** — Changes style during streaming to indicate queue mode
- **2 modified files** — `useChat.ts`, `MessageInput.tsx`
- 0 TS errors, 0 biome errors, vite build passes

**OAuth Fix + Error Logging (Session 51):**
- **Root cause found** — OpenAI OAuth tokens were stored as plain API keys via `syncProviderCredentials()`. Core's OpenAI provider checked `auth.type` and saw `'api-key'` instead of `'oauth'`, routing requests to `api.openai.com` instead of the ChatGPT Codex endpoint. Error: "insufficient permissions for this operation"
- **Fix** — `storeOAuthCredentials()` now routes by provider: Anthropic stores minted API key (correct), OpenAI/Copilot store via core's `setStoredAuth()` as `type: 'oauth'` with `accountId` extracted from JWT `id_token`
- **Scopes** — Reverted incorrect `model.request` scope from OpenAI config
- **CSP** — Added `https://chatgpt.com` for Codex API endpoint
- **OAuth disconnect UI** — "Connected via OAuth" badge + disconnect button in ProvidersTab
- **Error logging** — Full structured logging across OAuth flow (start, PKCE, browser, callback, token exchange, storage, errors)
- **Browser opener** — Fixed `@tauri-apps/plugin-shell` → `@tauri-apps/plugin-opener` import
- **Files modified** — `oauth.ts`, `ProvidersTab.tsx`, `tauri.conf.json`

**Architect + Editor Model Split (Session 50):**
- **Core config** — `editorModel` + `editorModelProvider` optional fields on `ProviderSettings`
- **Helper** — `getEditorModelConfig()` in `llm/client.ts` reads settings, infers provider from model name
- **Commander wired** — `commander/executor.ts` auto-applies editor model to workers when no per-worker override
- **Frontend** — `editorModel` field in `GenerationSettings`, dropdown in LLMTab with 8 editor model presets
- **Auto-pair** — Button suggests editor model based on primary (e.g., Opus → Sonnet, Sonnet → Haiku)
- **Settings sync** — `pushSettingsToCore()` bridges `editorModel` to core `ProviderSettings`
- **6 modified files** — `config/types.ts`, `config/schema.ts`, `llm/client.ts`, `llm/index.ts`, `commander/executor.ts`, `settings.ts`, `LLMTab.tsx`
- 0 TS errors, 0 biome errors, vite build passes

**Weak Model for Secondary Tasks (Session 49):**
- **Core config** — `weakModel` + `weakModelProvider` optional fields on `ProviderSettings`
- **Helper** — `getWeakModelConfig()` in `llm/client.ts` reads settings, infers provider from model name prefix
- **Planner wired** — `agent/planner.ts` uses `getWeakModelConfig()` instead of hardcoded `claude-sonnet-4-20250514`
- **Self-review wired** — `validator/self-review.ts` uses weak model for code review (secondary task)
- **Frontend** — `weakModel` field in `GenerationSettings`, dropdown in LLMTab with 9 model presets
- **Auto-pair** — Button suggests cheap model based on active primary model (e.g., Sonnet → Haiku)
- **Settings sync** — `pushSettingsToCore()` bridges `weakModel` to core `ProviderSettings`
- **6 modified files** — `config/types.ts`, `config/schema.ts`, `llm/client.ts`, `llm/index.ts`, `agent/planner.ts`, `validator/self-review.ts`, `settings.ts`, `LLMTab.tsx`
- 0 TS errors, 0 biome errors, vite build passes

**Git Auto-Commit (Session 48):**
- **Auto-commit module** — `packages/core/src/git/auto-commit.ts` stages + commits after file-modifying tools
- **Tool registry wiring** — PostToolUse in `registry.ts` calls `autoCommitIfEnabled()` for write locations
- **Undo action** — `undoLastAutoCommit()` reverts the most recent estela-prefixed commit
- **Frontend settings** — `GitSettings` interface (enabled, autoCommit, commitPrefix) with BehaviorTab UI
- **Settings sync** — `pushSettingsToCore()` bridges frontend git settings to core `SettingsManager`
- **1 new file** — `packages/core/src/git/auto-commit.ts`
- **6 modified files** — `config/types.ts`, `tools/registry.ts`, `git/index.ts`, `settings.ts`, `BehaviorTab.tsx`, `useChat.ts`
- 0 TS errors, 0 biome errors, vite build passes

**Previous sessions:** Backend gaps (paste collapse, tool approval, MCP, FS scope, shell timeout, OAuth), docs reorg, settings hardening, gap closure (cost tracking, vision, lint→fix, checkpoints), appearance expansion, density wiring, 706 backend tests (1778 total)

---

## Next Up

### Phase 1.5 Remaining (testing only)
- [x] ~~Memory recall in system prompts~~ — Done (recallSimilar + procedural → system message)
- [x] ~~Auto-compaction at 80% context~~ — Done (sliding window, syncs state + DB + tracker)
- [ ] Test full app flow in Tauri dev (chat, tools, settings, sessions)
- [ ] Verify all keyboard shortcuts work (Ctrl+B, Ctrl+,, Ctrl+M)
- [ ] Test on multiple Linux DEs (GNOME, KDE, Cosmic)

### Sprint 1.6: Testing & Debug (Planned)
- OAuth unit tests + integration tests
- Message flow unit + integration tests
- Debug logging coverage (chat/agent/core/session/settings/file-watcher)
- PI Coding Agent parity items
- Manual Tauri OAuth test checklist

### Session 54 Planning Output
Ready to implement tomorrow (priority order):
- OAuth test suite (unit + integration)
- Message flow test suite (unit + integration)
- Debug logging coverage pass
- PI Coding Agent parity items
- Manual Tauri OAuth testing
Estimated sessions:
- OAuth tests: 2 sessions
- Message flow tests: 2 sessions
- Debug logging: 1 session
- PI parity: 2 sessions
- Manual Tauri test sweep: 1 session
Dependencies:
- OAuth integration tests depend on stable credential storage keys
- Message flow integration tests depend on chat stream mock harness
- Debug logging should land before manual test sweep for better diagnostics
Most important first:
- OAuth test suite (prevents regression of OAuth credential routing)

### Phase 2: Plugin Ecosystem (THE DIFFERENTIATOR)
See `docs/ROADMAP.md` for sprint breakdown.

---

## Platform Priority

```
1. Desktop App (Tauri)     <- CURRENT (polish/testing)
2. Plugin Ecosystem        <- NEXT
3. CLI                     <- Secondary
4. Editor Integration      <- Future
5. Agent Network           <- Future
```

---

## Key File Map (Quick Reference)

### Layout System
| File | Purpose |
|------|---------|
| `src/stores/layout.ts` | All layout state (sidebar, panels, settings modal, shortcuts) |
| `src/components/layout/AppShell.tsx` | 3-column layout with @corvu/resizable |
| `src/components/layout/ActivityBar.tsx` | Left icon strip (sessions, explorer, settings) |
| `src/components/layout/SidebarPanel.tsx` | Contextual sidebar (sessions or explorer) |
| `src/components/layout/MainArea.tsx` | Chat view or welcome state |

### Splash / Init
| File | Purpose |
|------|---------|
| `src/components/SplashScreen.tsx` | Splash screen component |
| `src/App.tsx` | Init sequence, splash orchestration, window show |
| `src/index.tsx` | Suspense fallback (matches splash look) |
| `index.html` | Dark background on `#root` to prevent flash |

### Settings
| File | Purpose |
|------|---------|
| `src/components/settings/SettingsModal.tsx` | Settings modal (providers, agents, MCP, shortcuts, appearance, LLM, behavior) |
| `src/components/settings/tabs/LLMTab.tsx` | Generation params, agent limits, custom instructions |
| `src/components/settings/tabs/BehaviorTab.tsx` | Send key, auto-scroll, code blocks, notifications |
| `src/components/settings/tabs/AppearanceTab.tsx` | Theme, accents, fonts, density, code themes |
| `src/stores/settings.ts` | Settings persistence + credential sync + `pushSettingsToCore()` |
| `src/services/notifications.ts` | Desktop notifications + AudioContext chime |

### Core Bridge
| File | Purpose |
|------|---------|
| `src/services/core-bridge.ts` | Init all 5 core singletons (settings, tracker, registry, memory) |
| `src/components/chat/ContextBar.tsx` | Token usage bar below chat input |

### Core Stores
| File | Purpose |
|------|---------|
| `src/stores/session.ts` | Session CRUD, fork, duplicate, checkpoints, tracker-backed contextUsage |
| `src/stores/project.ts` | Project management |
| `src/stores/team.ts` | Dev team hierarchy state |

---

## Naming Convention

| Old Name | New Name | Role |
|----------|----------|------|
| Commander | Team Lead | Plans, delegates, coordinates |
| Worker | Senior Lead | Domain specialist, leads a group |
| Operator | Junior Dev | Executes specific tasks |
