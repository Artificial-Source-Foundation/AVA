# Backend Test Coverage

> Latest verified baseline: **~2576 tests** across **~110 test files**. Overall file coverage: ~46%.
>
> Strategy: Test pure functions and stateful classes. Skip LLM/FS/HTTP-dependent code.

---

## Summary

| Metric | Value |
|--------|-------|
| Total tests | ~2576 |
| Test files | ~110 |
| Source files (total) | ~237 |
| Source lines | ~54,500 |
| File coverage | ~46% |
| TS errors | 0 |
| Biome errors | 0 |

## Recently Delivered (Session 55)

### Logger Module (refactored)
- `packages/core/src/logger/logger.test.ts` — AvaLogger singleton, createLogger source-scoped console logger, agent event mapping, NDJSON file output, configuration

### Auth Tests
- `packages/core/src/auth/manager.test.ts` — Auth manager operations
- `packages/core/src/auth/pkce.test.ts` — PKCE code challenge and verifier generation

### Git Utils Tests
- `packages/core/src/git/utils.test.ts` — Git utility functions

### Validator Unit Tests
- `packages/core/src/validator/pipeline.test.ts` — ValidationPipeline orchestration, sequential execution, error handling
- `packages/core/src/validator/syntax.test.ts` — Syntax validator (parse error detection)
- `packages/core/src/validator/typescript.test.ts` — TypeScript validator (type checking)

## Recently Delivered (Sprint B7-B10)

### Validation Pipeline Integration (B7)
- `packages/core/src/agent/__tests__/agent-validation.integration.test.ts` — 5 tests (validation on completion, failure feedback, skip when disabled, skip when no files, retry limit)

### Minimal Tool Mode (B8)
- `packages/core/src/agent/modes/minimal.test.ts` — 13 tests (state management, tool access, per-session isolation, allowed tools list)

### Mid-Session Provider Switching (B9)
- `packages/core/src/agent/__tests__/agent-provider-switch.test.ts` — 4 tests (switch via requestProviderSwitch, history preserved, events emitted, invalid provider)

### Lead-Worker Auto-Routing (B10)
- `packages/core/src/commander/router.test.ts` — 26 tests (analyzeTask keyword matching, confidence, code path detection, selectWorker scoring)

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
| session/ | 4 | 6 | 67% | Missing: types.ts |
| commander/ | 5 | 7 | 71% | +router.ts (auto-routing). Missing: types.ts |
| extensions/ | 3 | 5 | 60% | Missing: types.ts |

### Moderate Coverage (30-59%)

| Module | Tested | Total | Coverage | Notes |
|--------|--------|-------|----------|-------|
| context/ | 3 | 4 | 75% | auto-compaction, compactor, tracker tested. Missing: types |
| custom-commands/ | 3 | 6 | 50% | Missing: loader, types |
| llm/ | 6 | 19 | 32% | Client + 2 providers (openai, openai-compat) + 3 utils tested |
| agent/ | 10 | 11 | 91% | +minimal mode, validation integration, provider switch. Missing: subagent unit |
| context/strategies/ | 5 | 8 | 63% | strategies.test.ts covers sliding-window, hierarchical, summarize + visibility |
| commander/parallel/ | 4 | 5 | 80% | activity.ts + scheduler.ts tested |
| policy/ | 2 | 5 | 40% | Missing: rules, types |
| focus-chain/ | 1 | 4 | 25% | parser.ts tested (45 tests) |
| diff/ | 2 | 4 | 50% | unified.ts (26 tests) + tracker.ts (33 tests) |
| models/ | 1 | 3 | 33% | registry.ts tested (34 tests) |
| question/ | 1 | 3 | 33% | manager.ts tested (22 tests) |
| scheduler/ | 1 | 3 | 33% | scheduler.ts tested (22 tests) |
| auth/ | 2 | 8 | 25% | manager.ts + pkce.ts tested. OAuth flows require real HTTP |
| validator/ | 3 | 9 | 33% | pipeline.ts + syntax.ts + typescript.ts unit tested. Also integration tested via agent-validation.integration.test.ts |
| git/ | 1 | 5 | 20% | utils.ts tested. Snapshot/auto-commit require real git repo |
| mcp/ | 1 | 6 | 17% | oauth.ts tested (27 tests) |
| bus/ | 1 | 3 | 33% | message-bus tested |
| tools/ | 14 | 43 | 33% | Utilities + namespacing + task-parallel + sandbox + bash tested |

### No Coverage (0%)

| Module | Files | Why |
|--------|-------|-----|
| llm/providers/ | 14 | 2 tested (openai, openai-compat). Rest require real HTTP |
| codebase/ | 11 | ranking + graph tested (49 tests). Indexer/symbols require filesystem |
| hooks/ | 4 | executor.ts tested (16 tests). factory/types require real tool execution |
| lsp/ | 4 | Requires language servers |
| git/ | 5 | utils.ts tested. Snapshot/auto-commit require real git repository |
| skills/ | 4 | Requires filesystem |
| slash-commands/ | 3 | Requires filesystem |
| instructions/ | 3 | Requires filesystem |
| integrations/ | 2 | Requires HTTP (Exa API) |
| agent/prompts/ | 6 | system.ts tested (40 tests), variant files low priority |

---

## Tested Files (~110 total)

### agent/ (11 test files)
- `evaluator.test.ts` — 35 tests (progress, goals, tool usage, metrics)
- `events.test.ts` — 53 tests (emitter, buffer, filtering, stats)
- `recovery.test.ts` — 107 tests (error classification, backoff, retry, manager)
- `planner.test.ts` — 32 tests (error classification, constructor)
- `modes/plan.test.ts` — 36 tests (state, restrictions, enter/exit tools)
- `modes/minimal.test.ts` — 13 tests (state management, tool access blocking/allowing, per-session isolation, allowed tools list)
- `__tests__/agent-pipeline.integration.test.ts` — 10 tests (tool dispatch, MAX_TURNS, NO_COMPLETE_TASK, doom loop, abort, event ordering, filtered tools, worker execution, subagent spawning, recursion prevention)
- `__tests__/agent-validation.integration.test.ts` — 5 tests (validation on completion, failure feedback loop, skip when disabled, skip when no files, maxValidationRetries)
- `__tests__/agent-provider-switch.test.ts` — 4 tests (switch provider between turns, history preserved, events emitted, invalid provider handled)
- `metrics.test.ts` — 15 tests (record events, turns, tokens, tool calls, errors, recoveries, duration, export, singleton)
- `prompts/system.test.ts` — 40 tests (RULES/CAPABILITIES constants, buildSystemPrompt, buildWorkerPrompt, buildScenarioPrompt, getModelAdjustments)

### tools/ (14 test files)
- `bash.test.ts` — bash tool execution tests
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

### llm/ (6 test files)
- `client.test.ts` — 31 tests (registry, factory, credential resolution)
- `providers/openai.test.ts` — OpenAI provider tests
- `providers/openai-compat.test.ts` — OpenAI-compatible provider tests
- `utils/errors.test.ts` — error classification tests
- `utils/openai-compat.test.ts` — shared OpenAI-compat streaming tests
- `utils/sse.test.ts` — SSE parser tests

### commander/ (10 test files)
- `executor.test.ts`, `registry.test.ts`, `tool-wrapper.test.ts`, `utils.test.ts`
- `router.test.ts` — 26 tests (analyzeTask keyword matching for 5 task types, confidence scaling, code path detection, selectWorker scoring, null returns)
- `parallel/batch.test.ts`, `parallel/conflict.test.ts`, `parallel/activity.test.ts`, `parallel/scheduler.test.ts`
- `workers/definitions.test.ts`

### config/ (6 test files)
- `manager.test.ts`, `schema.test.ts`
- `credentials.test.ts` — 20 tests (key operations, provider listing, validation, singleton)
- `migration.test.ts` — 24 tests (migrateSettings, mergeWithDefaults, findEnvApiKeys, needsMigration, getChangedFields)
- `export.test.ts` — 20 tests (exportSettingsToJson, importSettingsFromJson, mergeSettings, diffSettings, getDefaultSettingsJson)
- `integration.test.ts` — cross-module config integration tests

### context/ (8 test files)
- `auto-compaction.test.ts`, `compactor.test.ts`, `tracker.test.ts`
- `strategies/split-point.test.ts`, `strategies/tool-truncation.test.ts`, `strategies/verified-summarize.test.ts`, `strategies/visibility.test.ts`
- `strategies/strategies.test.ts` — 27 tests (slidingWindow, createSlidingWindow, buildSummaryTree, selectLevel, createSummarize, getSummarizationPrompt, extractSummary)

### session/ (4 test files)
- `manager.test.ts`, `file-storage.test.ts`, `resume.test.ts`, `doom-loop.test.ts`

### permissions/ (9 test files)
- `auto-approve.test.ts`, `command-validator.test.ts`, `trusted-folders.test.ts`
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

### auth/ (2 test files)
- `manager.test.ts` — auth manager operations
- `pkce.test.ts` — PKCE code challenge and verifier generation

### git/ (1 test file)
- `utils.test.ts` — git utility functions

### validator/ (3 test files)
- `pipeline.test.ts` — ValidationPipeline orchestration, sequential execution, error handling
- `syntax.test.ts` — syntax validator (parse error detection)
- `typescript.test.ts` — TypeScript validator (type checking)

### logger/ (1 test file)
- `logger.test.ts` — AvaLogger singleton, createLogger, agent event mapping, configuration

### hooks/ (1 test file)
- `executor.test.ts` — 16 tests (hook discovery, execution, cancellation)

### bus/ (1 test file)
- `message-bus.test.ts`

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

*Last updated: 2026-02-25 — updated after Session 55 (logger refactor, auth/git/validator tests)*
