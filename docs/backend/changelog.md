# Backend Changelog

> Development history of `packages/core/`. Most recent first.

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
- Provider expansion: 14 providers in Settings UI (was 4)
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

*Last updated: 2026-02-14*
