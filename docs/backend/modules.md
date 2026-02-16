# Backend Modules — Detailed File Listing

> Every file in `packages/core/src/` with its purpose. Organized by module.
>
> **~235 source files, ~54,200 lines** across 29 directories + 2 top-level files.

---

## agent/ (21 files, ~5,264 lines)

Autonomous agent loop — plans tasks, executes tools, recovers from errors.

| File | Lines | Purpose |
|------|-------|---------|
| `loop.ts` | ~902 | `AgentExecutor` — main agent loop, turn execution, tool dispatch |
| `evaluator.ts` | ~329 | Progress tracking: `calculateProgress`, `evaluateGoal`, `analyzeToolUsage` |
| `events.ts` | ~315 | `AgentEventEmitter`, `EventBuffer`, event filtering/stats utilities |
| `planner.ts` | ~518 | `AgentPlanner` — task planning, error classification, recovery planning |
| `recovery.ts` | ~577 | `RecoveryManager` — error classification, backoff, retry strategies |
| `subagent.ts` | ~317 | `SubagentManager` — spawn/manage child agents, presets |
| `types.ts` | ~373 | `AgentConfig`, `AgentStep`, `AgentEvent`, `ToolCallInfo` types |
| `metrics.ts` | ~192 | `MetricsCollector` — per-session agent metrics (turns, tokens, tools, errors) |
| `index.ts` | ~131 | Barrel export |
| **modes/** | | |
| `modes/plan.ts` | ~363 | Plan mode state, tool restrictions, enter/exit tools |
| `modes/index.ts` | ~24 | Barrel export |
| **prompts/** | | |
| `prompts/system.ts` | ~316 | System prompt builder (`buildSystemPrompt`, `buildWorkerPrompt`) |
| `prompts/index.ts` | ~32 | Barrel export |
| `prompts/variants/claude.ts` | ~246 | Claude-specific prompt adjustments |
| `prompts/variants/gpt.ts` | ~151 | GPT-specific prompt adjustments |
| `prompts/variants/gemini.ts` | ~177 | Gemini-specific prompt adjustments |
| `prompts/variants/generic.ts` | ~99 | Fallback prompt variant |
| `prompts/variants/types.ts` | ~124 | Variant type definitions |
| `prompts/variants/index.ts` | ~78 | Barrel export |
| **test support** | | |
| `test-helpers.ts` | ~60 | Shared test utilities for agent tests |
| `__tests__/mock-llm.ts` | ~80 | Mock LLM client for integration tests |

**Key exports:** `AgentExecutor`, `runAgent`, `AgentPlanner`, `RecoveryManager`, `AgentEventEmitter`, `SubagentManager`, `MetricsCollector`, `planEnterTool`, `planExitTool`

---

## commander/ (12 files, ~2,752 lines)

Team Lead → Senior Leads → Junior Devs hierarchical delegation.

| File | Lines | Purpose |
|------|-------|---------|
| `executor.ts` | ~300 | Worker execution, tool filtering, delegation prefix |
| `registry.ts` | ~200 | `WorkerRegistry` — register/get/list workers |
| `tool-wrapper.ts` | ~250 | Creates `delegate_*` tools from worker definitions |
| `utils.ts` | ~300 | Phone book generation, worker output formatting, stats |
| `types.ts` | ~200 | `WorkerDefinition`, `WorkerResult`, `BatchTask`, etc. |
| `index.ts` | ~120 | Barrel export |
| **parallel/** | | |
| `parallel/batch.ts` | ~300 | Batch execution (sequential, parallel, with conflict detection) |
| `parallel/conflict.ts` | ~200 | `ConflictDetector` — file access conflict detection |
| `parallel/scheduler.ts` | ~350 | `TaskScheduler`, `Semaphore`, fan-out/fan-in patterns |
| `parallel/activity.ts` | ~200 | `ActivityMultiplexer` — merge events from parallel workers |
| `parallel/index.ts` | ~30 | Barrel export |
| **workers/** | | |
| `workers/definitions.ts` | ~250 | Built-in workers: CODER, DEBUGGER, RESEARCHER, REVIEWER, TESTER |

**Key exports:** `WorkerRegistry`, `createDefaultRegistry`, `TaskScheduler`, `ConflictDetector`, `executeBatch`, `BUILT_IN_WORKERS`

---

## tools/ (43 files, ~12,123 lines)

24 registered tools plus utilities, sanitization, validation, locking, namespacing, and sandbox.

### Tool Files (one per tool)
| File | Lines | Tool Name | Purpose |
|------|-------|-----------|---------|
| `read.ts` | ~215 | read_file | Read file contents with line numbers |
| `create.ts` | ~176 | create_file | Create new files |
| `write.ts` | ~169 | write_file | Overwrite file contents |
| `delete.ts` | ~138 | delete_file | Delete files |
| `edit.ts` | ~389 | edit | Fuzzy text replacement (8 strategies) |
| `multiedit.ts` | ~244 | multiedit | Edit multiple files atomically |
| `glob.ts` | ~166 | glob | Find files by pattern |
| `grep.ts` | ~231 | grep | Search file contents (regex) |
| `ls.ts` | ~419 | ls | Directory listing |
| `bash.ts` | ~796 | bash | Shell command execution (PTY, sandbox routing) |
| `batch.ts` | ~266 | batch | Execute multiple tools in batch |
| `task.ts` | ~445 | task | Spawn subagent tasks (single + parallel dispatch) |
| `task-parallel.ts` | ~342 | — | Parallel task execution (Semaphore, Promise.allSettled) |
| `namespacing.ts` | ~181 | — | Tool name prefixing (`mcp__`/`ext__`), backward-compat lookup |
| `question.ts` | ~323 | question | Ask user clarifying questions |
| `skill.ts` | ~207 | skill | Auto-invoke knowledge skills |
| `todo.ts` | ~351 | todo_read/write | Session todo list management |
| `codesearch.ts` | ~313 | codesearch | Search codebase with context |
| `websearch.ts` | ~379 | websearch | Web search (Tavily, Exa) |
| `webfetch.ts` | ~426 | webfetch | Fetch + convert web pages |
| `completion.ts` | ~193 | attempt_completion | Mark task as complete |

### Tool Subdirectories
| Path | Lines | Purpose |
|------|-------|---------|
| `apply-patch/index.ts` | ~198 | Apply unified diffs |
| `apply-patch/parser.ts` | ~293 | Parse patch format |
| `apply-patch/applier.ts` | ~496 | Apply parsed patches |
| `browser/index.ts` | ~215 | Puppeteer browser automation tool |
| `browser/session.ts` | ~362 | Browser session management |
| `browser/actions.ts` | ~325 | Browser action implementations |
| `edit/normalize.ts` | ~210 | Edit normalization utilities |
| **sandbox/** | | |
| `sandbox/types.ts` | ~63 | `Sandbox` interface, `SandboxConfig`, `SandboxExecResult` |
| `sandbox/docker.ts` | ~175 | `DockerSandbox` — Docker-based sandboxed execution |
| `sandbox/noop.ts` | ~102 | `NoopSandbox` — host passthrough (default) |
| `sandbox/index.ts` | ~27 | `createSandbox(config)` factory |

### Utility Files
| File | Lines | Purpose |
|------|-------|---------|
| `registry.ts` | ~534 | `registerTool`, `getTool`, `executeTool`, `getToolDefinitions` |
| `define.ts` | ~206 | `defineTool` factory pattern (OpenCode-inspired) |
| `utils.ts` | ~698 | Path resolution, binary detection, glob matching, line formatting |
| `sanitize.ts` | ~367 | Content sanitization (strip fences, normalize line endings) |
| `truncation.ts` | ~320 | Output truncation (line-level and byte-level) |
| `locks.ts` | ~221 | File locking (`tryFileLock`, `withFileLock`) |
| `validation.ts` | ~115 | Zod schema helpers (`formatZodError`, `isZodSchema`) |
| `edit-replacers.ts` | ~486 | Edit strategies (levenshtein, similarity, line-level replace) |
| `errors.ts` | ~69 | `ToolError`, `ToolErrorType` |
| `types.ts` | ~70 | `Tool`, `ToolContext`, `ToolResult`, `ToolLocation` |
| `index.ts` | ~202 | Barrel export + tool registration |

---

## llm/ (19 files, ~2,596 lines)

LLM client factory + 14 provider implementations + utilities.

| File | Lines | Purpose |
|------|-------|---------|
| `client.ts` | ~265 | `LLMClient` base, `registerClient`, `createClient` factory |
| `index.ts` | ~17 | Barrel export |
| **providers/** | | |
| `providers/anthropic.ts` | ~318 | Anthropic Claude provider |
| `providers/openai.ts` | ~412 | OpenAI GPT provider |
| `providers/google.ts` | ~236 | Google Gemini provider |
| `providers/openrouter.ts` | ~206 | OpenRouter multi-model |
| `providers/deepseek.ts` | ~15 | DeepSeek provider (OpenAI-compat) |
| `providers/groq.ts` | ~28 | Groq provider (OpenAI-compat) |
| `providers/mistral.ts` | ~15 | Mistral AI provider (OpenAI-compat) |
| `providers/cohere.ts` | ~164 | Cohere provider |
| `providers/together.ts` | ~15 | Together AI provider (OpenAI-compat) |
| `providers/xai.ts` | ~15 | xAI/Grok provider (OpenAI-compat) |
| `providers/ollama.ts` | ~235 | Ollama local provider |
| `providers/glm.ts` | ~196 | GLM provider |
| `providers/kimi.ts` | ~196 | Kimi (Moonshot) provider |
| `providers/index.ts` | ~27 | Barrel export |
| **utils/** | | |
| `utils/openai-compat.ts` | ~324 | Shared OpenAI-compatible streaming/request logic |
| `utils/errors.ts` | ~77 | LLM error types and classification |
| `utils/sse.ts` | ~75 | Server-Sent Events parser |

**Key exports:** `LLMClient`, `registerClient`, `createClient`

---

## permissions/ (13 files, ~3,924 lines)

Risk assessment, tool approval, and security inspection pipeline.

| File | Lines | Purpose |
|------|-------|---------|
| `manager.ts` | ~386 | `PermissionManager` — central permission checks |
| `rules.ts` | ~243 | Rule definitions for tool risk levels |
| `command-validator.ts` | ~453 | Validates shell commands for safety |
| `quote-parser.ts` | ~430 | Parse shell quoting for command analysis |
| `auto-approve.ts` | ~621 | Auto-approval logic for low-risk operations |
| `persistent-approvals.ts` | ~395 | Remember user approvals across sessions |
| `trusted-folders.ts` | ~317 | Per-folder trust levels |
| `security-inspector.ts` | ~301 | Pattern-based threat detection with confidence scores |
| `repetition-inspector.ts` | ~150 | Per-tool-call stuck detection with time window |
| `inspector-pipeline.ts` | ~202 | 3-stage inspection chain (Security → Permission → Repetition) |
| `audit.ts` | ~141 | Audit trail — records all inspector decisions |
| `types.ts` | ~178 | Permission type definitions |
| `index.ts` | ~107 | Barrel export |

---

## context/ (12 files, ~2,206 lines)

Token tracking and context window management.

| File | Lines | Purpose |
|------|-------|---------|
| `tracker.ts` | ~291 | `ContextTracker` — token counting, budget tracking |
| `compactor.ts` | ~270 | `ContextCompactor` — triggers compaction when budget exceeded |
| `types.ts` | ~132 | Context type definitions (includes `MessageVisibility`) |
| `index.ts` | ~68 | Barrel export |
| **strategies/** | | |
| `strategies/summarize.ts` | ~184 | Summarization-based compaction |
| `strategies/sliding-window.ts` | ~181 | Drop oldest messages |
| `strategies/hierarchical.ts` | ~303 | Multi-level compaction |
| `strategies/split-point.ts` | ~173 | Smart split point detection |
| `strategies/tool-truncation.ts` | ~171 | Truncate tool outputs first |
| `strategies/verified-summarize.ts` | ~271 | Verified summarization with quality check |
| `strategies/visibility.ts` | ~117 | Visibility-aware compaction (agent_visible tagging) |
| `strategies/index.ts` | ~45 | Barrel export |

---

## config/ (9 files, ~2,172 lines)

Settings, credentials, and configuration management.

| File | Lines | Purpose |
|------|-------|---------|
| `manager.ts` | ~361 | `SettingsManager` — read/write settings |
| `schema.ts` | ~186 | Zod validation schemas for all settings categories |
| `storage.ts` | ~114 | Config file persistence |
| `credentials.ts` | ~270 | API key + OAuth credential storage |
| `migration.ts` | ~224 | Config version migration |
| `export.ts` | ~272 | Export/import config for sharing |
| `integration.ts` | ~278 | Cross-module config integration |
| `types.ts` | ~325 | Config type definitions (includes `SandboxSettings`) |
| `index.ts` | ~142 | Barrel export |

---

## session/ (6 files, ~2,024 lines)

Session persistence, resume, and forking.

| File | Purpose |
|------|---------|
| `manager.ts` | `SessionManager` — CRUD, list, switch |
| `file-storage.ts` | File-based session storage |
| `resume.ts` | Session resume (restore context from checkpoint) |
| `doom-loop.ts` | Detect agent stuck in loops |
| `types.ts` | Session type definitions |
| `index.ts` | Barrel export |

---

## codebase/ (11 files, ~3,431 lines)

Repository understanding and code intelligence.

| File | Purpose |
|------|---------|
| `indexer.ts` | Codebase indexer (file discovery, language detection) |
| `symbols.ts` | Symbol extraction (functions, classes, types) |
| `imports.ts` | Import/export analysis |
| `graph.ts` | Dependency graph building |
| `ranking.ts` | PageRank-based file importance scoring |
| `repomap.ts` | Generate repository map (tree-like view) |
| `types.ts` | Codebase type definitions |
| `index.ts` | Barrel export |
| **treesitter/** | |
| `treesitter/bash.ts` | Bash script parsing via tree-sitter |
| `treesitter/types.ts` | Tree-sitter type definitions |
| `treesitter/index.ts` | Barrel export |

---

## validator/ (9 files, ~2,256 lines)

QA verification pipeline — runs after agent produces results.

| File | Purpose |
|------|---------|
| `pipeline.ts` | `ValidationPipeline` — orchestrates all checks |
| `syntax.ts` | Syntax validation (parse errors) |
| `lint.ts` | Lint checks (ESLint/Biome) |
| `build.ts` | Build verification (TypeScript compilation) |
| `test.ts` | Test runner verification |
| `typescript.ts` | TypeScript-specific checks |
| `self-review.ts` | LLM self-review of changes |
| `types.ts` | Validator type definitions |
| `index.ts` | Barrel export |

---

## Remaining Modules (Brief)

### auth/ (8 files, ~1,107 lines)
OAuth + PKCE flows. Files: `manager.ts`, `anthropic-oauth.ts`, `copilot-oauth.ts`, `google-oauth.ts`, `openai-oauth.ts`, `pkce.ts`, `types.ts`, `index.ts`

### bus/ (3 files, ~524 lines)
Pub/sub message bus. Files: `message-bus.ts`, `types.ts`, `index.ts`

### custom-commands/ (6 files, ~993 lines)
TOML user commands. Files: `discovery.ts`, `parser.ts`, `template.ts`, `loader.ts`, `types.ts`, `index.ts`

### diff/ (4 files, ~657 lines)
Diff tracking. Files: `tracker.ts`, `unified.ts`, `types.ts`, `index.ts`

### extensions/ (5 files, ~947 lines)
Plugin system. Files: `manager.ts`, `manifest.ts`, `storage.ts`, `types.ts`, `index.ts`

### focus-chain/ (4 files, ~825 lines)
Task progress tracking. Files: `manager.ts`, `parser.ts`, `types.ts`, `index.ts`

### git/ (5 files, ~969 lines)
Git snapshots + auto-commit. Files: `snapshot.ts`, `utils.ts`, `auto-commit.ts`, `types.ts`, `index.ts`

### hooks/ (4 files, ~1,147 lines)
Lifecycle hooks. Files: `executor.ts`, `factory.ts`, `types.ts`, `index.ts`

### instructions/ (3 files, ~321 lines)
Project instructions. Files: `loader.ts`, `types.ts`, `index.ts`

### integrations/ (2 files, ~351 lines)
External APIs. Files: `exa.ts`, `index.ts`

### lsp/ (4 files, ~1,219 lines)
Language Server Protocol. Files: `diagnostics.ts`, `call-hierarchy.ts`, `types.ts`, `index.ts`

### mcp/ (6 files, ~1,495 lines)
Model Context Protocol. Files: `client.ts`, `bridge.ts`, `discovery.ts`, `oauth.ts`, `types.ts`, `index.ts`

### models/ (3 files, ~674 lines)
Model registry. Files: `registry.ts`, `types.ts`, `index.ts`

### policy/ (5 files, ~1,071 lines)
Policy engine. Files: `engine.ts`, `matcher.ts`, `rules.ts`, `types.ts`, `index.ts`

### question/ (3 files, ~361 lines)
User questions. Files: `manager.ts`, `types.ts`, `index.ts`

### scheduler/ (3 files, ~337 lines)
Background tasks. Files: `scheduler.ts`, `types.ts`, `index.ts`

### skills/ (4 files, ~629 lines)
Knowledge modules. Files: `discovery.ts`, `loader.ts`, `types.ts`, `index.ts`

### slash-commands/ (4 files, ~854 lines)
User slash commands. Files: `registry.ts`, `commands/index.ts`, `types.ts`, `index.ts`

### types/ (2 files, ~151 lines)
Shared type definitions. Files: `llm.ts`, `index.ts`

---

## Top-Level Files

| File | Lines | Purpose |
|------|-------|---------|
| `index.ts` | ~86 | Main barrel export (all 29 modules) |
| `platform.ts` | ~226 | Platform abstraction (Node.js, Tauri, browser) |

---

*Last updated: 2026-02-15*
