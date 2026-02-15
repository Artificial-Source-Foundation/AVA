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

| Module | Files to Test | Estimated Tests | Effort |
|--------|--------------|-----------------|--------|
| focus-chain/ | parser.ts, manager.ts | ~30 | Low |
| diff/ | unified.ts, tracker.ts | ~25 | Low |
| models/ | registry.ts | ~20 | Low |
| question/ | manager.ts | ~15 | Low |
| scheduler/ | scheduler.ts | ~15 | Low |
| **Subtotal** | | **~105** | **Low** |

### Medium Priority — Some Mocking Required

| Module | Files to Test | Estimated Tests | Effort |
|--------|--------------|-----------------|--------|
| agent/prompts/ | system.ts + 4 variants | ~40 | Medium |
| codebase/ | ranking.ts, graph.ts, imports.ts | ~50 | Medium |
| permissions/ | rules.ts, auto-approve.ts, quote-parser.ts | ~40 | Medium |
| config/ | credentials.ts, migration.ts, export.ts | ~35 | Medium |
| context/strategies/ | hierarchical.ts, sliding-window.ts, summarize.ts | ~30 | Medium |
| commander/parallel/ | activity.ts, scheduler.ts | ~25 | Medium |
| **Subtotal** | | **~220** | **Medium** |

### Low Priority — Integration Tests / Heavy Mocking

| Module | Files to Test | Reason |
|--------|--------------|--------|
| llm/providers/ (14 files) | All providers | Requires HTTP mocking per provider |
| auth/ (8 files) | OAuth flows | Requires HTTP + browser mocking |
| validator/ (9 files) | QA pipeline | Requires filesystem + build tools |
| mcp/ (6 files) | MCP client | Requires MCP server |
| hooks/ (4 files) | Lifecycle hooks | Requires tool execution context |
| git/ (4 files) | Git operations | Requires real git repo |
| lsp/ (4 files) | LSP integration | Requires language servers |
| tools/ individual (25 files) | Each tool | Requires filesystem + network |

---

## Feature Gaps

### Missing in Agent System
- [ ] **Agent loop tests** — `loop.ts` is the core but untested (requires LLM mocking)
- [ ] **Subagent tests** — `subagent.ts` manages child agents, no tests
- [ ] **Prompt variant tests** — 4 variants (Claude, GPT, Gemini, generic), none tested
- [ ] **Agent metrics** — No persistent metrics collection (turns, tokens, duration per session)

### Missing in Tools
- [ ] **Tool execution tests** — Individual tool `execute()` methods untested
- [ ] **Browser tool tests** — Requires Puppeteer mocking
- [ ] **Apply-patch tests** — Parser and applier untested
- [ ] **Edit strategy benchmarks** — No comparison of 8 edit strategies on real diffs

### Missing in Intelligence
- [ ] **Codebase indexer tests** — No tests for file discovery or symbol extraction
- [ ] **Memory embedding tests** — `embedding.ts` untested (OpenAI API dependent)
- [ ] **Memory store tests** — `store.ts` (SQLite) untested
- [ ] **Context strategy benchmarks** — No comparison of compaction strategies

### Missing in Safety
- [ ] **Permission manager tests** — Central `manager.ts` untested
- [ ] **Auto-approve tests** — Auto-approval logic untested
- [ ] **Rules tests** — Rule definitions untested
- [ ] **Quote parser tests** — Shell quoting analysis untested

### Missing in Infrastructure
- [ ] **Config credential tests** — Credential storage untested
- [ ] **Config migration tests** — Version migration untested
- [ ] **MCP client tests** — MCP protocol client untested
- [ ] **Hook executor tests** — Hook execution untested

---

## Architecture Debt

### Known Issues
- [x] ~~**Export collisions** — `a2a` and `policy` modules need named exports~~ (Fixed in Session 43)
- [ ] **Platform abstraction gaps** — `platform.ts` has different behavior for Node/Tauri/browser but tests only cover Node
- [ ] **Circular dependency risk** — `tools/index.ts` imports from `agent/modes/` (plan tools), creating a cross-module dependency
- [ ] **Large barrel export** — `index.ts` exports 33 modules via `export *`, making tree-shaking harder

### Opportunities
- [ ] **Split tools/ into subcategories** — 37 files in one directory is large; could split into file-tools/, search-tools/, web-tools/
- [ ] **Extract tool utilities** — `utils.ts`, `sanitize.ts`, `truncation.ts`, `locks.ts` could be a separate `tool-utils/` module
- [ ] **Standardize provider interface** — All 14 LLM providers implement `stream()` differently; could add a test harness
- [ ] **Memory consolidation scheduling** — `ConsolidationEngine` exists but isn't wired to run automatically

---

## Roadmap Integration

This backlog feeds into the project roadmap:

| Phase | Backend Work |
|-------|-------------|
| **1.5 Polish** (complete) | Settings hardening, appearance system, core wiring, backend tests |
| **2 Plugins** | Extension system needs: manifest validation, hot reload, sandboxing |
| **3 CLI** | CLI-specific session storage, config paths, terminal rendering |
| **4 Integrations** | ACP terminal wiring, A2A server deployment, MCP server hosting |

---

*Last updated: 2026-02-14*
