# Backend Backlog

> Gaps, missing features, and future work for `packages/core/`.

## Ownership Rules

- Source of truth here: backend/core implementation backlog only.
- Frontend-only implementation items belong in `docs/frontend/backlog.md`.
- Cross-cutting frontend-backend lifecycle wiring is tracked in `docs/development/backlogs/integration-backlog.md`.

---

## Test Coverage Gaps (Priority Order)

## Audit Additions (Session 54)

### P0
- Complete manual OAuth desktop matrix (OpenAI, Anthropic, Copilot: connect/disconnect/send flow)
- Finish debug logging coverage across chat/agent/core/session/settings/file-watcher/ChatView
- Align sprint evidence docs whenever full-suite totals change
- Support plugin lifecycle wiring contracts consumed by frontend settings manager (`INT-001`, `INT-002`, `INT-003`)

### P1
- ~~PI Coding Agent parity items (provider switching, session branching tree, minimal tool mode, runtime skill creation)~~ **DONE** (provider switching Sprint B9, session branching Gap Analysis Batch 6, minimal tool mode Sprint B8, skill CRUD Gap Analysis Batch 5)
- ~~MCP OAuth flows (auth + refresh + storage)~~ **DONE** (Gap Analysis: `mcp/src/oauth.ts` PKCE flow + `mcp/src/reconnect.ts` exponential backoff)
- ~~Remote browser support baseline~~ Removed — browser tool deleted (Sprint 13), use Puppeteer MCP server

### P2
- Reduce oversized frontend files (>300 lines) to meet CLAUDE.md constraints

### High Priority — Pure Functions, Easy Wins

> **DONE (Sprint B1)** — All 6 modules covered: focus-chain/parser (45 tests), diff/unified (26 tests), diff/tracker (33 tests), models/registry (34 tests), question/manager (22 tests), scheduler/scheduler (22 tests), mcp/oauth expanded (+12 tests). Total: +196 tests across 6 new + 1 expanded test files.

### Medium Priority — Some Mocking Required

> **DONE (Sprint B2)** — All 6 medium-priority modules covered: permissions/rules+quote-parser (76 tests), config/credentials+migration+export (64 tests), agent/prompts/system (40 tests), codebase/ranking+graph (49 tests), context/strategies (27 tests), commander/parallel/activity+scheduler (30 tests). Plus NEW agent/metrics.ts source + tests (15 tests). Total: +301 tests across 12 new test files + 1 new source file.

### Low Priority — Integration Tests / Heavy Mocking

| Module | Files to Test | Reason |
|--------|--------------|--------|
| llm/providers/ (13 files) + utils/ (3 files) | Remaining providers + utils | 6 test files exist; rest require HTTP mocking |
| auth/ (8 files) | OAuth flows | Requires HTTP + browser mocking |
| validator/ (9 files) | QA pipeline | Requires filesystem + build tools |
| mcp/ (6 files) | MCP client | **DONE** (Sprint 13: real transport+client+manager, 33 tests) |
| hooks/ (4 files) | Lifecycle hooks | Requires tool execution context |
| git/ (5 files) | Git operations | Requires real git repo |
| ~~lsp/ (4 files)~~ | ~~LSP integration~~ | **DONE** (Sprint 17: full client, transport, manager, queries — 35 tests) |
| tools/ individual (~25 files) | Each tool | Requires filesystem + network |

---

## Feature Gaps

### Missing in Agent System
- [x] **Agent loop tests** — ~~`loop.ts` is the core but untested~~ **DONE** (10 integration tests via mock LLM in `agent/__tests__/agent-pipeline.integration.test.ts`)
- [x] **Subagent tests** — ~~`subagent.ts` manages child agents, no tests~~ **DONE** (task tool spawning + recursion prevention tested)
- [ ] **Prompt variant tests** — 4 variants (Claude, GPT, Gemini, generic), none tested
- [x] **Agent metrics** — ~~No persistent metrics collection~~ **DONE** (`agent/metrics.ts` + 15 tests)
- [x] **Parallel subagents** — ~~Task tool spawns 1 subagent at a time~~ **DONE** (Sprint B4: `tasks` array with semaphore-based concurrency, explore=5, execute=1)
- [x] **Lead-worker auto-routing** — ~~Commander requires manual delegation~~ **DONE** (Sprint 15: flat delegation; Sprint 16: Praxis 3-tier hierarchy with Commander → Leads → Workers, auto-routing via keyword analysis)

### Missing in Tools
- [x] **Tool execution tests** — ~~Individual tool `execute()` methods untested~~ **DONE** (write + edit execute tests already comprehensive; 11 + 12 tests respectively)
- [x] **Browser tool** — ~~Requires Puppeteer mocking~~ Removed (Sprint 13, use Puppeteer MCP server)
- [x] **Apply-patch tests** — ~~Parser and applier untested~~ **DONE** (parser.test.ts, 10 tests)
- [ ] **Edit strategy benchmarks** — No comparison of 8 edit strategies on real diffs

### Missing in Intelligence
- [x] **Codebase indexer tests** — ~~No tests for file discovery or symbol extraction~~ **DONE** (Sprint 11: extension-side indexer with detectLanguage, indexFiles, createRepoMap tests; core-side symbol extraction still untested)
- [ ] **Context strategy benchmarks** — No comparison of compaction strategies

### Missing in Safety
- [ ] **Permission manager tests** — Central `manager.ts` untested
- [x] **Auto-approve tests** — ~~Auto-approval logic untested~~ **DONE** (pre-existing)
- [x] **Rules tests** — ~~Rule definitions untested~~ **DONE** (28 tests)
- [x] **Quote parser tests** — ~~Shell quoting analysis untested~~ **DONE** (48 tests)
- [x] **Security inspector pipeline** — ~~Goose has 3-inspector chain~~ **DONE** (Sprint B5: SecurityInspector + RepetitionInspector + InspectorPipeline + AuditTrail, 73 tests)
- [x] **Container/sandbox execution** — ~~Bash runs on host~~ **DONE** (Sprint B6: Sandbox abstraction, DockerSandbox, NoopSandbox, opt-in `mode: 'docker'`, bash.ts wired with graceful fallback)

### Missing in Infrastructure
- [x] **Config credential tests** — ~~Credential storage untested~~ **DONE** (20 tests)
- [x] **Config migration tests** — ~~Version migration untested~~ **DONE** (24 tests)
- [x] **MCP client tests** — ~~MCP protocol client untested~~ **DONE** (Sprint 10: manager tests 9; Sprint 13: transport, client, manager, extension — 4 test files)
- [x] **Hook executor tests** — ~~Hook execution untested~~ **DONE** (executor.ts tested, 16 tests)
- [x] **SQLite session storage** — ~~Sessions are file-based JSON; Goose uses SQLite for durability + querying~~ **DONE** (Sprint 17: `SessionStorage` interface, `SqliteSessionStorage`, `MemorySessionStorage`, serialization helpers)
- [x] **Visibility metadata** — ~~Compacted messages are fully removed~~ **DONE** (Sprint B3: `MessageVisibility` type, visibility-aware compaction)
- [x] **Auto-compaction threshold** — ~~Fixed strategy~~ **DONE** (Sprint B3: configurable threshold, tested)
- [x] **Tool prefix namespacing** — ~~Flat tool registry risks name collisions~~ **DONE** (Sprint B4: `tools/namespacing.ts` with `mcp__`/`ext__` prefixes, backward-compat lookupTool)
- [x] **Batch parallel tool exec** — ~~Batch tool executes sequentially~~ **DONE** (Sprint B4: task-parallel.ts, Semaphore-based concurrency via Promise.allSettled)

---

## Architecture Debt

### Known Issues
- [ ] **Platform abstraction gaps** — `platform.ts` has different behavior for Node/Tauri/browser but tests only cover Node
- [ ] **Circular dependency risk** — `tools/index.ts` imports from `agent/modes/` (plan tools), creating a cross-module dependency
- [ ] **Large barrel export** — `index.ts` exports 30 modules via `export *`, making tree-shaking harder

### Opportunities
- [ ] **Split tools/ into subcategories** — 44 files in one directory is large; could split into file-tools/, search-tools/, web-tools/
- [ ] **Extract tool utilities** — `utils.ts`, `sanitize.ts`, `truncation.ts`, `locks.ts` could be a separate `tool-utils/` module
- [x] **Standardize provider interface** — ~~Providers implement `stream()` differently; could add a test harness~~ **DONE** (Sprint 10: `providers/_shared/src/test-harness.ts` + anthropic/openai/google tests)

---

## Roadmap Integration

This backlog feeds into the project roadmap:

| Phase | Backend Work |
|-------|-------------|
| **1.5 Polish** (complete) | Settings hardening, appearance system, core wiring, backend tests |
| **2 Plugins** | Plugin SDK shipped (Sprint 10); manifest validation, hot reload, real install remaining |
| **3 CLI** | CLI-specific session storage, config paths, terminal rendering |
| **4 Integrations** | MCP client done (Sprint 13); MCP server hosting, OAuth, resources remaining |

---

---

## Core-v2 / Extensions Test Coverage

### Sprint 11 — Wire All 14 Stub Extensions

> **Sprint 11** wired all 14 stub extensions with real logic, added 12 helper modules, rewrote all tests. Baseline after Sprint 11: **3,524 tests / 200 files** (up from 3,417/188).

#### Completed
- [x] **Build fix** — Excluded `__test-utils__/**` from core-v2 tsconfig, `**/test-harness.ts` from extensions tsconfig
- [x] **Wire all 14 stub extensions** — Each now has real `activate()` logic with tools, commands, middleware, and event handlers
- [x] **12 new helper modules** — `commands.ts`, `registry.ts`, `tracker.ts` (focus-chain), `runner.ts` (scheduler), `tracker.ts` (diff), `loader.ts` (instructions), `matcher.ts` (skills), `snapshots.ts` (git), `parser.ts` (custom-commands), `indexer.ts` (codebase), `runner.ts` (sandbox)
- [x] **24 new/rewritten test files** — 14 updated `index.test.ts` + 10 new helper `*.test.ts`
- [x] **Dead code cleanup** — Deleted `anthropic-oauth.ts`, `SidebarPlugins.tsx`, `ProvidersTab.tsx`
- [x] **MCP manager wired** — `activate()` loads servers from settings, registers tools on connect, handles add/remove events
- [x] **Models registry** — `createModelRegistry()` with Map-based storage, listens for `provider:registered`
- [x] **Instructions loader** — `loadInstructions()` walks directories upward, finds `.ava-instructions`/`CLAUDE.md`, merges by priority

### Sprint 10 — Test Coverage + Plugin Ecosystem

> **Sprint 10** added 102 new tests. Baseline: 3,417 tests / 188 files.

#### Completed
- [x] Mock ExtensionAPI test utility (`createMockExtensionAPI()`) — 9 tests
- [x] Activation tests for all 14 extension modules — 43 tests
- [x] Provider test harness + anthropic/openai/google/copilot provider tests — 12 tests
- [x] MCP manager unit tests (addServer, removeServer, getTools, resetMCP) — 9 tests
- [x] Plugin catalog fetch/cache/fallback tests — 8 tests
- [x] CLI scaffold template tests (manifest, ExtensionAPI source, test generation) — 3 tests
- [x] 5 example plugin tests (timestamp-tool, file-stats, polite-middleware, session-notes, event-logger) — 21 tests
- [x] **GitHub Copilot provider extension** — `packages/extensions/providers/copilot/` with custom `CopilotClient`
- [x] **Copilot model fetcher** — Dynamic model fetch with hardcoded fallback

### Sprint 12 — Agent V2 E2E

> **Sprint 12** wired the full agent-v2 pipeline end-to-end through the CLI: core tools, system prompt, tool approval, subagent task, retry/doom-loop recovery, context management, and rich CLI output.

#### Completed
- [x] **CLI agent-v2 command** — `ava agent-v2 run "goal" --provider --model --yolo --verbose`
- [x] **23 extensions loading** — All built-in extensions activate successfully
- [x] **Output modes** — `--verbose` (tool details), `--json` (NDJSON), default (minimal)
- [x] **Tool approval** — `--yolo` auto-approves; without it, readline prompts for risky tools
- [x] **Extension loader** — Tries source `.ts` first (tsx), falls back to `dist/*.js` (compiled CLI)
- [x] **Platform-tauri build fix** — Missing `@ava/core-v2` symlink + tsconfig paths
- [x] **Extensions package.json exports** — `"exports": { "./*": "./dist/*" }` for CLI subpath resolution

### Sprint 13 — Web Tools Cleanup + Real MCP Client

> **Sprint 13** removed the browser tool, added free DuckDuckGo websearch, and implemented a real MCP client with JSON-RPC 2.0 over stdio/SSE. Net: −888 lines deleted, +1238 lines added across 19 files. Tool count 24 → 23. Extension tests: 609 passing (67 files).

#### Completed
- [x] **Browser tool removed** — Deleted `tools-extended/src/browser/` (4 files, −689 lines), users should use Puppeteer MCP server instead
- [x] **Free websearch** — DuckDuckGo HTML scraping as default (no API key needed), Tavily/Exa as optional fallbacks
- [x] **MCP transport layer** — `StdioTransport` (newline-delimited JSON via platform shell spawn) + `SSETransport` (Server-Sent Events + POST) — `mcp/src/transport.ts` (~190 lines)
- [x] **MCP protocol client** — Initialize handshake (`2024-11-05`), tools/list, tools/call with request/response correlation + 30s timeouts — `mcp/src/client.ts` (~130 lines)
- [x] **MCP manager rewrite** — Real connection lifecycle: connect → initialize → list tools → ready; `callTool()` for execution — `mcp/src/manager.ts` (~120 lines)
- [x] **MCP extension rewrite** — Tools registered with real execution, dynamic add/remove via events — `mcp/src/index.ts` (~100 lines)
- [x] **MCP types update** — Added `env` field to `MCPServer` for passing API keys to stdio servers
- [x] **Tests** — 33 MCP tests (transport 7, client 7, manager 10, extension 9) across 4 test files
- [x] **Smoke tested** — CLI `agent-v2 run` with DuckDuckGo websearch, no API keys, 23 extensions loaded

#### Still Needed (MCP)
- [x] ~~MCP OAuth flows (auth + refresh + storage)~~ **DONE** (Gap Analysis: `mcp/src/oauth.ts` — PKCE code verifier, token exchange, refresh, revoke + 6 tests)
- [x] ~~MCP resources (read/subscribe)~~ **DONE** (Sprint 17: `listResources()`, `readResource()` in client + manager)
- [x] ~~MCP prompts (list/get)~~ **DONE** (Sprint 17: `listPrompts()`, `getPrompt()` in client + manager)
- [x] ~~MCP sampling (server-initiated LLM requests)~~ **DONE** (Sprint 17: `onSamplingRequest()` handler, server→client request routing)
- [x] ~~Reconnection with exponential backoff~~ **DONE** (Gap Analysis: `mcp/src/reconnect.ts` — `ReconnectStrategy` with jitter, max attempts, reset + 7 tests)
- [ ] Tool list change notifications (re-discover on `notifications/tools/list_changed`)
- [ ] Server health monitoring + auto-restart

### Sprint 14 — P1 Competitive Gap Features (Backend)

> **Sprint 14** added live tool progress streaming, undo/redo file changes, and enhanced permissions. +912 lines across 12 backend files. Total: **3,668 tests / 211 files** (up from 3,524/200).

#### Completed
- [x] **Live tool progress streaming** — Agent loop emits `tool:progress` events during execution; 178 new loop tests — `core-v2/src/agent/loop.ts` + `loop.test.ts`
- [x] **Undo/redo file changes** — Diff extension tracks file snapshots, supports undo/redo via middleware; 200 new tests — `extensions/diff/src/index.ts` + `index.test.ts`
- [x] **Enhanced permissions middleware** — Risk level classification, relative path blocking, `PermissionRequest`/`PermissionResponse` types — `extensions/permissions/src/middleware.ts` + `types.ts`
- [x] **IDE integration slash command** — `/open` command to open files in external editor — `extensions/slash-commands/src/commands.ts`
- [x] **Bash tool metadata** — Added working directory to bash tool output — `core-v2/src/tools/bash.ts`

### Sprint 15 — Team Delegation Wiring

> **Sprint 15** wired flat delegation: 5 `delegate_*` tools, `ToolContext.onEvent` for child agents, `AgentEvent` union extended with `delegation:start` + `delegation:complete`. Tool count 23 → 28. Extensions active 23 → 24.

#### Completed
- [x] **5 delegate tools** — `delegate_coder`, `delegate_tester`, `delegate_reviewer`, `delegate_researcher`, `delegate_debugger`
- [x] **Event forwarding** — `ToolContext.onEvent` callback for child agent events (avoids circular dep)
- [x] **Commander extension** — Registers delegate tools + `team` agent mode, settings-gated with try/catch
- [x] **Agent team bridge** — `delegation:start` creates team members, `delegation:complete` updates status
- [x] **Auto mode detection** — `useAgent.ts` + `agent-v2.ts` auto-set `toolMode: 'team'` when available
- [x] **Task tool dedup** — Imports `BUILTIN_WORKERS` from commander instead of duplicating

### Sprint 16 — Praxis 3-Tier Agent Hierarchy

> **Sprint 16** upgraded the flat delegation into a real 3-tier hierarchy: Commander → Leads → Workers. 13 built-in agents, per-agent model/provider overrides, agent registry, planning pipeline, settings bridge with import/export, tier-aware UI. +2850 lines across 25 files. 124 tests across 9 test files.

#### Completed
- [x] **AgentDefinition type** — Unified type bridging frontend `AgentPreset` and backend `WorkerDefinition` — `commander/src/agent-definition.ts`
- [x] **Agent registry** — Central `Map<string, AgentDefinition>` with register/get/filter — `commander/src/registry.ts`
- [x] **13 built-in agents** — 1 Commander + 4 Leads (frontend, backend, QA, fullstack) + 8 Workers (coder, tester, reviewer, researcher, debugger, architect, planner, devops) — `commander/src/workers.ts`
- [x] **Tier-aware delegation** — `resolveTools()` adds delegate tools for leads, strips them from workers — `commander/src/delegate.ts`
- [x] **Praxis agent mode** — Replaces `'team'` mode, Commander only gets delegate + meta tools — `commander/src/index.ts`
- [x] **Planning pipeline** — Planner returns structured `TaskPlan` JSON, topological sort for dependencies — `commander/src/planning.ts`
- [x] **Settings bridge** — `AgentPreset` extended with `tier`, `tools`, `delegates`, `domain`, `provider` — `src/config/defaults/agent-defaults.ts`
- [x] **Settings sync** — Custom agents from Settings UI registered on activation — `commander/src/settings-sync.ts`
- [x] **Import/Export agents** — `exportAgents()` / `importAgents()` with JSON format `{ praxis_agents, version }` — `src/stores/settings/index.ts`
- [x] **Tier-based Settings UI** — AgentsTab groups by Commander/Leads/Workers/Custom with tier badges — `src/components/settings/tabs/AgentsTab.tsx`
- [x] **Enhanced edit modal** — Tier, tools, delegates, domain, provider fields — `src/components/settings/settings-agent-edit-modal.tsx`
- [x] **Import/Export UI wiring** — File download/upload handlers in SettingsModal → SettingsModalContent → AgentsTab
- [x] **Team bridge tier mapping** — `tier: 'lead'` → `'senior-lead'`, `tier: 'worker'` → `'junior-dev'` — `src/hooks/agent/agent-team-bridge.ts`
- [x] **Per-agent model/provider** — Each agent can use different LLM model and provider
- [x] **Documentation** — `docs/praxis.md` (229 lines)
- [x] **Tests** — registry (8), planning (9), settings-sync (5), delegate (16), index (7), workers (51) = 96 new/rewritten tests

### Gap Analysis Sprint — Backend Extensions (2026-02-28)

> Added alongside the frontend gap analysis. New backend extension modules, tests, and enhancements.

#### Completed
- [x] **Session storage abstraction** — `SessionStorage` interface + `MemorySessionStorage` + `SQLiteSessionStorage` + serialization utils — `core-v2/src/session/storage.ts`, `sqlite-storage.ts`, `memory-storage.ts` (4 test files)
- [x] **LSP client** — Full LSP client with initialize/hover/definition/references/completion + server manager + transport + query utils — `extensions/lsp/src/client.ts`, `server-manager.ts`, `transport.ts`, `queries.ts` (4 test files)
- [x] **Symbol extractor** — Regex-based symbol extraction for 7 languages (TS/JS/Python/Rust/Go/Java/C++) — `extensions/codebase/src/symbol-extractor.ts` (1 test file)
- [x] **Memory extension** — Persistent cross-session memory store with `memory_read`/`memory_write`/`memory_list` tools — `extensions/memory/` (3 source files, 3 test files)
- [x] **MCP OAuth** — PKCE flow with code verifier, token exchange, refresh, revoke — `extensions/mcp/src/oauth.ts` (1 test file)
- [x] **MCP reconnect** — Exponential backoff with jitter, max attempts, reset — `extensions/mcp/src/reconnect.ts` (1 test file)
- [x] **Provider tests** — 10 remaining providers tested (cohere, deepseek, glm, groq, kimi, mistral, ollama, openrouter, together, xai) — 10 test files
- [x] **Validator tests** — Extension activation + validation pipeline — 1 test file
- [x] **MCP types expanded** — OAuth, resources, prompts, sampling, notifications type definitions — `extensions/mcp/src/types.ts`

### Sprint 17 — Backend Completion (All 9 "What's Next" Items)

> **Sprint 17** completed all remaining backend items. 7 phases delivered: validator/focus-chain wiring, provider tests, SQLite session storage, persistent memory extension, MCP advanced features, symbol extraction, full LSP client. +271 tests across 39 files. Tool count 28 → 35. Extensions 24 → 25. Total: **~3,896 tests across ~250 files**.

#### Completed
- [x] **Phase A: Quick fixes** — Validator `enabledByDefault: true` + `agent:completing` handler wired. Focus-chain event names fixed (`turn:start`/`turn:end`/`agent:finish`). Diff tracking already working.
- [x] **Phase B: Provider tests** — 10 providers tested (deepseek, mistral, groq, xai, cohere, together, ollama, glm, kimi, openrouter) via shared test harness.
- [x] **Phase C: SQLite session storage** — `SessionStorage` interface, `MemorySessionStorage`, `SqliteSessionStorage`, serialization helpers, auto-save timer. 18 new tests.
- [x] **Phase D: Persistent memory extension** — `MemoryStore` (CRUD + categories), 4 tools (`memory_write`/`memory_read`/`memory_list`/`memory_delete`), system prompt injection via `prompt:build`. 19 new tests.
- [x] **Phase E: MCP advanced** — Resources (`listResources`/`readResource`), prompts (`listPrompts`/`getPrompt`), sampling (server→client), reconnection (exponential backoff + jitter), OAuth (auth code flow + token refresh), transport `onError`/`onClose`. 17 new tests.
- [x] **Phase F: Symbol extraction** — Regex-based extractor for TS/JS, Python, Rust, Go. `/symbols` command. 12 new tests.
- [x] **Phase G: Full LSP client** — Content-Length framed transport, `LSPClient` (initialize/hover/definition/references/diagnostics), `LSPServerManager` (per-language lifecycle), `formatHover`/`formatLocations`/`formatDiagnostics`. 3 tools (`lsp_diagnostics`/`lsp_hover`/`lsp_definition`). 35 new tests.

### Still Needed
- [ ] Real plugin install/uninstall (currently state-only in frontend)
- [ ] Tool list change notifications (re-discover on `notifications/tools/list_changed`)
- [ ] Server health monitoring + auto-restart
- [ ] Background shell management (bash_background, bash_kill)
- [ ] Tool result truncation (>50K chars)
- [ ] Image/vision support in agent loop
- [ ] Tauri bridge for core-v2 (desktop integration)

*Last updated: 2026-03-02 — historical Sprint 17 completion metrics (271 new tests). Current snapshot: 55+ tools, 30+ extensions, 16 providers.*
