# Backend Modules — Detailed File Listing

> Every file in `packages/core/src/` with its purpose. Organized by module.

---

## agent/ (12 files, 4,197 lines)

Autonomous agent loop — plans tasks, executes tools, recovers from errors.

| File | Lines | Purpose |
|------|-------|---------|
| `loop.ts` | ~600 | `AgentExecutor` — main agent loop, turn execution, tool dispatch |
| `evaluator.ts` | ~300 | Progress tracking: `calculateProgress`, `evaluateGoal`, `analyzeToolUsage` |
| `events.ts` | ~450 | `AgentEventEmitter`, `EventBuffer`, event filtering/stats utilities |
| `planner.ts` | ~350 | `AgentPlanner` — task planning, error classification, recovery planning |
| `recovery.ts` | ~500 | `RecoveryManager` — error classification, backoff, retry strategies |
| `subagent.ts` | ~400 | `SubagentManager` — spawn/manage child agents, presets |
| `types.ts` | ~250 | `AgentConfig`, `AgentStep`, `AgentEvent`, `ToolCallInfo` types |
| `test-helpers.ts` | ~60 | Mock factories for testing |
| `index.ts` | ~130 | Barrel export |
| **modes/** | | |
| `modes/plan.ts` | ~300 | Plan mode state, tool restrictions, enter/exit tools |
| `modes/index.ts` | ~20 | Barrel export |
| **prompts/** | | |
| `prompts/system.ts` | ~400 | System prompt builder (`buildSystemPrompt`, `buildWorkerPrompt`) |
| `prompts/index.ts` | ~30 | Barrel export |
| `prompts/variants/claude.ts` | ~150 | Claude-specific prompt adjustments |
| `prompts/variants/gpt.ts` | ~120 | GPT-specific prompt adjustments |
| `prompts/variants/gemini.ts` | ~120 | Gemini-specific prompt adjustments |
| `prompts/variants/generic.ts` | ~80 | Fallback prompt variant |
| `prompts/variants/types.ts` | ~30 | Variant type definitions |
| `prompts/variants/index.ts` | ~20 | Barrel export |

**Key exports:** `AgentExecutor`, `runAgent`, `AgentPlanner`, `RecoveryManager`, `AgentEventEmitter`, `SubagentManager`, `planEnterTool`, `planExitTool`

---

## commander/ (12 files, 2,744 lines)

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

## tools/ (37 files, 10,990 lines)

22 registered tools plus utilities, sanitization, validation, and locking.

### Tool Files (one per tool)
| File | Tool Name | Purpose |
|------|-----------|---------|
| `read.ts` | read_file | Read file contents with line numbers |
| `create.ts` | create_file | Create new files |
| `write.ts` | write_file | Overwrite file contents |
| `delete.ts` | delete_file | Delete files |
| `edit.ts` | edit | Fuzzy text replacement (8 strategies) |
| `multiedit.ts` | multiedit | Edit multiple files atomically |
| `glob.ts` | glob | Find files by pattern |
| `grep.ts` | grep | Search file contents (regex) |
| `ls.ts` | ls | Directory listing |
| `bash.ts` | bash | Shell command execution (PTY) |
| `batch.ts` | batch | Execute multiple tools in batch |
| `task.ts` | task | Spawn subagent tasks |
| `question.ts` | question | Ask user clarifying questions |
| `skill.ts` | skill | Auto-invoke knowledge skills |
| `todo.ts` | todo_read/write | Session todo list management |
| `codesearch.ts` | codesearch | Search codebase with context |
| `websearch.ts` | websearch | Web search (Tavily, Exa) |
| `webfetch.ts` | webfetch | Fetch + convert web pages |
| `completion.ts` | attempt_completion | Mark task as complete |

### Tool Subdirectories
| Path | Purpose |
|------|---------|
| `apply-patch/index.ts` | Apply unified diffs |
| `apply-patch/parser.ts` | Parse patch format |
| `apply-patch/applier.ts` | Apply parsed patches |
| `browser/index.ts` | Puppeteer browser automation tool |
| `browser/session.ts` | Browser session management |
| `edit/normalize.ts` | Edit normalization utilities |

### Utility Files
| File | Purpose |
|------|---------|
| `registry.ts` | `registerTool`, `getTool`, `executeTool`, `getToolDefinitions` |
| `define.ts` | `defineTool` factory pattern (OpenCode-inspired) |
| `utils.ts` | Path resolution, binary detection, glob matching, line formatting |
| `sanitize.ts` | Content sanitization (strip fences, normalize line endings) |
| `truncation.ts` | Output truncation (line-level and byte-level) |
| `locks.ts` | File locking (`tryFileLock`, `withFileLock`) |
| `validation.ts` | Zod schema helpers (`formatZodError`, `isZodSchema`) |
| `edit-replacers.ts` | Edit strategies (levenshtein, similarity, line-level replace) |
| `errors.ts` | `ToolError`, `ToolErrorType` |
| `types.ts` | `Tool`, `ToolContext`, `ToolResult`, `ToolLocation` |

---

## llm/ (16 files, 3,222 lines)

LLM client factory + 14 provider implementations.

| File | Lines | Purpose |
|------|-------|---------|
| `client.ts` | ~350 | `LLMClient` base, `registerClient`, `createClient` factory |
| `index.ts` | ~50 | Barrel export |
| **providers/** | | |
| `providers/anthropic.ts` | ~250 | Anthropic Claude provider |
| `providers/openai.ts` | ~250 | OpenAI GPT provider |
| `providers/google.ts` | ~250 | Google Gemini provider |
| `providers/openrouter.ts` | ~200 | OpenRouter multi-model |
| `providers/deepseek.ts` | ~150 | DeepSeek provider |
| `providers/groq.ts` | ~150 | Groq provider |
| `providers/mistral.ts` | ~150 | Mistral AI provider |
| `providers/cohere.ts` | ~150 | Cohere provider |
| `providers/together.ts` | ~150 | Together AI provider |
| `providers/xai.ts` | ~150 | xAI (Grok) provider |
| `providers/ollama.ts` | ~200 | Ollama local provider |
| `providers/glm.ts` | ~150 | GLM provider |
| `providers/kimi.ts` | ~150 | Kimi (Moonshot) provider |
| `providers/index.ts` | ~30 | Barrel export |

**Key exports:** `LLMClient`, `registerClient`, `createClient`

---

## memory/ (9 files, 2,747 lines)

Long-term memory with episodic, semantic, and procedural stores.

| File | Purpose |
|------|---------|
| `manager.ts` | `MemoryManager` — unified interface, remember/recall |
| `episodic.ts` | `EpisodicMemoryManager` — session memories (what happened) |
| `semantic.ts` | `SemanticMemoryManager` — facts/knowledge (what is true) |
| `procedural.ts` | `ProceduralMemoryManager` — patterns/actions (what to do) |
| `embedding.ts` | `OpenAIEmbedder`, `CachingEmbedder`, `MockEmbedder` |
| `consolidation.ts` | `ConsolidationEngine` — merge/decay old memories |
| `store.ts` | `SQLiteVectorStore` — vector storage backend |
| `types.ts` | Memory type definitions |
| `test-helpers.ts` | Mock factories for testing |

---

## permissions/ (9 files, 3,130 lines)

Risk assessment and tool approval system.

| File | Purpose |
|------|---------|
| `manager.ts` | `PermissionManager` — central permission checks |
| `rules.ts` | Rule definitions for tool risk levels |
| `command-validator.ts` | Validates shell commands for safety |
| `quote-parser.ts` | Parse shell quoting for command analysis |
| `auto-approve.ts` | Auto-approval logic for low-risk operations |
| `persistent-approvals.ts` | Remember user approvals across sessions |
| `trusted-folders.ts` | Per-folder trust levels |
| `types.ts` | Permission type definitions |
| `index.ts` | Barrel export |

---

## context/ (11 files, 2,077 lines)

Token tracking and context window management.

| File | Purpose |
|------|---------|
| `tracker.ts` | `ContextTracker` — token counting, budget tracking |
| `compactor.ts` | `ContextCompactor` — triggers compaction when budget exceeded |
| `types.ts` | Context type definitions |
| `index.ts` | Barrel export |
| **strategies/** | |
| `strategies/summarize.ts` | Summarization-based compaction |
| `strategies/sliding-window.ts` | Drop oldest messages |
| `strategies/hierarchical.ts` | Multi-level compaction |
| `strategies/split-point.ts` | Smart split point detection |
| `strategies/tool-truncation.ts` | Truncate tool outputs first |
| `strategies/verified-summarize.ts` | Verified summarization with quality check |
| `strategies/index.ts` | Barrel export |

---

## config/ (9 files, 2,082 lines)

Settings, credentials, and configuration management.

| File | Purpose |
|------|---------|
| `manager.ts` | `SettingsManager` — read/write settings |
| `schema.ts` | Settings schema validation |
| `storage.ts` | Config file persistence |
| `credentials.ts` | API key + OAuth credential storage |
| `migration.ts` | Config version migration |
| `export.ts` | Export config for sharing |
| `integration.ts` | Cross-module config integration |
| `types.ts` | Config type definitions |
| `index.ts` | Barrel export |

---

## session/ (6 files, 2,024 lines)

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

## codebase/ (11 files, 3,431 lines)

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

## validator/ (9 files, 2,253 lines)

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

### auth/ (8 files, 1,107 lines)
OAuth + PKCE flows. Files: `manager.ts`, `anthropic-oauth.ts`, `copilot-oauth.ts`, `google-oauth.ts`, `openai-oauth.ts`, `pkce.ts`, `types.ts`, `index.ts`

### bus/ (3 files, 524 lines)
Pub/sub message bus. Files: `message-bus.ts`, `types.ts`, `index.ts`

### custom-commands/ (6 files, 993 lines)
TOML user commands. Files: `discovery.ts`, `parser.ts`, `template.ts`, `loader.ts`, `types.ts`, `index.ts`

### diff/ (4 files, 657 lines)
Diff tracking. Files: `tracker.ts`, `unified.ts`, `types.ts`, `index.ts`

### extensions/ (5 files, 947 lines)
Plugin system. Files: `manager.ts`, `manifest.ts`, `storage.ts`, `types.ts`, `index.ts`

### focus-chain/ (4 files, 825 lines)
Task progress tracking. Files: `manager.ts`, `parser.ts`, `types.ts`, `index.ts`

### git/ (4 files, 799 lines)
Git snapshots. Files: `snapshot.ts`, `utils.ts`, `types.ts`, `index.ts`

### hooks/ (4 files, 1,135 lines)
Lifecycle hooks. Files: `executor.ts`, `factory.ts`, `types.ts`, `index.ts`

### instructions/ (3 files, 321 lines)
Project instructions. Files: `loader.ts`, `types.ts`, `index.ts`

### integrations/ (2 files, 351 lines)
External APIs. Files: `exa.ts`, `index.ts`

### lsp/ (4 files, 1,219 lines)
Language Server Protocol. Files: `diagnostics.ts`, `call-hierarchy.ts`, `types.ts`, `index.ts`

### mcp/ (6 files, 1,470 lines)
Model Context Protocol. Files: `client.ts`, `bridge.ts`, `discovery.ts`, `oauth.ts`, `types.ts`, `index.ts`

### models/ (3 files, 674 lines)
Model registry. Files: `registry.ts`, `types.ts`, `index.ts`

### policy/ (5 files, 1,071 lines)
Policy engine. Files: `engine.ts`, `matcher.ts`, `rules.ts`, `types.ts`, `index.ts`

### question/ (3 files, 361 lines)
User questions. Files: `manager.ts`, `types.ts`, `index.ts`

### scheduler/ (3 files, 337 lines)
Background tasks. Files: `scheduler.ts`, `types.ts`, `index.ts`

### skills/ (4 files, 629 lines)
Knowledge modules. Files: `discovery.ts`, `loader.ts`, `types.ts`, `index.ts`

### slash-commands/ (4 files, 854 lines)
User slash commands. Files: `registry.ts`, `commands/index.ts`, `types.ts`, `index.ts`

### a2a/ (7 files, 1,466 lines)
Agent-to-Agent protocol. Files: `server.ts`, `streaming.ts`, `task.ts`, `auth.ts`, `agent-card.ts`, `types.ts`, `index.ts`

### acp/ (7 files, 1,377 lines)
Agent Client Protocol. Files: `terminal.ts`, `session-store.ts`, `mcp-bridge.ts`, `error-handler.ts`, `mode.ts`, `types.ts`, `index.ts`

---

*Last updated: 2026-02-08*
