# Backend Test Coverage

> 1778 tests across 64 test files. Overall file coverage: ~30% (56/188 testable files).
>
> Strategy: Test pure functions and stateful classes. Skip LLM/FS/HTTP-dependent code.

---

## Summary

| Metric | Value |
|--------|-------|
| Total tests | 1778 |
| Test files | 64 |
| Source files (excluding barrels/types) | ~188 |
| Files with tests | 56 |
| Files without tests | 132 |
| File coverage | ~30% |
| TS errors | 0 |
| Biome errors | 0 |

---

## Coverage by Module

### Well Tested (60%+)

| Module | Tested | Total | Coverage | Notes |
|--------|--------|-------|----------|-------|
| commander/workers/ | 1 | 1 | 100% | Worker definitions fully tested |
| a2a/ | 5 | 7 | 71% | Missing: types.ts (type-only) |
| acp/ | 5 | 7 | 71% | Missing: types.ts (type-only) |
| session/ | 4 | 6 | 67% | Missing: types.ts |
| commander/ | 4 | 6 | 67% | Missing: types.ts |
| extensions/ | 3 | 5 | 60% | Missing: types.ts |

### Moderate Coverage (30-59%)

| Module | Tested | Total | Coverage | Notes |
|--------|--------|-------|----------|-------|
| memory/ | 5 | 10 | 50% | Missing: embedding, store |
| context/ | 2 | 4 | 50% | Missing: types |
| custom-commands/ | 3 | 6 | 50% | Missing: loader, types |
| llm/ | 1 | 2 | 50% | Client tested, not providers |
| agent/ | 4 | 9 | 44% | Missing: loop, subagent, types |
| context/strategies/ | 3 | 7 | 43% | Missing: hierarchical, sliding-window, summarize |
| commander/parallel/ | 2 | 5 | 40% | Missing: activity, scheduler |
| policy/ | 2 | 5 | 40% | Missing: rules, types |
| bus/ | 1 | 3 | 33% | message-bus tested |
| tools/ | 10 | 35 | 29% | Utilities tested, individual tools not tested |

### No Coverage (0%)

| Module | Files | Why |
|--------|-------|-----|
| llm/providers/ | 14 | Integration test territory (real HTTP) |
| auth/ | 8 | OAuth flows require real HTTP |
| codebase/ | 11 | Requires real filesystem + tree-sitter |
| validator/ | 9 | Requires real filesystem (lint, build, test) |
| mcp/ | 6 | Requires MCP server connections |
| hooks/ | 4 | Requires real tool execution |
| lsp/ | 4 | Requires language servers |
| focus-chain/ | 4 | Pure functions, good candidate for testing |
| git/ | 4 | Requires real git repository |
| diff/ | 4 | Some pure functions testable |
| skills/ | 4 | Requires filesystem |
| slash-commands/ | 3 | Requires filesystem |
| instructions/ | 3 | Requires filesystem |
| models/ | 3 | Registry is testable |
| question/ | 3 | Simple state management |
| scheduler/ | 3 | Testable with mocks |
| integrations/ | 2 | Requires HTTP (Exa API) |
| agent/prompts/ | 7 | Template strings, testable but low priority |

---

## Tested Files (56 total)

### agent/ (5 test files)
- `evaluator.test.ts` — 35 tests (progress, goals, tool usage, metrics)
- `events.test.ts` — 53 tests (emitter, buffer, filtering, stats)
- `recovery.test.ts` — 107 tests (error classification, backoff, retry, manager)
- `planner.test.ts` — 32 tests (error classification, constructor)
- `modes/plan.test.ts` — 36 tests (state, restrictions, enter/exit tools)

### tools/ (11 test files)
- `utils.test.ts` — 95 tests (binary detection, path resolution, glob, skip dirs)
- `sanitize.test.ts` — 76 tests (model families, fence stripping, normalization)
- `truncation.test.ts` — 22 tests (line/metadata truncation)
- `locks.test.ts` — 33 tests (file locking, collision, cleanup)
- `completion.test.ts` — 31 tests (completion state, formatting)
- `validation.test.ts` — 31 tests (Zod helpers)
- `define.test.ts` — 40 tests (tool factory, permissions, locations)
- `todo.test.ts` — 19 tests (todo state management)
- `edit-replacers.test.ts` — 65 tests (levenshtein, similarity, replacers)
- `edit/normalize.test.ts` — existing tests

### llm/ (1 test file)
- `client.test.ts` — 31 tests (registry, factory, credential resolution)

### commander/ (7 test files)
- `executor.test.ts`, `registry.test.ts`, `tool-wrapper.test.ts`, `utils.test.ts`
- `parallel/batch.test.ts`, `parallel/conflict.test.ts`
- `workers/definitions.test.ts`

### memory/ (6 test files)
- `manager.test.ts`, `episodic.test.ts`, `semantic.test.ts`
- `procedural.test.ts`, `consolidation.test.ts`

### config/ (2 test files)
- `manager.test.ts`, `schema.test.ts`

### context/ (5 test files)
- `compactor.test.ts`, `tracker.test.ts`
- `strategies/split-point.test.ts`, `strategies/tool-truncation.test.ts`, `strategies/verified-summarize.test.ts`

### session/ (4 test files)
- `manager.test.ts`, `file-storage.test.ts`, `resume.test.ts`, `doom-loop.test.ts`

### permissions/ (2 test files)
- `command-validator.test.ts`, `trusted-folders.test.ts`

### policy/ (2 test files)
- `engine.test.ts`, `matcher.test.ts`

### custom-commands/ (3 test files)
- `discovery.test.ts`, `parser.test.ts`, `template.test.ts`

### extensions/ (3 test files)
- `manager.test.ts`, `manifest.test.ts`, `storage.test.ts`

### bus/ (1 test file)
- `message-bus.test.ts`

### a2a/ (5 test files)
- `server.test.ts`, `streaming.test.ts`, `task.test.ts`, `auth.test.ts`, `agent-card.test.ts`

### acp/ (5 test files)
- `terminal.test.ts`, `session-store.test.ts`, `mcp-bridge.test.ts`, `error-handler.test.ts`, `mode.test.ts`

---

## What's Testable But Not Tested (Priority Candidates)

These modules have pure functions that could be unit tested:

| Module | Testable Files | Effort |
|--------|---------------|--------|
| focus-chain/ | parser.ts, manager.ts | Low |
| diff/ | unified.ts, tracker.ts | Low |
| models/ | registry.ts | Low |
| question/ | manager.ts | Low |
| scheduler/ | scheduler.ts | Low |
| agent/prompts/ | system.ts, variants/* | Medium |
| codebase/ | ranking.ts, graph.ts | Medium |
| permissions/ | rules.ts, auto-approve.ts, quote-parser.ts | Medium |
| config/ | credentials.ts, migration.ts, export.ts | Medium |

---

*Last updated: 2026-02-08*
