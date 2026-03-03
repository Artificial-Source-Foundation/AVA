# Backend Changelog

> Development history of `packages/core/`. Most recent first.

---

## 2026-03-02

### Sprint 23 — Complete Backend + Differentiation (15 Items, 5 Phases)

**~460 new tests across 30+ new files** in the new architecture stack. Tool count 50+ → 55+. Extensions 30+ → 34+. Providers 15 → 16 (Azure). Total: ~5,350+ tests across ~340+ files. `pnpm build:all` + `npx tsc --noEmit` + `npm run lint` all clean.

**Phase 1: Session DAG (CG-06)**
- `parentSessionId`, `branchName`, `branchPoint`, `children` on SessionState/SessionMeta. `fork()`, `getTree()`, `getBranches()` on SessionManager. `dag.ts` — `getAncestors()`, `getDescendants()`, `flattenTree()`, `findRoot()`, `getDepth()`. SQLite migration for new columns. 26 tests.

**Phase 2: Quick Wins (4 parallel)**
- **B-033 Undo Message Removal**: `diff:undo` removes assistant message, `diff:redo` restores it. `removedMessages` map keyed by sessionId:messageIndex. 5 new tests.
- **B-063 Token-Efficient Results**: `efficient-results.ts` — `normalizeWhitespace()`, `stripAnsi()`, `smartSummarize()`, `groupGrepResults()`, `summarizeLsOutput()`, `efficientToolResult()` dispatcher. Applied in agent loop before building ToolResultBlock. 33 tests.
- **B-053 Azure OpenAI Provider**: `providers/azure/` — Custom client with `api-key` header and deployment endpoint format. Reuses shared `ToolCallBuffer`, `readSSEStream`. `'azure'` added to `LLMProvider` union. 3 tests.
- **B-095 Ambient Terminal**: `cli/src/ambient/install.ts` — Shell function for `@ava "goal"` one-shot. Bash/zsh/fish support. `ava ambient install/uninstall` CLI commands. 13 tests.
- **B-080 Tauri Bridge Full**: `TauriPTY` via `@tauri-apps/plugin-shell`, `TauriFileWatcher` via `@tauri-apps/plugin-fs`. Wired into `createTauriPlatform()`.
- **B-084 Dual-Stack Toggle**: `agentBackend: 'core' | 'core-v2'` in settings (default core-v2). `ava run --backend` CLI command.
- **B-096 Dev Team Delegation UI**: `WorkerDetail.tsx`, `DelegationLog.tsx`, `TeamMetrics.tsx` components. `DelegationEvent` type, `delegationLog` signal, `teamTokenUsage`/`teamFilesChanged` memos.

**Phase 3: Large Features (4 parallel)**
- **B-091 Client-Server API**: `server/` extension — `node:http` server with REST routes (POST /run, GET /stream, POST /steer, GET /status, DELETE, GET /health). Token auth, SSE streaming, session router. ACP stub implemented. 25 tests.
- **B-090 Plugin Marketplace**: `ReviewStore` with submit/get/average/delete. `sortCatalog()`, `filterCatalog()`. `PluginCard.tsx`, `ReviewModal.tsx` UI. 23 tests.
- **B-092 Recipe/Workflow**: `recipes/` extension — JSON/YAML recipes with `{{param}}` and `{{steps.X.result}}` substitution. Sequential + parallel steps, conditions. `.ava/recipes/` auto-discovery. 41 tests.
- **B-094 Chat Recall (FTS5)**: `recall/` extension — FTS5 indexer, BM25 search, cross-branch search via `getAncestors()`. `recall` tool + `/recall` command. 20 tests.

**Phase 4: GitHub Bot (B-093)**
- `github-bot/` extension — Webhook handler with HMAC-SHA256 verification. @ava mention extraction, user/repo ACL. PR/issue context building. Result posting as markdown comments. 29 tests.

**Phase 5: Cleanup + CLI Hardening**
- Activation smoke test updated to 31 extensions (was 27). Extensions tsconfig updated. ACP stub delegates to server SessionRouter. Docs updated.
- `ava tool list` now loads extensions (was core-only, showing 6 tools; now shows 43).
- `ava run` unified command wired with full extension loading: platform, core tools, 31 extensions, instructions, system prompt, session lifecycle, abort handling, `manager.dispose()`. Mock provider auto-selects when `--mock` used.
- CLI smoke tested end-to-end: tool list, tool execution (read/glob/grep/ls/repo_map/todoread), agent-v2 mock + real LLM (OpenRouter), ava run mock + real LLM, validation pipeline, plugin scaffold, auth status. All passing.

---

### Sprint 22 — Full Competitive Parity (35 Items, 5 Phases)

**~337 new tests across 25+ test files** in the new architecture stack. Tool count 44 → 50+. Extensions 28 → 30+. Agents 14 → 15. Total: ~4,859 tests across ~310 files. `pnpm build:all` + `npx tsc --noEmit` + `npm run lint` all clean.

**Phase 1: Plugin Hook System (item 23)**
- `registerHook`/`callHook` on `ExtensionAPI` — sequential chaining pipeline. Types: `HookName`, `HookHandler<TInput, TOutput>`, `HookResult<TOutput>`. Global `callHook()` + `hookHandlers` Map in api.ts, `resetRegistries()` clears hooks. 5 tests.

**Phase 2: Parallel Block A (4 agents)**
- **Prompt Caching (item 1)**: `addCacheControlMarkers()` in `anthropic/src/cache.ts` + `openrouter/src/cache.ts` — marks system block + last 2 user messages with `cache_control: { type: 'ephemeral' }`. `anthropic-beta: prompt-caching-2024-07-31` header. `transformMessages`/`transformRequestBody` hooks in `openai-compat.ts`. 27 tests.
- **LSP 6 Operations (item 2)**: 4 new client methods (`documentSymbols`, `workspaceSymbols`, `codeActions`, `rename`). 6 new tools extracted to `lsp/src/tools.ts` (9 total). New types: `LSPDocumentSymbol`, `LSPWorkspaceSymbol`, `LSPCodeAction`, etc. 62 tests.
- **Model-Family Prompts (item 5)**: `families.ts` — `detectModelFamily()`, `FAMILY_PROMPT_SECTIONS` for Claude/GPT/Gemini/Llama. Builder replaced inline conditionals with family-based approach. 28 tests.
- **Session Types (items 17, 26, 27, 28)**: Session archival (`archive()`), busy state (`setBusy()`), cursor pagination (`listGlobal()`). `SessionBusyError` class, `session:status` event. `slug.ts` — `generateSlug()` with stop word filtering. 103 tests.

**Phase 3: Parallel Block B (4 agents)**
- **Agent Loop Changes (items 6, 12, 22)**: Tool output saved to file on truncation (`output-files.ts`, 7-day cleanup). Tool call repair (`repair.ts`, 4 strategies). Agent step limits (`maxSteps` in AgentConfig, `MAX_STEPS` terminate mode). 80 tests.
- **Instructions (items 7, 16)**: Subdirectory AGENTS.md walking (`subdirectory.ts`, 3-layer dedup). URL instructions (`url-loader.ts`, 5s timeout). `'remote'` scope, `urls?` config. 51 tests.
- **Compaction + Session Events (items 8, 33)**: `pruneStrategy` — 40K token budget, protected tools. `session:status` events on status changes. 113 tests.
- **Permissions + Providers (items 10, 11, 13, 25)**: Bash parser (`bash-parser.ts`, lightweight tokenizer). Arity fingerprinting (`arity.ts`, 100+ commands). `buildApprovalKey()` uses arity. Provider transforms: `filterEmptyContentBlocks()`, `enforceAlternatingRoles()`, `truncateMistralIds()`. LiteLLM provider. 305 tests (37 new).

**Phase 4: Parallel Block C (3 agents)**
- **File Watcher + Structured Output + Per-Message Overrides (items 4, 9, 14)**: `FileWatcher` class (polling), watches `.git/HEAD`, emits `git:branch-changed`. `__structured_output` tool, `validateStructuredOutput()`, forced `tool_choice`. Per-message `_system`, `_format`, `_variant` overrides. 32 tests.
- **PTY + Task Resumption + Explore Subagent (items 3, 15, 24)**: PTY tool using `getPlatform().pty`, ANSI stripping, progress streaming. `task_id` param for session resumption. Explorer worker with `deniedTools` (7 allowed + 15 denied tools). 39 tests.
- **Diff Enhancements + Session Sharing + Plan Saves (items 18, 19, 20, 21, 35)**: `toolCallIndex`/`messageIndex` on FileDiff. `diff:revert-to` handler. `summarizeDiffSession()`. `savePlanToFile()`. Sharing extension stub. 55 tests.

**Phase 5: Low Priority (2 agents)**
- **Protocol Features (items 29, 30, 31, 32)**: `parseRetryAfterMs()` for 3 formats. `fetchWellKnownConfig(domain)`. OpenAI Responses API body builder + `shouldUseResponsesAPI()` auto-routing. ACP protocol stub. 73 tests.
- **Cleanup + Integration Tests**: Activation smoke test updated to 25 extensions (session snapshot at that time). Full suite: 4,859 tests across 310 files. Build, typecheck, lint all clean.

**CLAUDE.md updated:** Tools table 44 → 50+ (added pty, 6 new LSP tools, delegate_explorer). Extensions module map updated. Extension API updated with `registerHook`/`callHook`.

---

### Sprint 21 — Frontend ↔ Core-v2 Full Integration (7 Phases)

**13 new tests across 3 test files.** Total: ~4,744 tests across ~288 files. Closed all integration gaps between desktop app (`src/`) and core-v2 backend.

**Phase 1: Plugin SessionManager Dedup**
- `App.tsx` replaced duplicate `createSessionManager()` with shared `getCoreSessionManager()` from core-bridge.

**Phase 2: Export onEvent from Barrel**
- `packages/core-v2/src/index.ts` now exports `onEvent` (runtime) and `EventHandler` (type) from extensions barrel.

**Phase 3: Bidirectional Settings Sync**
- NEW: `src/services/settings-sync.ts` — bridges core SettingsManager `category_changed`/`category_registered` events → `CustomEvent('ava:core-settings-changed')`.
- `markPushing()` + `queueMicrotask()` pattern prevents feedback loops when frontend pushes to core.
- `settings-persistence.ts` calls `markPushing()` before `pushSettingsToCore()`.
- Settings store listens for `ava:core-settings-changed` and maps `permissions`/`context`/`git` categories back to AppSettings.
- 3 tests in `settings-sync.test.ts`.

**Phase 4: Extension Event Bridge Hook**
- NEW: `src/hooks/useExtensionEvents.ts` — 3 reactive SolidJS hooks bridging `onEvent()` → signals:
  - `useExtensionEvent<T>(name)` → latest value signal
  - `useExtensionEvents(names)` → record of signals
  - `useExtensionEventLog<T>(name, max)` → accumulated array
- All auto-dispose via `onCleanup()`. 5 tests.

**Phase 5: Model Status Hook**
- NEW: `src/hooks/useModelStatus.ts` — subscribes to `models:updated` + `models:ready` events.
- Exposes `modelCount`, `lastUpdate`, `refresh()`. 5 tests.

**Phase 6: Context Budget Sync**
- `core-bridge.ts` subscribes to `context:compacting` and `agent:finish` events, syncs to `ContextBudget`.
- `ContextBudget` gained `setUsed(tokens)` method for external sync.
- `session.ts` contextUsage memo uses reactive `budgetTick` signal for re-evaluation on agent events.

**Phase 7: Chat → AgentExecutor Unification**
- **Major refactor**: `stream-lifecycle.ts` now uses `AgentExecutor` instead of direct `client.stream()`.
- All chat tool execution goes through full middleware chain (permissions, hooks, sandbox, checkpoints, doom loop detection).
- `StreamOptions.goal` replaces `StreamOptions.messages` — conversation context passed via `AgentInputs.context`.
- `context-tracking.ts` refactored: `buildApiMessages` → `buildConversationContext`, `maybeCompact` removed (AgentExecutor handles compaction internally). Added `buildChatSystemPrompt()`.
- Diff capture moved to temporary `ToolMiddleware` at priority 25 — captures before/after file content, records FileOperation, runs lint check.
- `send-message.ts` rewritten to build `goal` + `systemPrompt` + `conversationContext` instead of messages array.
- Integration test updated with `AgentExecutor` mock, `addToolMiddleware` mock, `onEvent` mock.

**Files:** 6 new (3 source + 3 tests), 12 edited, 0 deleted.

---

### Sprint 20 — Backend Completion Sprint (26 Items, 5 Phases)

**~245 new tests across 20+ test files** in the new architecture stack. Tool count 43 → 44. Total: ~4,522 tests across ~285 files. 5 E2E CLI smoke tests passing.

**Phase 1: Critical Praxis Fixes (3 items)**
- P-01/P-02: Child agent CWD scoping — git/diff extensions now prefer `ctx.workingDirectory` over cached cwd in snapshot middleware, `/undo`, `/snapshot`. Delegate emits `session:child-opened` event before `child.run()`. `create_file` catch block only ignores EEXIST/EISDIR (was swallowing all errors).
- P-03: Auto-detect flat vs hierarchy — `selectAgentMode()` heuristic wired from `agent-modes/src/selector.ts` into CLI via dynamic import (replaces inline duplicate).

**Phase 2: Extension Wiring Quick Wins (6 items)**
- B-071: Git snapshots integration test — 10 tests verifying snapshot middleware captures file state.
- B-074/CG-08: Verified already done (diff tracking, SKILL.md compat), marked complete.
- B-075: Scheduler consumer — memory extension emits `scheduler:register` for periodic flush, added `store.flush()`.
- B-076: `load_skill` tool — `createLoadSkillTool()` in skills extension, loads skill by name from shared array.
- CG-07: Plugin tool hooks — `tool:before-register` event emitted in `registry.ts` before `tools.set()`.

**Phase 3: New Features (6 items)**
- B-036: File @mentions — `expandAtMentions(goal, cwd)` replaces `@path/to/file` with `<file>` content blocks in CLI input. Handles absolute/relative/`./` paths, deduplication, non-existent files left as-is.
- B-037: Session export — `exportSessionToMarkdown()` + `exportSessionToJSON()`, `/export` slash command.
- B-061: Streaming tool output — `onProgress` callback in `ToolContext`, `tool:progress` event in `AgentEvent` union, bash stdout wired to progress callback.
- B-064: Auto-formatter after edits — middleware at priority 50, detects biome.json/.prettierrc/deno.json, runs formatter after write_file/edit/create_file/apply_patch. Per-directory caching.
- B-077: Custom user tools — `loadCustomTools()` discovers `.ts`/`.js` from `.ava/tools/` + `~/.ava/tools/`, dynamic imports, registers via `api.registerTool()`.
- B-079: `/init` command — `generateProjectRules()` scans for config files, generates `CLAUDE.md` with detected stack.

**Phase 4: Praxis Quality + Competitive Gaps (5 items)**
- P-04: Shared file cache — `files` param on delegate tools injects `<file path="...">` content blocks into child goal.
- P-05: Budget awareness — budget instruction appended to child agent system prompt: "You have N turns maximum."
- CG-03: /undo integration verified — 8 integration tests covering write→undo→restore→redo, cross-file, dispose.
- CG-09: Git worktree isolation — `createWorktree(cwd, sessionId)`, `removeWorktree()`, `isolation` config in delegation.
- CG-10: Model packs — 3 built-in packs (budget/balanced/premium), `applyModelPack()` in commander, `resolveModelForTier()`.

**Phase 5: Comprehensive Smoke Tests**
- 8 unit smoke test files: core tools (6), extended tools (20), commander delegates (5), memory tools (4), git tools (4), LSP tools (3), skills tool (1), activation (23 extensions), permissions (5 modes × 6 categories).
- 5 real E2E CLI smoke tests via Sonnet 4.6/OpenRouter: flat mode (6 turns, 31s), Praxis mode (5 commander turns + workers, 382s), memory tools (3 turns, 8s), git tools (5 turns, 13s), background shell (4 turns, 10s).

**CLAUDE.md updated:** Tools table 43 → 44 (added load_skill).

---

### Sprint 19 — Backend Completion Sprint (28 Items, 4 Phases)

**~360 new tests across 40+ test files** in the new architecture stack. Tool count 35 → 43. Extensions 26 → 28. Total: ~4,280 tests across ~270 files.

**Phase 1: Core Agent Loop Hardening (8 items)**
- Parallel tool execution — `parallelToolExecution` config, `Promise.all()` for independent tools, `--sequential` CLI flag. Detects dependencies by checking if tool B's input references tool A's output.
- Image/vision support — `ImageBlock` type added to `ContentBlock` union, agent loop handles image content in messages, OpenAI-compat image block conversion for providers.
- Tool result truncation — `truncateToolResults()` enforces 50KB per result and 200KB total context max with `[...truncated N chars]` marker.
- Background shell management — 3 new tools: `bash_background` (spawn process, return PID), `bash_output` (read stdout/stderr from PID), `bash_kill` (terminate by PID). `ProcessRegistry` singleton tracks all PIDs with stdout/stderr buffers and exit codes.
- Tool list change notifications — `tools:registered` and `tools:unregistered` events emitted via message bus on register/unregister. Agent loop subscribes to refresh available tools.
- MCP health monitoring — `MCPHealthMonitor` class with configurable ping interval (default 30s), stuck process detection (10s timeout), auto-restart on failure, `mcp:health` events.
- Auto-compaction improvements — Strategy selection: `summarize` for long sessions, `truncate` for short. `context:compacted` event with before/after token counts.
- Steering interrupt improvements — `steer()` method on agent loop, `agent:steered` event. New user messages during tool execution abort remaining tools and inject immediately.

**Phase 2: Praxis End-to-End (6 items)**
- Auto-planning orchestrator — `executeOrchestration()` groups subtasks into dependency-ordered batches, executes independent subtasks via `Promise.all()`, respects `maxParallelDelegations` (default 3), emits `orchestration:batch-start`/`orchestration:batch-complete` events. Deadlock detection for dependency cycles.
- Task routing integration — `analyzeDomain()` keyword matching maps task descriptions to 4 domains (frontend/backend/testing/devops), `selectWorker()` picks best lead agent.
- Per-domain tool filtering — Frontend Lead: read-only + web tools. Backend Lead: all tools + LSP. QA Lead: read-only + test runner. Fullstack Lead: all tools.
- Result aggregation — `aggregateResults()` combines multi-lead results into structured summary: files changed, tests run, issues found, total duration.
- Error recovery in delegation — Retry with more specific prompt on first failure, escalate to parent on second failure, `delegation:retry` event, configurable `maxRetries` (default 1).
- Parallel agent execution — Independent subtasks (no dependency edges in TaskPlan) run via `Promise.all()`. Each parallel agent gets own working context. Results collected and aggregated.

**Phase 3: Competitive Feature Gaps (8 items)**
- Git tools — 4 new tools using `gh` CLI: `create_pr` (create GitHub PR), `create_branch` (create + switch), `switch_branch` (switch existing), `read_issue` (read GitHub issue with comments/labels/state).
- Per-tool-call checkpoints — Git stash middleware at priority 20. After every file-modifying tool call (write, edit, create, delete, bash), auto-creates lightweight checkpoint. `CheckpointStore` tracks checkpoints for instant rollback.
- Granular permission modes — 5 levels: `suggest` (never execute), `ask` (ask every call), `auto-edit` (auto-approve edits, ask for bash/delete), `auto-safe` (auto read+edit, ask bash/delete/network), `yolo` (approve everything). 6 tool categories: read/edit/execute/delete/network/agent.
- MCP streamable HTTP transport — `HttpStreamTransport` class implementing newest MCP standard. HTTP POST for requests, SSE for streaming responses, bidirectional on single connection, session ID management.
- Auto-learning memory — Pattern detectors for tech-stack (8 frameworks), test-framework (4 runners), primary language (4 languages). Hooks into `agent:completing` event, filters by confidence ≥ 0.7, emits `memory:auto-learned`.
- Model availability + fallback — EMA latency tracking, provider/model status (available/degraded/unavailable), per-provider fallback chains (e.g., Opus → Sonnet → GPT-4o), `getAvailableModel()` auto-selects.
- Global doom loop detection — `trackGlobalToolCall()` and `detectGlobalDoomLoop()` track patterns across all concurrent agents at registry level. Extends per-agent detection.
- Toolshim for non-tool-calling models — `parseToolCallsFromText()` parses XML-based `<tool_call>` blocks from text responses. `buildToolSchemaXML()` injects tool descriptions into system prompt. `needsToolShim()` detects models without native tool_use.

**Phase 4: Plugin Infrastructure (2 items)**
- Plugin install/uninstall backend — `installPlugin()` from local path, GitHub URL, or registry. `uninstallPlugin()` with cleanup. Signature verification stub. Installs to `~/.ava/plugins/`.
- Plugin catalog API — `fetchCatalog()` from remote JSON, `searchCatalog()` with keyword + category filtering, `getCatalogEntry()` by ID, 30-minute cache TTL.

**Phase 5: Smoke Test (2026-03-02)**
- Full E2E CLI smoke test with Sonnet 4.6 via OpenRouter — 12 turns, 569 seconds, SUCCESS
- Praxis hierarchy activated: Commander → fullstack-lead → backend-lead + qa-lead
- Built calculator project in /tmp: 5 functions, 39 tests, all passing
- 28 extensions loaded, all tools functional

**CLAUDE.md updated:** Tools table 35 → 43 (added bash_background/output/kill, create_pr, create_branch, switch_branch, read_issue). Extensions module map updated.

---

## 2026-02-28

### Sprint 17 — Backend Completion (All 9 "What's Next" Items)

**271 new tests across 39 test files** in the new architecture stack. Tool count 28 → 35. Extensions 24 → 25. Total: ~3,896 tests across ~250 files.

**Phase A: Quick Fixes**
- Validator `enabledByDefault: true` + `agent:completing` event handler wired
- Focus-chain event names fixed: `agent:turn-start` → `turn:start`, `agent:turn-end` → `turn:end`, `agent:completed` → `agent:finish`
- Focus-chain payload casts fixed: `sessionId` → `agentId`
- Created `validator/src/index.test.ts` (3 tests)
- Updated `focus-chain/src/index.test.ts` (5 tests updated)

**Phase B: Provider Tests**
- 10 new test files for providers missing tests: deepseek, mistral, groq, xai, cohere, together, ollama, glm, kimi, openrouter
- Each uses shared `testProviderActivation()` harness (3 tests per provider = 30 new tests)

**Phase C: SQLite Session Storage**
- `SessionStorage` interface with save/load/delete/list/loadAll — `core-v2/src/session/storage.ts`
- `MemorySessionStorage` (Map-based) — `core-v2/src/session/memory-storage.ts`
- `SqliteSessionStorage` (uses `IDatabase`) — `core-v2/src/session/sqlite-storage.ts`
- `serializeSession()` / `deserializeSession()` — handles Map↔Object conversion
- `SessionManager` updated with `save()`, `loadSession()`, `loadFromStorage()`, `startAutoSave()`
- 18 new tests across 3 files (storage, sqlite-storage, manager)

**Phase D: Persistent Memory Extension**
- New extension: `packages/extensions/memory/` with manifest, store, tools, entry
- `MemoryStore` — CRUD over ExtensionStorage, categories (project/preferences/debug/context)
- 4 tools: `memory_write`, `memory_read`, `memory_list`, `memory_delete`
- System prompt injection via `prompt:build` event
- 19 new tests across 3 files

**Phase E: MCP Advanced Features**
- Resources: `listResources()`, `readResource()` in client + manager
- Prompts: `listPrompts()`, `getPrompt()` in client + manager
- Sampling: `onSamplingRequest()` handler, server→client JSON-RPC request routing
- Reconnection: `ReconnectStrategy` with exponential backoff + jitter — `mcp/src/reconnect.ts`
- OAuth: authorization code flow, token exchange, refresh — `mcp/src/oauth.ts`
- Transport: `onError`/`onClose` callbacks on both `StdioTransport` and `SSETransport`
- 17 new tests across 2 new + 2 updated files

**Phase F: Symbol Extraction**
- Regex-based extractor for TypeScript/JS, Python, Rust, Go — `codebase/src/symbol-extractor.ts`
- Extracts functions, classes, interfaces, types, enums, methods, variables
- `/symbols` command registered
- 12 new tests

**Phase G: Full LSP Client**
- `LSPTransport` — Content-Length header framing (NOT newline-delimited) — `lsp/src/transport.ts`
- `LSPClient` — initialize, shutdown, didOpen/didChange/didClose, hover, definition, references, completion, diagnostics — `lsp/src/client.ts`
- `LSPServerManager` — per-language server lifecycle (start/stop/restart) — `lsp/src/server-manager.ts`
- `formatHover()`, `formatLocations()`, `formatDiagnostics()` — `lsp/src/queries.ts`
- 3 tools: `lsp_diagnostics`, `lsp_hover`, `lsp_definition`
- 35 new tests across 5 files

**CLAUDE.md updated:** Tools table 28 → 35 (added memory_read/write/list/delete, lsp_diagnostics/hover/definition)

---

## 2026-02-26

### Sprint 8 — Test Coverage Push (core-v2 + extensions)

**195 new tests across 15 test files** in the new architecture stack (core-v2 + extensions).

**New test files (15):**
- `packages/core-v2/src/agent/loop.test.ts` — AgentExecutor: run, events, termination modes (22 tests)
- `packages/core-v2/src/agent/types.test.ts` — AgentTerminateMode enum, DEFAULT_AGENT_CONFIG (6 tests)
- `packages/core-v2/src/extensions/loader.test.ts` — loadExtensionsFromDirectory, loadBuiltInExtension (13 tests)
- `packages/extensions/tools-extended/src/batch.test.ts` — Parallel tool execution, nesting prevention (8 tests)
- `packages/extensions/tools-extended/src/multiedit.test.ts` — Atomic multi-edit, error handling (7 tests)
- `packages/extensions/tools-extended/src/create.test.ts` — File creation, parent dirs (6 tests)
- `packages/extensions/tools-extended/src/delete.test.ts` — File deletion, directory rejection (5 tests)
- `packages/extensions/tools-extended/src/ls.test.ts` — Directory listing, ignore filters (7 tests)
- `packages/extensions/tools-extended/src/todo.test.ts` — In-memory todo CRUD (7 tests)
- `packages/extensions/tools-extended/src/completion.test.ts` — Completion signaling (4 tests)
- `packages/extensions/tools-extended/src/question.test.ts` — Question formatting (5 tests)
- `packages/extensions/validator/src/validators.test.ts` — syntax, typescript, lint, test validators (35 tests)
- `packages/extensions/agent-modes/src/minimal-mode.test.ts` — State management, tool filtering (14 tests)
- `packages/extensions/commander/src/workers.test.ts` — 5 worker definitions validation (23 tests)
- `packages/extensions/context/src/strategies.test.ts` — truncate + summarize compaction (15 tests)

**Infrastructure:**
- Added `@ava/core-v2/__test-utils__/mock-platform` alias to `vitest.config.ts`

**Test totals:**
- core-v2: 344 → 407 tests (24 files)
- extensions: 187 → 319 tests (28 files)
- **Total: ~3302 tests** across ~162 test files

---

## 2026-02-25

### Session 55 — Logger Refactor + Agent CLI Command + Test Coverage

**Logger refactored from single file to directory module** — Replaced `packages/core/src/logger.ts` (single file, console-only) with `packages/core/src/logger/` directory module (4 files). New module provides: `AvaLogger` singleton class with NDJSON file output (`~/.ava/logs/ava-YYYY-MM-DD.ndjson`), stderr output, custom callbacks, and `fromAgentEvent()` method for structured agent event logging. Retained `createLogger()` for inline source-scoped console logging (used by agent loop constructor and commander modules). Singleton pattern: `getLogger()`/`setLogger()`/`resetLogger()`.

**CLI agent command** — NEW `cli/src/commands/agent.ts` (~295 lines). `ava agent run "goal"` invokes the agent loop from the CLI with `--verbose` (stderr streaming), `--json` (NDJSON stdout), `--provider`, `--model`, `--max-turns`, and `--timeout` options.

**New test files (7):**
- `packages/core/src/logger/logger.test.ts` — AvaLogger, createLogger, singleton, agent event mapping
- `packages/core/src/auth/manager.test.ts` — Auth manager operations
- `packages/core/src/auth/pkce.test.ts` — PKCE challenge/verifier generation
- `packages/core/src/git/utils.test.ts` — Git utility functions
- `packages/core/src/validator/pipeline.test.ts` — ValidationPipeline orchestration
- `packages/core/src/validator/syntax.test.ts` — Syntax validator
- `packages/core/src/validator/typescript.test.ts` — TypeScript validator

**Import fixes** — Updated `commander/router.ts` and `commander/executor.ts` to import from `../logger/logger.js` (was `../logger.js`). Updated `agent/loop.ts` to import both `createLogger` and `getLogger` from unified module.

**Files changed:**
- `packages/core/src/logger.ts` — **deleted** (merged into directory module)
- `packages/core/src/logger/logger.ts` — new (AvaLogger + createLogger, ~380 lines)
- `packages/core/src/logger/types.ts` — new (LogLevel, LogEntry, LoggerConfig, ~74 lines)
- `packages/core/src/logger/index.ts` — new (barrel export)
- `packages/core/src/logger/logger.test.ts` — new
- `packages/core/src/agent/loop.ts` — import updated
- `packages/core/src/commander/router.ts` — import updated
- `packages/core/src/commander/executor.ts` — import updated
- `packages/core/src/index.ts` — exports from `./logger/index.js`
- `cli/src/commands/agent.ts` — new
- `cli/src/index.ts` — agent command wired + help text merged
- `docs/development/sprints/2026-S3.0-stabilization.md` — new sprint doc
- 6 new test files (auth, git, validator)
- **Total: ~2576 tests** across ~110 test files

---

## 2026-02-21

### Session 54 — OpenRouter Tool Calling + Completion Tool Naming Fix

**OpenRouter provider rewrite** — Refactored `llm/providers/openrouter.ts` to use shared `openai-compat.ts` utilities (`buildOpenAIRequestBody`, `ToolCallBuffer`, `readSSEStream`). OpenRouter now sends tools in API requests and parses `delta.tool_calls` from the stream, matching the pattern used by DeepSeek, Groq, Mistral, xAI, and Together providers. Previously, OpenRouter hand-rolled SSE parsing and completely ignored tools, so the agent loop always terminated with `NO_COMPLETE_TASK`.

**Completion tool naming unification** — Fixed a naming mismatch where the system prompt told models to call `attempt_completion` but the agent loop injected a dynamic tool named `complete_task`. Models received conflicting instructions and would return plain text instead of calling either tool. Renamed `COMPLETE_TASK_TOOL` constant from `'complete_task'` to `'attempt_completion'` and added deduplication in `getAvailableTools()` to prevent duplicate tool definitions (both the registered tool from `completion.ts` and the dynamic one from the agent loop were named `attempt_completion` after the rename, which caused providers to silently reject requests).

**CLI cleanup** — Fixed `mock-client.ts` tool name (`complete_task` → `attempt_completion`), removed stale `"estela"` binary alias from `cli/package.json`.

**Tests** — 11 new OpenRouter provider tests (tool calls, auth, HTTP errors, headers, usage). Updated all agent tests to use `attempt_completion` naming. 103 test files, 2466 tests passing.

**Known behavior:** Sonnet via OpenRouter completes in 3 turns (read → text response → recovery → `attempt_completion`) because the model returns its analysis as plain text before calling the completion tool. The `NO_COMPLETE_TASK` recovery handles this gracefully — it's a model behavior pattern, not a bug.

**Files changed:**
- `packages/core/src/llm/providers/openrouter.ts` — rewritten with shared utils
- `packages/core/src/llm/providers/openrouter.test.ts` — new (11 tests)
- `packages/core/src/agent/types.ts` — `COMPLETE_TASK_TOOL = 'attempt_completion'`
- `packages/core/src/agent/loop.ts` — dedup in `getAvailableTools()`, naming updates
- `packages/core/src/agent/evaluator.ts` — naming updates
- `packages/core/src/agent/modes/minimal.ts` — removed duplicate `complete_task` entry
- `packages/core/src/commander/workers/definitions.ts` — worker prompts updated
- `cli/src/commands/mock-client.ts` — tool name fix
- `cli/package.json` — removed `estela` alias
- 5 test files updated for naming consistency

---

## 2026-02-09

### Session 53 — File Watcher + Step-Level Undo
- **File watcher** — `src/services/file-watcher.ts` watches project dir via Tauri FS `watch()` (500ms debounce, recursive)
- **6 AI patterns** — `// AI!`, `// AI?`, `# AI!`, `# AI?`, `-- AI!`, `-- AI?` across 30+ extensions
- **ChatView wiring** — `createEffect` starts/stops watcher; `onComment` auto-sends as chat message
- **Undo button** — Undo2 icon in MessageInput, calls `undoLastAutoCommit()` (git revert), 2.5s feedback
- **FS permissions** — `fs:allow-watch`, `fs:allow-unwatch` added to Tauri capabilities

### Session 52 — Message Queue + Steering Interrupts
- **Message queue** — `useChat` queues follow-ups during streaming, auto-dequeues after completion
- **Steer** — `steer()` cancels current stream + sends new message immediately
- **Cancel** — Now clears queue + aborts (stop = stop everything)
- **Keyboard** — `Ctrl+Shift+Enter` = steer, textarea enabled during streaming for type-ahead

### Session 50 — Architect + Editor Model Split
- **Core config** — `editorModel` + `editorModelProvider` on `ProviderSettings`
- **Helper** — `getEditorModelConfig()` in `llm/client.ts`
- **Commander wired** — `executor.ts` auto-applies editor model to workers
- **Frontend** — `editorModel` dropdown in LLMTab with auto-pair suggestions

### Session 49 — Weak Model for Secondary Tasks
- **Core config** — `weakModel` + `weakModelProvider` on `ProviderSettings`
- **Helper** — `getWeakModelConfig()` in `llm/client.ts`
- **Planner wired** — `agent/planner.ts` uses weak model instead of hardcoded Sonnet
- **Self-review wired** — `validator/self-review.ts` uses weak model for code review
- **Frontend** — `weakModel` dropdown in LLMTab with auto-pair suggestions

### Session 48 — Git Auto-Commit
- **Auto-commit module** — `packages/core/src/git/auto-commit.ts` stages + commits after file-modifying tools
- **Tool registry** — PostToolUse in `registry.ts` calls `autoCommitIfEnabled()`
- **Undo** — `undoLastAutoCommit()` reverts most recent ava-prefixed commit
- **Frontend** — `GitSettings` (enabled, autoCommit, commitPrefix) in BehaviorTab

### Session 51 — OAuth Fix + Error Logging
- **OAuth credential bridging fix** — OpenAI OAuth tokens stored as `type: 'oauth'` via core `setStoredAuth()` (was plain API key). Enables Codex endpoint routing.
- **JWT id_token parsing** — `decodeJwtPayload()` + `extractAccountId()` for ChatGPT account ID
- **Scopes fix** — Reverted `model.request` scope (was causing "insufficient permissions")
- **CSP** — Added `https://chatgpt.com` to `connect-src` for Codex endpoint
- **OAuth disconnect UI** — "Connected via OAuth" badge + LogOut button in ProvidersTab
- **OAuth error logging** — Structured logging via file logger across entire OAuth flow
- **Browser opener fix** — `@tauri-apps/plugin-shell` → `@tauri-apps/plugin-opener`
- **PKCE guard** — Prevents concurrent flows on shared port 1455

### Session 47 — Backend Gaps Fix
- Tool approval bridge: message bus `TOOL_CONFIRMATION_REQUEST` → frontend signal → `TOOL_CONFIRMATION_RESPONSE`
- MCP settings CRUD: `mcpServers[]` in settings store with add/remove/update
- FS scope expansion: Rust `allow_project_path` command via `FsExt`
- Shell timeout: `Promise.race()` wrapper in `TauriShell.exec()`
- OAuth configs: corrected Anthropic (client ID, port, scopes) and OpenAI (port, path, params)
- Paste collapse: large text pastes collapsed into expandable chips
- Dead mock removal: `defaultMCPServers` replaced with real settings state

### Session 46 — Settings Hardening + Appearance
- 16 new settings across GenerationSettings, AgentLimitSettings, BehaviorSettings, NotificationSettings
- 2 new tabs: LLM, Behavior
- Appearance expansion: system theme, dark variants, code themes, ligatures, chat font size, custom accent, sans font, high contrast
- Density recalibration: compact/default/comfortable, 8 components wired
- 706 new backend tests (Agent, Tools, LLM client) → 1778 total

### Session 45 — Frontend Gaps
- File explorer via Tauri FS, code editor reads real files
- Agent DB persistence (saveAgent, getAgents, updateAgentInDb)
- Google models API (dynamic fetch with hardcoded fallback)
- DiffViewer split view (buildSplitPairs)

---

## 2026-02-15

### Sprints B7-B10 — Validator Integration + Minimal Tool Mode + Provider Switching + Auto-Routing
- **B7: Validator pipeline wired into agent loop** — `AgentExecutor` now tracks modified files (`write_file`, `create_file`, `edit`, `delete_file`, `apply_patch`, `multiedit`). On `complete_task`, runs `ValidationPipeline` (syntax, typescript, lint) before accepting completion. On failure, agent gets feedback and retries (up to `maxValidationRetries`). New `validationEnabled` and `maxValidationRetries` fields on `AgentConfig`. `validation:start`/`validation:result`/`validation:finish` events emitted. Settings integration wires `AgentSettings.validatorsEnabled` → `AgentConfig.validationEnabled`.
- **B8: Minimal tool mode** — NEW `agent/modes/minimal.ts` (~95 lines). Per-session state (same pattern as plan mode). 8 allowed tools: `read_file`, `write_file`, `edit`, `bash`, `glob`, `grep`, `attempt_completion`, `complete_task`, `question`. `checkMinimalModeAccess()` wired into `tools/registry.ts` after plan mode check. `toolMode` field added to `AgentConfig`.
- **B9: Mid-session provider switching** — `AgentExecutor.run()` refactored to use mutable `let client`. New `requestProviderSwitch(provider, model)` public method. Main loop checks `pendingProviderSwitch` before each turn; on switch, creates new client and emits `provider:switch` event. Graceful fallback on failure. `AgentConfig.provider` widened from 3-provider union to full `LLMProvider` type.
- **B10: Lead-worker auto-routing** — NEW `commander/router.ts` (~115 lines). `analyzeTask()` does keyword/heuristic analysis returning `TaskAnalysis` (taskType, confidence, keywords, hasCodePaths). `selectWorker()` maps task type → worker (test→tester, review→reviewer, research→researcher, debug→debugger, write→coder). `executeWithAutoRouting()` tries auto-route at confidence ≥ 0.7, returns null for LLM fallback.
- **48 new tests** across 4 files: `agent-validation.integration.test.ts` (5), `minimal.test.ts` (13), `agent-provider-switch.test.ts` (4), `router.test.ts` (26)
- **Total: ~2369 tests** across ~87 test files

### Documentation Audit — Comprehensive Backend Docs Update
- **Architecture guide** — NEW `docs/backend/architecture-guide.md` (~827 lines). Deep navigation guide covering: 18 singletons, request lifecycle, agent system internals, tools system, intelligence modules, safety & permissions (3-layer model), configuration, extensibility, infrastructure, protocols, 7 key patterns, 6 common task recipes.
- **Full codebase exploration** — 6 parallel agents mapped every module in `packages/core/src/`. Results: **257 source files, ~59,700 lines** across 32 directories.
- **modules.md rewrite** — All file counts and line counts updated from actual `wc -l` measurements. Key corrections: agent/ 19 files/5,264 lines (was 12/4,197), tools/ 43 files/12,123 lines (was 37/10,990), llm/ 20 files/2,956 lines (was 16/3,222), permissions/ 13 files/3,924 lines (was 9/3,130), git/ 5 files/969 lines (was 4/799). Added missing files: `tools/sandbox/*`, `tools/namespacing.ts`, `tools/task-parallel.ts`, `permissions/security-inspector.ts`, `permissions/repetition-inspector.ts`, `permissions/inspector-pipeline.ts`, `permissions/audit.ts`, `context/strategies/visibility.ts`, `git/auto-commit.ts`, `llm/utils/*`, `agent/metrics.ts`.
- **README.md updated** — Header stats corrected, module map updated with accurate file/line counts for all 32 directories.
- **gap-analysis.md** — Phase 3+ roadmap items marked DONE: sandbox, parallel subagents, batch parallel, security inspector, visibility metadata, auto-compaction.
- **backlog.md** — Fixed stale file counts (tools/ 43, git/ 5, llm/ includes utils/), marked hook executor tests as DONE.
- **test-coverage.md** — Added source file/line totals, fixed module file counts.

---

## 2026-02-14

### Sprint B6 — Container/Sandbox Execution
- **Sandbox abstraction** — NEW `tools/sandbox/types.ts` (~65 lines). `Sandbox` interface with `exec()`, `isAvailable()`, `cleanup()`. `SandboxConfig` with mode, image, timeout, network, memory, CPU.
- **DockerSandbox** — NEW `tools/sandbox/docker.ts` (~145 lines). Docker-based sandboxed execution via `docker run --rm -v <workdir>:/workspace --network=none --memory=512m --cpus=1`. Timeout enforcement, abort support, network isolation (opt-in).
- **NoopSandbox** — NEW `tools/sandbox/noop.ts` (~90 lines). Host passthrough (backward compat, default mode).
- **Factory** — NEW `tools/sandbox/index.ts` (~30 lines). `createSandbox(config)` returns Docker or Noop based on mode.
- **Sandbox settings** — `SandboxSettingsSchema` added to `config/schema.ts`. `SandboxSettings` interface with `mode`, `image`, `timeoutSeconds`, `networkAccess`, `memoryLimit`, `cpuLimit` in `config/types.ts`. Default: `mode: 'none'` (no behavior change).
- **Bash tool wiring** — `bash.ts` routes through sandbox when `mode: 'docker'`. `executeSandboxed()` function with Docker availability check and graceful fallback to host execution. Output tagging `(sandboxed)` in exit code.
- `tools/sandbox/sandbox.test.ts` — 25 tests (config defaults, Docker args, network toggle, custom image/memory/CPU, NoopSandbox, factory, edge cases)
- **Total: 2687 tests** across 109 files (was 2662 across 108 files)

### Sprint B5 — Security Inspector Pipeline
- **SecurityInspector** — NEW `permissions/security-inspector.ts` (~210 lines). Pattern-based threat detection with confidence scores (0-1). 15 built-in patterns across 5 categories: `command_injection`, `privilege_escalation`, `data_exfiltration`, `file_access`, `resource_abuse`. Configurable block threshold. Custom pattern support.
- `permissions/security-inspector.test.ts` — 25 tests (all threat categories, custom patterns, block threshold, field extraction)
- **RepetitionInspector** — NEW `permissions/repetition-inspector.ts` (~130 lines). Per-tool-call stuck detection with configurable threshold and time window. Complements session-level doom-loop detection.
- `permissions/repetition-inspector.test.ts` — 14 tests (threshold, windowing, clear, configure, order-insensitive params, circular refs)
- **InspectorPipeline** — NEW `permissions/inspector-pipeline.ts` (~180 lines). Three-stage inspection chain: Security → Permission → Repetition. First blocker stops chain. Adapter pattern for plugging inspectors. `createDefaultPipeline()` factory.
- `permissions/inspector-pipeline.test.ts` — 19 tests (pipeline flow, blocking, adapters, audit recording, factory)
- **AuditTrail** — NEW `permissions/audit.ts` (~130 lines). Records every inspector decision with timestamp, tool, inspector, decision, confidence, reason, category. Filterable queries, export, singleton.
- `permissions/audit.test.ts` — 15 tests (record, query filters, getBlocked, getWarnings, export, clear, maxEntries, singleton)
- **Total: 2662 tests** across 108 files (was 2589 across 104 files)

### Sprint B4 — Parallel Subagents + Tool Namespacing
- **Tool namespacing** — NEW `tools/namespacing.ts` (~180 lines). Prevents name collisions between built-in, MCP, and extension tools. Naming convention: `mcp__<server>__<tool>`, `ext__<plugin>__<tool>`. Functions: `namespaceTool()`, `stripNamespace()`, `isNamespaced()`, `getNamespace()`, `getSource()`, `getBareName()`, `mcpToolName()`, `isMcpTool()`, `extToolName()`, `isExtTool()`, `lookupTool()` (backward-compat bare-name matching).
- `tools/namespacing.test.ts` — 29 tests (all namespace operations, lookup with fuzzy matching, prefix constants)
- **Parallel task execution** — NEW `tools/task-parallel.ts` (~340 lines). Semaphore-based concurrent subagent execution via `Promise.allSettled()`. Concurrency limits: explore=5, plan=3, execute=1 (file safety), custom=5. Max 10 parallel tasks. Extended `TaskParams` with `tasks[]` array and `maxConcurrent`.
- `tools/task-parallel.test.ts` — 37 tests (concurrency constants, tasks validation, maxConcurrent validation, combined params, schema, dispatch)
- `tools/task.ts` refactored — split into task.ts (definition + single execution) and task-parallel.ts (parallel execution)
- **Total: 2589 tests** across 104 files (was 2523 across 102 files)

### Sprint B3 — Context Intelligence
- **Visibility metadata** — `MessageVisibility` type (`all` | `user_visible` | `agent_visible`) added to `Message` in `context/types.ts`
- **NEW: `context/strategies/visibility.ts`** — Visibility-aware compaction strategy. Tags older messages as `agent_visible` instead of removing them. Helper functions: `isUserVisible()`, `isAgentVisible()`, `filterUserVisible()`, `filterAgentVisible()`, `tagVisibility()`
- `context/strategies/visibility.test.ts` — 18 tests (visibility helpers, filter functions, compaction strategy)
- **Auto-compaction threshold** — Already existed via `createAutoCompactor()` in `compactor.ts` with configurable `threshold` param + `compactionThreshold` in settings schema (default: 80%). Now tested.
- `context/auto-compaction.test.ts` — 12 tests (createAutoCompactor threshold behavior, Compactor strategy management, needsCompaction, getUsagePercent)
- **Total: 2523 tests** across 102 files (was 2493 across 100 files)

### Sprint B2 — Test Coverage Medium + Agent Metrics
- **12 new test files + 1 new source file** covering medium-effort modules and a new feature
- `permissions/rules.test.ts` — 28 tests (BUILTIN_RULES, assessCommandRisk, assessPathRisk, getHighestPathRisk)
- `permissions/quote-parser.test.ts` — 48 tests (quote state machine, dangerous chars, command segments, redirects, subshells)
- `config/credentials.test.ts` — 20 tests (key CRUD, provider listing, validation, singleton)
- `config/migration.test.ts` — 24 tests (migrateSettings, mergeWithDefaults, findEnvApiKeys, needsMigration, getChangedFields)
- `config/export.test.ts` — 20 tests (export/import JSON, mergeSettings, diffSettings, getDefaultSettingsJson)
- `agent/prompts/system.test.ts` — 40 tests (RULES/CAPABILITIES, buildSystemPrompt, buildWorkerPrompt, buildScenarioPrompt, getModelAdjustments)
- `codebase/ranking.test.ts` — 25 tests (calculatePageRank, calculateRelevanceScore, extractKeywords, sortByRank/Score)
- `codebase/graph.test.ts` — 24 tests (getEdges, findRoots/Leaves, findCircularDependencies, getDependencyDepth, transitive deps)
- `context/strategies/strategies.test.ts` — 27 tests (slidingWindow, createSlidingWindow, buildSummaryTree, selectLevel, createSummarize, extractSummary)
- `commander/parallel/activity.test.ts` — 17 tests (ActivityMultiplexer, createTaggedCallback, createFilteredCallback, createAggregator)
- `commander/parallel/scheduler.test.ts` — 13 tests (TaskScheduler, createLinearChain, createFanOut, createFanIn)
- **NEW: `agent/metrics.ts`** — ~120 lines. `MetricsCollector` tracks per-session turns, tokens, tool usage, errors, recoveries, duration
- `agent/metrics.test.ts` — 15 tests (record events, track metrics, export, singleton)
- **Total: 2493 tests** across 100 files (was 2192 across 88 files)

### Sprint B1 — Test Coverage Easy Wins
- **6 new test files + 1 expanded** covering all pure-function modules with zero prior coverage
- `focus-chain/parser.test.ts` — 45 tests (parse, serialize, update, add, remove, progress, next task)
- `diff/unified.test.ts` — 26 tests (createDiff, parseDiffHunks, getDiffStats, hasChanges, extractPaths, formatDiffLines)
- `diff/tracker.test.ts` — 33 tests (DiffTracker: add, apply, reject, queries, bulk ops, events, singleton)
- `models/registry.test.ts` — 34 tests (lookup, query, pricing, validation, suggested models)
- `question/manager.test.ts` — 22 tests (ask/answer flow, cancel, timeout, queries, events, factory/singleton)
- `scheduler/scheduler.test.ts` — 22 tests (register, start/stop, runNow, concurrency limits, callbacks, factory/singleton)
- `mcp/oauth.test.ts` — expanded with 12 new tests (hasStoredTokens, clearPendingStates, resetTokenCache, completeOAuthFlow validation, getAuthorizationHeader, token cache behavior)
- **Total: 2192 tests** across 88 files (was 1996 across 82 files)

### Session 55 — Task Tool Wired + Agent Pipeline Integration Tests
- **Task tool wired** — `tools/task.ts` placeholder replaced with real `AgentExecutor` spawning. Builds filtered tool list (excludes `task` for recursion prevention), bridges `AgentEvent` to `SubagentManager` progress events, uses editor model config for subagents. Maps `AgentTerminateMode` → `SubagentResult['terminationReason']`.
- **Mock LLM utility** — `agent/__tests__/mock-llm.ts` (~70 lines). Programmable `LLMClient` with scripted `StreamDelta` sequences. Auto-completes when queue is empty to prevent infinite loops.
- **10 integration tests** — `agent/__tests__/agent-pipeline.integration.test.ts` (~300 lines). Covers: tool dispatch → GOAL termination, MAX_TURNS + recovery, NO_COMPLETE_TASK detection, doom loop detection, abort signal, event ordering, filtered tools, executeWorker, task tool spawning subagent, recursion prevention.
- **Competitor research** — Deep audit of Goose (Rust, MCP-native, 3-inspector pipeline, lead-worker auto-routing), OpenCode (batch parallel, resumable subagents), Cline (5 parallel subagents, git checkpoints), Roo Code (mode system, boomerang delegation, tool repetition detector).
- **Backend docs updated** — gap-analysis.md (19 new gaps, Roo Code added as 9th codebase, expanded per-codebase takeaways), backlog.md, changelog.md, modules.md, test-coverage.md all updated.
- **Total: 1996 tests** across 82 files (was 1801 across 70 files)

### Documentation Normalization — Tested Baseline Alignment
- Normalized status docs to the latest verified baseline: **1801 tests across 70 files**
- Synced roadmap, vision, backend overview, and development matrix references
- Added active epic tracking docs under `docs/development/epics/`
- Marked legacy sprint plan docs as historical context and linked current status sources
- Clarified that manual desktop OAuth runtime matrix remains the last manual MVP gate

---

## 2026-02-08

### Session 43 — Backend Test Coverage Phase 2
- **706 new tests** across 15 test files + 2 helper files
- Agent: evaluator (35), events (53), recovery (107), planner (32), plan mode (36) = 263
- Tools: utils (95), sanitize (76), truncation (22), locks (33), completion (31), validation (31), define (40), todo (19), edit-replacers (65) = 412
- LLM: client (31)
- Fixed 5 missing module exports in `index.ts` (a2a named exports, policy named exports)
- **Total: 1778 tests** across 64 files (was 1072)

### Session 42 — Density + Font Wiring Fix
- Density values recalibrated (compact 4/8px, default 6/12px, comfortable 8/16px)
- Section density added (`--density-section-py/px`)
- 8 frontend components wired with density CSS variables
- Chat font size applied to MessageInput textarea

### Session 41 — Appearance Expansion
- 8 new appearance features: system theme, dark variants, code themes, ligatures, chat font size, custom accent, sans font, high contrast
- `setupSystemThemeListener()` for OS theme sync
- `hexToAccentVars()` for custom accent color computation
- localStorage bridge for flash prevention

### Session 40 — Core Frontend Wiring
- `core-bridge.ts` — initializes 5 core singletons at startup
- `pushSettingsToCore()` — maps frontend settings to core SettingsManager
- `ContextBar.tsx` — token usage progress bar
- Session checkpoints (`createCheckpoint`, `rollbackToCheckpoint`)
- Agent memory recording via `getCoreMemory().remember()`

### Session 39 — Backend Testing Phase 1
- **536 new tests** across 24 files covering Config, Context, Memory, Session, Commander
- Appearance tab (dark/light mode, 6 accent colors, UI scale, mono font)
- Settings redesign (all tabs rewritten to flat minimal rows)

---

## 2026-02-07

### Session 37 — Phase 1 Completion
- Provider expansion: 14 providers in Settings UI at that time (was 4). Current snapshot: 16 providers.
- Google + Copilot OAuth (device code flow)
- Team delegation flow visualization (SVG animated lines)
- Session fork ("Fork from here" context menu)
- Plugin browser shell placeholder

### Session 36 — Frontend Gaps
- Working directory fix for `useChat` and `useAgent`
- Tool approval wired (`ApprovalRequest`, `checkAutoApproval`, `createApprovalGate`)
- Session duplicate implementation
- Dead code removed: `-975 lines` (old LLM client, providers, credentials)

### Session 35 — LLM Integration Working
- Root cause: 3 disconnected credential stores
- Fix: `syncProviderCredentials()` + `syncAllApiKeys()` bridge
- Anthropic `dangerous-direct-browser-access: true` header
- Chat → streaming LLM response now working end-to-end

---

## 2026-02-05

### Sessions 32-33 — Vision + MVP Sprints
- Defined "The Obsidian of AI Coding" vision
- 7 MVP sprints defined
- Tauri hardening (CSP, scoped FS, deferred window, release profile)
- Code splitting (solid 116KB, icons 20KB, app 408KB, vendor 2.3MB)

### Session 30 — Epics 25-26
- Epic 25: ACP + A2A protocols (97 tests)
- Epic 26: Gemini CLI feature parity (337 tests)

---

## 2026-02-04

### Epics 19-21 — MVP Polish
- Epic 19: Hooks system (PreToolUse, PostToolUse, etc.)
- Epic 20: Browser tool (Puppeteer automation)
- Epic 21: Provider expansion
- Feature parity sprints 1-7

---

## 2026-02-03

### Epics 8-17 — Agent System Build
- Epic 8: Agent loop (autonomous execution)
- Epic 9: Commander (hierarchical delegation)
- Epic 10: Parallel execution (batch, scheduling, conflict detection)
- Epic 11: Validator (QA pipeline)
- Epic 12: Codebase understanding (symbols, imports, PageRank)
- Epic 13: Config system (settings, credentials, migration)
- Epic 14: Memory system (episodic, semantic, procedural)
- Epic 15-17: Enhancement (OpenCode features, missing tools)

---

## 2026-02-02

### Epics 3-7 — Infrastructure
- Epic 3: ACP monorepo structure
- Epic 4: Safety system (permissions, policy, trust)
- Epic 5: Context management (tracking, compaction)
- Epic 6: Developer experience
- Epic 7: Platform abstraction (Node, Tauri, browser)

---

## 2026-01-29 — 2026-01-30

### Epics 1-2 — Foundation
- Epic 1: Multi-provider LLM streaming
- Epic 2: File tools (7 tools initially)

---

## 2026-01-28

### Project Scaffold
- Tauri + SolidJS + SQLite initial setup
- Monorepo with packages/core, packages/platform-node, packages/platform-tauri

---

*Last updated: 2026-03-02 — ~4,859 tests across ~310 test files (includes core-v2 + extensions + frontend integration)*
