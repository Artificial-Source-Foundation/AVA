# Backend Test Coverage

> Latest verified baseline: **2687 tests** across **109 test files**. Overall file coverage: ~43% (85/200 testable files).
>
> Strategy: Test pure functions and stateful classes. Skip LLM/FS/HTTP-dependent code.

---

## Summary

| Metric | Value |
|--------|-------|
| Total tests | 2687 |
| Test files | 109 |
| Source files (excluding barrels/types) | ~200 |
| Files with tests | 85 |
| Files without tests | 115 |
| File coverage | ~43% |
| TS errors | 0 |
| Biome errors | 0 |

## Recently Delivered (Sprint B4)

### OAuth (Frontend + Core Routing)
- `src/services/auth/oauth-flow.test.ts` — automated auth flow edge-case coverage
- `src/services/auth/oauth.test.ts` and `packages/core/src/llm/client.test.ts` — credential routing, reconnect/clear behavior, and provider path validation
- Integration behavior for OpenAI/Copilot/Anthropic routing is now covered in automated suites

### Message Flow (Frontend)
- `src/hooks/useChat.integration.test.ts` — queue/steer/cancel behavior and stream state transitions
- `src/components/chat/ChatView.integration.test.tsx` — watcher-triggered AI comment -> auto-send with metadata assertions
- `src/components/settings/tabs/PluginsTab.smoke.test.tsx` + `packages/core/src/extensions/manager.test.ts` — plugin baseline smoke/regression checks

## Current Gaps (Audit)

- Manual OAuth desktop runtime matrix is still pending (provider-by-provider connect/disconnect/send validation)
- Logging hardening remains in progress across chat/agent/core-bridge/session/settings/file-watcher surfaces

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
| agent/ | 7 | 10 | 70% | metrics.ts NEW. Missing: subagent unit, types |
| context/strategies/ | 4 | 7 | 57% | strategies.test.ts covers sliding-window, hierarchical, summarize |
| commander/parallel/ | 4 | 5 | 80% | activity.ts + scheduler.ts tested |
| policy/ | 2 | 5 | 40% | Missing: rules, types |
| focus-chain/ | 1 | 4 | 25% | parser.ts tested (45 tests) |
| diff/ | 2 | 4 | 50% | unified.ts (26 tests) + tracker.ts (33 tests) |
| models/ | 1 | 3 | 33% | registry.ts tested (34 tests) |
| question/ | 1 | 3 | 33% | manager.ts tested (22 tests) |
| scheduler/ | 1 | 3 | 33% | scheduler.ts tested (22 tests) |
| mcp/ | 1 | 6 | 17% | oauth.ts tested (27 tests) |
| bus/ | 1 | 3 | 33% | message-bus tested |
| tools/ | 13 | 37 | 35% | Utilities + namespacing + task-parallel + sandbox tested |

### No Coverage (0%)

| Module | Files | Why |
|--------|-------|-----|
| llm/providers/ | 14 | Integration test territory (real HTTP) |
| auth/ | 8 | OAuth flows require real HTTP |
| codebase/ | 9 | Partially tested (ranking, graph). Indexer/symbols require filesystem |
| validator/ | 9 | Requires real filesystem (lint, build, test) |
| hooks/ | 4 | executor.ts tested (16 tests). Remaining require real tool execution |
| lsp/ | 4 | Requires language servers |
| git/ | 4 | Requires real git repository |
| skills/ | 4 | Requires filesystem |
| slash-commands/ | 3 | Requires filesystem |
| instructions/ | 3 | Requires filesystem |
| integrations/ | 2 | Requires HTTP (Exa API) |
| agent/prompts/ | 6 | system.ts tested (40 tests), variant files low priority |

---

## Tested Files (76 total)

### agent/ (8 test files)
- `evaluator.test.ts` — 35 tests (progress, goals, tool usage, metrics)
- `events.test.ts` — 53 tests (emitter, buffer, filtering, stats)
- `recovery.test.ts` — 107 tests (error classification, backoff, retry, manager)
- `planner.test.ts` — 32 tests (error classification, constructor)
- `modes/plan.test.ts` — 36 tests (state, restrictions, enter/exit tools)
- `__tests__/agent-pipeline.integration.test.ts` — 10 tests (tool dispatch, MAX_TURNS, NO_COMPLETE_TASK, doom loop, abort, event ordering, filtered tools, worker execution, subagent spawning, recursion prevention)
- `metrics.test.ts` — 15 tests (record events, turns, tokens, tool calls, errors, recoveries, duration, export, singleton)
- `prompts/system.test.ts` — 40 tests (RULES/CAPABILITIES constants, buildSystemPrompt, buildWorkerPrompt, buildScenarioPrompt, getModelAdjustments)

### tools/ (14 test files)
- `utils.test.ts` — 95 tests (binary detection, path resolution, glob, skip dirs)
- `sanitize.test.ts` — 76 tests (model families, fence stripping, normalization)
- `truncation.test.ts` — 22 tests (line/metadata truncation)
- `locks.test.ts` — 33 tests (file locking, collision, cleanup)
- `completion.test.ts` — 31 tests (completion state, formatting)
- `validation.test.ts` — 31 tests (Zod helpers)
- `define.test.ts` — 40 tests (tool factory, permissions, locations)
- `todo.test.ts` — 19 tests (todo state management)
- `edit-replacers.test.ts` — 65 tests (levenshtein, similarity, replacers)
- `namespacing.test.ts` — 29 tests (namespace, strip, lookup, MCP/ext helpers)
- `task-parallel.test.ts` — 37 tests (concurrency constants, tasks validation, maxConcurrent, schema, dispatch)
- `sandbox/sandbox.test.ts` — 25 tests (config defaults, Docker args, NoopSandbox, factory, edge cases)
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

### config/ (5 test files)
- `manager.test.ts`, `schema.test.ts`
- `credentials.test.ts` — 20 tests (key operations, provider listing, validation, singleton)
- `migration.test.ts` — 24 tests (migrateSettings, mergeWithDefaults, findEnvApiKeys, needsMigration, getChangedFields)
- `export.test.ts` — 20 tests (exportSettingsToJson, importSettingsFromJson, mergeSettings, diffSettings, getDefaultSettingsJson)

### context/ (6 test files)
- `compactor.test.ts`, `tracker.test.ts`
- `strategies/split-point.test.ts`, `strategies/tool-truncation.test.ts`, `strategies/verified-summarize.test.ts`
- `strategies/strategies.test.ts` — 27 tests (slidingWindow, createSlidingWindow, buildSummaryTree, selectLevel, createSummarize, getSummarizationPrompt, extractSummary)

### session/ (4 test files)
- `manager.test.ts`, `file-storage.test.ts`, `resume.test.ts`, `doom-loop.test.ts`

### permissions/ (8 test files)
- `command-validator.test.ts`, `trusted-folders.test.ts`
- `rules.test.ts` — 28 tests (BUILTIN_RULES, assessCommandRisk, assessPathRisk, getHighestPathRisk)
- `quote-parser.test.ts` — 48 tests (createQuoteState, processChar, isInsideQuotes, isInSafeContext, detectDangerousCharacters, parseCommandSegments, detectRedirects, extractSubshells)
- `security-inspector.test.ts` — 25 tests (threat categories, pattern matching, custom patterns, block threshold)
- `repetition-inspector.test.ts` — 14 tests (threshold, windowing, params hashing, config)
- `inspector-pipeline.test.ts` — 19 tests (pipeline flow, blocking, adapters, audit, factory)
- `audit.test.ts` — 15 tests (record, query filters, export, clear, maxEntries, singleton)

### policy/ (2 test files)
- `engine.test.ts`, `matcher.test.ts`

### custom-commands/ (3 test files)
- `discovery.test.ts`, `parser.test.ts`, `template.test.ts`

### extensions/ (3 test files)
- `manager.test.ts`, `manifest.test.ts`, `storage.test.ts`

### focus-chain/ (1 test file)
- `parser.test.ts` — 45 tests (parse, serialize, update, add, remove, progress, next task)

### diff/ (2 test files)
- `unified.test.ts` — 26 tests (createDiff, parseDiffHunks, getDiffStats, hasChanges, extractPaths, formatDiffLines)
- `tracker.test.ts` — 33 tests (add, apply, reject, queries, bulk ops, events, singleton)

### models/ (1 test file)
- `registry.test.ts` — 34 tests (lookup, query, pricing, validation, suggestions)

### question/ (1 test file)
- `manager.test.ts` — 22 tests (ask/answer, cancel, timeout, queries, events, factory)

### scheduler/ (1 test file)
- `scheduler.test.ts` — 22 tests (register, start/stop, runNow, concurrency, callbacks, factory)

### codebase/ (2 test files)
- `ranking.test.ts` — 25 tests (calculatePageRank, calculateRelevanceScore, extractKeywords, sortByRank, sortByScore)
- `graph.test.ts` — 24 tests (getEdges, findRoots, findLeaves, findCircularDependencies, getDependencyDepth, getTransitiveDependencies, getTransitiveDependents, getGraphStats)

### commander/parallel/ (2 test files added)
- `activity.test.ts` — 17 tests (ActivityMultiplexer, createTaggedCallback, createFilteredCallback, createAggregator)
- `scheduler.test.ts` — 13 tests (TaskScheduler, createLinearChain, createFanOut, createFanIn)

### bus/ (1 test file)
- `message-bus.test.ts`

### a2a/ (5 test files)
- `server.test.ts`, `streaming.test.ts`, `task.test.ts`, `auth.test.ts`, `agent-card.test.ts`

### acp/ (5 test files)
- `terminal.test.ts`, `session-store.test.ts`, `mcp-bridge.test.ts`, `error-handler.test.ts`, `mode.test.ts`

---

## What's Testable But Not Tested (Priority Candidates)

> **DONE (Sprint B2)** — All 6 medium-priority modules covered. See below.

| Module | Testable Files | Status |
|--------|---------------|--------|
| ~~agent/prompts/~~ | ~~system.ts, variants/*~~ | **DONE** (system.ts tested, 40 tests) |
| ~~codebase/~~ | ~~ranking.ts, graph.ts~~ | **DONE** (49 tests) |
| ~~permissions/~~ | ~~rules.ts, auto-approve.ts, quote-parser.ts~~ | **DONE** (76 tests + existing auto-approve) |
| ~~config/~~ | ~~credentials.ts, migration.ts, export.ts~~ | **DONE** (64 tests) |
| ~~context/strategies/~~ | ~~hierarchical.ts, sliding-window.ts, summarize.ts~~ | **DONE** (27 tests) |
| ~~commander/parallel/~~ | ~~activity.ts, scheduler.ts~~ | **DONE** (30 tests) |

---

*Last updated: 2026-02-14*
