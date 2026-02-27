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
- PI Coding Agent parity items (provider switching, session branching tree, minimal tool mode, runtime skill creation)
- MCP OAuth flows (auth + refresh + storage)
- Remote browser support baseline

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
| mcp/ (6 files) | MCP client | Requires MCP server |
| hooks/ (4 files) | Lifecycle hooks | Requires tool execution context |
| git/ (5 files) | Git operations | Requires real git repo |
| lsp/ (4 files) | LSP integration | Requires language servers |
| tools/ individual (~25 files) | Each tool | Requires filesystem + network |

---

## Feature Gaps

### Missing in Agent System
- [x] **Agent loop tests** — ~~`loop.ts` is the core but untested~~ **DONE** (10 integration tests via mock LLM in `agent/__tests__/agent-pipeline.integration.test.ts`)
- [x] **Subagent tests** — ~~`subagent.ts` manages child agents, no tests~~ **DONE** (task tool spawning + recursion prevention tested)
- [ ] **Prompt variant tests** — 4 variants (Claude, GPT, Gemini, generic), none tested
- [x] **Agent metrics** — ~~No persistent metrics collection~~ **DONE** (`agent/metrics.ts` + 15 tests)
- [x] **Parallel subagents** — ~~Task tool spawns 1 subagent at a time~~ **DONE** (Sprint B4: `tasks` array with semaphore-based concurrency, explore=5, execute=1)
- [ ] **Lead-worker auto-routing** — Commander requires manual delegation; Goose auto-routes to developer/researcher/data-analyst

### Missing in Tools
- [x] **Tool execution tests** — ~~Individual tool `execute()` methods untested~~ **DONE** (write + edit execute tests already comprehensive; 11 + 12 tests respectively)
- [ ] **Browser tool tests** — Requires Puppeteer mocking
- [ ] **Apply-patch tests** — Parser and applier untested
- [ ] **Edit strategy benchmarks** — No comparison of 8 edit strategies on real diffs

### Missing in Intelligence
- [ ] **Codebase indexer tests** — No tests for file discovery or symbol extraction
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
- [x] **MCP client tests** — ~~MCP protocol client untested~~ **DONE** (Sprint 10: MCP manager tests — addServer, removeServer, getTools, resetMCP — 9 tests)
- [x] **Hook executor tests** — ~~Hook execution untested~~ **DONE** (executor.ts tested, 16 tests)
- [ ] **SQLite session storage** — Sessions are file-based JSON; Goose uses SQLite for durability + querying
- [x] **Visibility metadata** — ~~Compacted messages are fully removed~~ **DONE** (Sprint B3: `MessageVisibility` type, visibility-aware compaction)
- [x] **Auto-compaction threshold** — ~~Fixed strategy~~ **DONE** (Sprint B3: configurable threshold, tested)
- [x] **Tool prefix namespacing** — ~~Flat tool registry risks name collisions~~ **DONE** (Sprint B4: `tools/namespacing.ts` with `mcp__`/`ext__` prefixes, backward-compat lookupTool)
- [x] **Batch parallel tool exec** — ~~Batch tool executes sequentially~~ **DONE** (Sprint B4: task-parallel.ts, Semaphore-based concurrency via Promise.allSettled)

---

## Architecture Debt

### Known Issues
- [ ] **Platform abstraction gaps** — `platform.ts` has different behavior for Node/Tauri/browser but tests only cover Node
- [ ] **Circular dependency risk** — `tools/index.ts` imports from `agent/modes/` (plan tools), creating a cross-module dependency
- [ ] **Large barrel export** — `index.ts` exports 29 modules via `export *`, making tree-shaking harder

### Opportunities
- [ ] **Split tools/ into subcategories** — 43 files in one directory is large; could split into file-tools/, search-tools/, web-tools/
- [ ] **Extract tool utilities** — `utils.ts`, `sanitize.ts`, `truncation.ts`, `locks.ts` could be a separate `tool-utils/` module
- [x] **Standardize provider interface** — ~~All 14 LLM providers implement `stream()` differently; could add a test harness~~ **DONE** (Sprint 10: `providers/_shared/src/test-harness.ts` + anthropic/openai/google tests)

---

## Roadmap Integration

This backlog feeds into the project roadmap:

| Phase | Backend Work |
|-------|-------------|
| **1.5 Polish** (complete) | Settings hardening, appearance system, core wiring, backend tests |
| **2 Plugins** | Plugin SDK shipped (Sprint 10); manifest validation, hot reload, real install remaining |
| **3 CLI** | CLI-specific session storage, config paths, terminal rendering |
| **4 Integrations** | MCP server hosting, external tool connectors |

---

---

## Core-v2 / Extensions Test Coverage (Sprint 10)

> **Sprint 10** added 102 new tests for the new stack. Total: 3,417 tests / 188 files.

### Completed
- [x] Mock ExtensionAPI test utility (`createMockExtensionAPI()`) — 9 tests
- [x] Activation tests for all 14 stub extension modules — 43 tests
- [x] Provider test harness + anthropic/openai/google/copilot provider tests — 12 tests
- [x] MCP manager unit tests (addServer, removeServer, getTools, resetMCP) — 9 tests
- [x] Plugin catalog fetch/cache/fallback tests — 8 tests
- [x] CLI scaffold template tests (manifest, ExtensionAPI source, test generation) — 3 tests
- [x] 5 example plugin tests (timestamp-tool, file-stats, polite-middleware, session-notes, event-logger) — 21 tests

### Still Needed
- [ ] Wire 14 stub extensions to actually register tools/commands/modes (currently only log)
- [ ] Create `models/src/registry.ts` and `instructions/src/loader.ts` modules
- [ ] Connect MCP manager to `activate()` in mcp extension
- [ ] Provider tests for remaining 10 providers (same harness pattern)
- [ ] Real plugin install/uninstall (currently state-only in frontend)

### Recently Added
- [x] **GitHub Copilot provider extension** — `packages/extensions/providers/copilot/` with custom `CopilotClient` (OAuth via `getAuth()`, `Copilot-Integration-Id` header, model prefix stripping), activation, and test harness (3 tests)
- [x] **Copilot model fetcher** — Dynamic model fetch from `https://api.githubcopilot.com/models` with hardcoded fallback (gpt-4.1, gpt-4o, claude-3.5-sonnet, o3-mini); `supportsDynamicFetch()` updated

*Last updated: 2026-02-27*
