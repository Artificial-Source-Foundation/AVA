# Backend Architecture Guide

> Deep navigation guide for `packages/core/src/`. Explains where things are, why they exist, and how they work — so future sessions can navigate and modify the codebase quickly.

**Scope:** ~235 source files, ~54,200 lines, 29 modules in `packages/core/src/`

---

## Table of Contents

1. [Quick Reference](#quick-reference)
2. [Request Lifecycle](#request-lifecycle)
3. [Agent System](#agent-system)
4. [Tools System](#tools-system)
5. [Intelligence Modules](#intelligence-modules)
6. [Safety & Permissions](#safety--permissions)
7. [Configuration](#configuration)
8. [Extensibility](#extensibility)
9. [Infrastructure](#infrastructure)
10. [Protocols](#protocols)
11. [Key Patterns](#key-patterns)
12. [Common Tasks](#common-tasks)

---

## Quick Reference

### Singletons (module-level `get/set/reset` pattern)

Every major subsystem uses the same singleton pattern: a module-level `let _instance`, exposed via `getX()`, `setX()`, `resetX()`. This enables dependency injection in tests and platform swapping.

| Singleton | Module | Getter |
|-----------|--------|--------|
| Platform | `platform.ts` | `getPlatform()` |
| Settings | `config/manager.ts` | `getSettingsManager()` |
| Credentials | `config/credentials.ts` | `getCredentialsManager()` |
| Policy Engine | `policy/engine.ts` | `getPolicyEngine()` |
| Permission Manager | `permissions/manager.ts` | `getPermissionManager()` |
| Message Bus | `bus/message-bus.ts` | `getMessageBus()` |
| Session Manager | `session/manager.ts` | `getSessionManager()` |
| Extension Manager | `extensions/manager.ts` | `getExtensionManager()` |
| Hook Runner | `hooks/executor.ts` | `getHookRunner()` |
| Scheduler | `scheduler/scheduler.ts` | `getScheduler()` |
| Question Manager | `question/manager.ts` | `getQuestionManager()` |
| Metrics Collector | `agent/metrics.ts` | `getMetricsCollector()` |
| Doom Loop Detector | `session/doom-loop.ts` | `getDoomLoopDetector()` |
| Diff Tracker | `diff/tracker.ts` | `getDefaultTracker()` |
| Audit Trail | `permissions/audit.ts` | `getAuditTrail()` |
| Command Validator | `permissions/command-validator.ts` | `getCommandValidator()` |
| Trusted Folders | `permissions/trusted-folders.ts` | `getTrustedFolderManager()` |

### Key Entry Points

| What | File | Export |
|------|------|--------|
| Run an agent | `agent/loop.ts` | `AgentExecutor`, `runAgent()` |
| Execute a tool | `tools/registry.ts` | `executeTool()` |
| Create LLM client | `llm/client.ts` | `createClient()` |
| Build system prompt | `agent/prompts/system.ts` | `buildSystemPrompt()` |
| Check permissions | `policy/engine.ts` | `PolicyEngine.check()` |
| Manage sessions | `session/manager.ts` | `SessionManager` |
| Count tokens | `context/tracker.ts` | `countTokens()`, `ContextTracker` |

### Platform Abstraction

`platform.ts` defines interfaces (`IFileSystem`, `IShell`, `IPTY`, `ICredentialStore`, `IDatabase`) implemented by:
- `packages/platform-node/` — Node.js (CLI)
- `packages/platform-tauri/` — Tauri (desktop app)

All file I/O, shell execution, credential storage, and database access goes through `getPlatform()`. **Never use `node:fs` directly in core** — always use `getPlatform().fs`.

---

## Request Lifecycle

How a user message flows through the system end-to-end:

```
User Message (Desktop/CLI)
  │
  ├─ 1. SessionManager.addMessage()          # Persist to session
  │
  ├─ 2. AgentExecutor.run(goal, context)      # agent/loop.ts
  │     │
  │     ├─ buildSystemPrompt()                # agent/prompts/system.ts
  │     │   └─ getVariantForModel()           # Model-specific XML/markdown
  │     │
  │     ├─ LLM Stream (createClient)          # llm/client.ts
  │     │   └─ Provider-specific client       # llm/providers/*.ts
  │     │
  │     ├─ Tool Calls Extracted
  │     │   │
  │     │   ├─ 3. PolicyEngine.check()        # policy/engine.ts
  │     │   │   └─ Rule matching (50 rules)
  │     │   │
  │     │   ├─ 4. InspectorPipeline.inspect() # permissions/inspector-pipeline.ts
  │     │   │   ├─ SecurityInspector          # Pattern-based threat detection
  │     │   │   └─ RepetitionInspector        # Stuck-loop detection
  │     │   │
  │     │   ├─ 5. MessageBus.confirm()        # bus/message-bus.ts
  │     │   │   └─ UI confirmation if needed
  │     │   │
  │     │   ├─ 6. HookRunner.run(PreToolUse)  # hooks/executor.ts
  │     │   │
  │     │   ├─ 7. tool.execute(params, ctx)   # tools/<tool>.ts
  │     │   │
  │     │   ├─ 8. HookRunner.run(PostToolUse) # hooks/executor.ts
  │     │   │
  │     │   └─ 9. Git auto-commit (optional)
  │     │
  │     ├─ DoomLoopDetector.check()           # session/doom-loop.ts
  │     │
  │     ├─ File tracking (modifiedFiles set)  # agent/loop.ts
  │     │
  │     ├─ MetricsCollector.record()          # agent/metrics.ts
  │     │
  │     ├─ On complete_task:
  │     │   └─ ValidationPipeline.run()       # validator/pipeline.ts
  │     │       ├─ syntax → typescript → lint
  │     │       ├─ If passed → complete
  │     │       └─ If failed → feedback to agent, retry
  │     │
  │     ├─ Provider switch check              # Mid-session provider switching
  │     │   └─ createClient(newProvider)       # If pendingProviderSwitch set
  │     │
  │     └─ Termination check:
  │         ├─ MAX_TURNS reached
  │         ├─ TIMEOUT exceeded
  │         ├─ DOOM_LOOP detected (3 identical calls)
  │         ├─ GOAL reached (complete_task)
  │         └─ ABORTED (user/signal)
  │
  └─ AgentResult returned to UI
```

### Commander (Hierarchical) Flow

When the Team Lead delegates work:

```
Team Lead (AgentExecutor)
  │
  ├─ Calls delegate_<worker> tool        # commander/tool-wrapper.ts
  │   └─ WorkerRegistry.get(name)        # commander/registry.ts
  │
  ├─ executeWorker(definition, inputs)   # commander/executor.ts
  │   ├─ getFilteredTools()              # BLOCKS all delegate_* tools (recursion prevention)
  │   ├─ Creates new AgentExecutor       # With worker's tool subset + system prompt
  │   └─ Runs agent loop                 # Same loop, restricted tools
  │
  └─ Worker can spawn Junior Devs       # Via task tool → SubagentManager
      └─ SubagentManager.createConfig() # agent/subagent.ts
          └─ Presets: explore(read-only), plan(+write), execute(all)
```

**Why recursion prevention?** Workers get `delegate_*` tools filtered out so a Senior Lead can't call another Senior Lead — only Team Lead delegates. This prevents infinite delegation chains.

---

## Agent System

### agent/ — The Autonomous Loop

**Why it exists:** Core agent execution — receives a goal, loops calling LLM → tools until done or stopped.

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `loop.ts` | ~960 | `AgentExecutor`, `runAgent` | Main loop: LLM stream → extract tool calls → execute → validate → repeat |
| `types.ts` | ~430 | `AgentConfig`, `AgentEvent`, `AgentResult` | All agent types; `AgentTerminateMode` enum (7 modes), validation + provider switch events |
| `events.ts` | ~316 | `AgentEventEmitter`, `EventBuffer` | Event emission + circular buffer for collection |
| `planner.ts` | ~519 | `AgentPlanner` | LLM-based task planning + recovery planning (uses weak model) |
| `recovery.ts` | ~578 | `RecoveryManager` | Error classification (9 categories), exponential backoff, retry |
| `evaluator.ts` | ~330 | `calculateProgress`, `evaluateGoal` | Progress % tracking, goal confidence scoring |
| `subagent.ts` | ~318 | `SubagentManager`, `SUBAGENT_PRESETS` | Child agent spawning with tool filtering |
| `metrics.ts` | ~193 | `MetricsCollector` | Per-session metrics (turns, tokens, tool counts, errors) |

**agent/modes/plan.ts** (~364 lines) — Plan Mode restricts tools to read-only (`read`, `glob`, `grep`, `ls`, `websearch`, `webfetch`). Blocks all write/execute tools. Per-session state tracking.

**agent/modes/minimal.ts** (~95 lines) — Minimal Mode restricts to 8 core tools (`read_file`, `write_file`, `edit`, `bash`, `glob`, `grep`, `attempt_completion`, `question`). Same per-session Map pattern as plan mode. Reduces token usage for focused tasks. Wired into `registry.ts` via `checkMinimalModeAccess()`.

**agent/prompts/** — System prompt construction:
- `system.ts` — `buildSystemPrompt()` with `RULES`, `CAPABILITIES`, `BEST_PRACTICES` constants (~2000 tokens)
- `variants/` — Model-specific adjustments:
  - `claude.ts` — Uses XML tags (`<rules>`, `<capabilities>`) — Claude understands XML well
  - `gpt.ts` — Uses markdown headings
  - `gemini.ts` — Compact format
  - `generic.ts` — Fallback

**Key Design Decisions:**
- **Doom loop detection in loop.ts**: 3 consecutive identical tool calls with same params → terminate. Hash-based comparison.
- **Grace period**: On MAX_TURNS/TIMEOUT, gives agent 1 final turn to attempt completion before hard stop.
- **Event-driven**: Every action emits typed events → UI can render live progress. Validation events: `validation:start`, `validation:result`, `validation:finish`. Provider switch: `provider:switch`.
- **Weak model for planning**: `AgentPlanner` uses `getWeakModelConfig()` for cheaper/faster plan generation.
- **File tracking**: `modifiedFiles: Set<string>` in AgentExecutor tracks all files changed by write/edit/create/delete/patch/multiedit tools. Used by validation pipeline.
- **Validation gate**: On `complete_task`, if `validationEnabled` and files were modified, runs `ValidationPipeline` (syntax → typescript → lint). On failure, sends feedback to agent and retries up to `maxValidationRetries` (default: 2).
- **Mid-session provider switching**: `requestProviderSwitch(provider, model)` queues a switch. Main loop creates new LLM client before next turn. Conversation history preserved — messages use provider-agnostic format.

### commander/ — Hierarchical Delegation

**Why it exists:** Enables Team Lead → Senior Lead → Junior Dev hierarchy. Team Lead plans, delegates specialized work to domain experts.

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `executor.ts` | ~300 | `executeWorker`, `executeWithAutoRouting` | Execute a worker with filtered tools + recursion prevention + auto-routing |
| `registry.ts` | ~100 | `WorkerRegistry` | Registry of available workers + phone book generation |
| `router.ts` | ~115 | `analyzeTask`, `selectWorker` | Keyword/heuristic task analysis for auto-routing to best worker |
| `tool-wrapper.ts` | ~120 | `createWorkerTool` | Wraps `WorkerDefinition` as a callable `delegate_<name>` tool |
| `utils.ts` | ~80 | `generatePhoneBook` | Formats worker directory for Team Lead's system prompt |
| `types.ts` | ~234 | `WorkerDefinition`, `WorkerResult` | Worker config, activity events, parallel config |

**commander/workers/** — Built-in worker definitions:
- `definitions.ts` — `CODER_WORKER` (write code, 15 turns), `TESTER_WORKER` (write/run tests), `REVIEWER_WORKER` (read-only review), `RESEARCHER_WORKER` (info gathering), `DEBUGGER_WORKER` (debugging/fixing)

**commander/parallel/** — Parallel execution:
- `batch.ts` — `BatchExecutor` with `Semaphore` for concurrency control
- `scheduler.ts` — `TaskScheduler` for parallel worker execution
- `conflict.ts` — `ConflictDetector` detects file access conflicts between parallel workers
- `activity.ts` — `ActivityTracker` for live progress tracking

**Key Design Decisions:**
- **Phone book**: `registry.ts` generates a text directory of workers that gets injected into the Team Lead's system prompt, teaching it who to delegate to.
- **Tool prefix convention**: All worker tools are `delegate_<worker_name>`. The `DELEGATE_TOOL_PREFIX = 'delegate_'` constant is checked in `getFilteredTools()` to block recursion.
- **Auto-routing**: `router.ts` provides `analyzeTask()` for keyword/heuristic analysis and `selectWorker()` to map task type → worker. `executeWithAutoRouting()` tries auto-route at confidence ≥ 0.7 before falling back to LLM phone book routing. Routes: test→tester, review→reviewer, research→researcher, debug→debugger, write→coder.

### validator/ — QA Pipeline (Wired into Agent Loop)

**Why it exists:** Runs automated checks after code changes to verify quality. Now wired into `AgentExecutor` — runs automatically on `complete_task` when files were modified.

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `pipeline.ts` | ~300 | `ValidationPipeline` | Orchestrates validators in sequence |
| `syntax.ts` | ~200 | `SyntaxValidator` | Basic syntax checks |
| `typescript.ts` | ~200 | `TypeScriptValidator` | `tsc --noEmit` type checking |
| `lint.ts` | ~200 | `LintValidator` | Runs linter |
| `test.ts` | ~200 | `TestValidator` | Runs test suite |
| `build.ts` | ~200 | `BuildValidator` | Build/compilation checking |
| `self-review.ts` | ~300 | `SelfReviewValidator` | LLM reviews its own changes |
| `types.ts` | ~150 | `ValidatorResult`, `ValidatorType` | 5 validator types |

**Pipeline runs in order:** syntax → typescript → lint → test → self-review. First critical failure stops chain (configurable via `AgentSettings.enabledValidators`).

**Integration with agent loop:** When `AgentConfig.validationEnabled` is true and the agent calls `complete_task` with modified files, the pipeline runs automatically. If validation fails, the agent receives error feedback and gets another turn to fix issues (up to `maxValidationRetries`, default 2). This prevents the agent from marking tasks complete with syntax errors or type failures.

---

## Tools System

### Architecture Overview

```
tools/
├── Core Infrastructure
│   ├── types.ts          # Tool, ToolDefinition, ToolContext, ToolResult interfaces
│   ├── define.ts         # defineTool() — Zod-based declarative tool definition
│   ├── registry.ts       # Global registry + executeTool() (10-step flow)
│   ├── errors.ts         # ToolError class with error codes
│   ├── validation.ts     # Zod schema utilities
│   ├── namespacing.ts    # mcp__/ext__ prefix management
│   ├── locks.ts          # File locking (concurrent edit prevention)
│   ├── sanitize.ts       # Markdown fence stripping, model-specific output fixes
│   ├── truncation.ts     # Smart output truncation with file persistence
│   └── utils.ts          # Binary detection, path resolution, glob matching
│
├── 24 Tool Implementations
│   ├── File: read.ts, write.ts, create.ts, delete.ts, edit.ts, multiedit.ts
│   ├── Search: glob.ts, grep.ts, ls.ts, codesearch.ts
│   ├── Exec: bash.ts, batch.ts, task.ts, browser/
│   ├── Web: websearch.ts, webfetch.ts
│   ├── Agent: question.ts, completion.ts, skill.ts, todo.ts
│   └── Patch: apply-patch/
│
├── Edit Support
│   ├── edit-replacers.ts  # 8 fuzzy matching strategies
│   └── edit/normalize.ts  # Unicode normalization
│
├── Sandbox
│   ├── sandbox/types.ts   # Sandbox interface
│   ├── sandbox/docker.ts  # Docker container execution
│   ├── sandbox/noop.ts    # Host passthrough (default)
│   └── sandbox/index.ts   # createSandbox() factory
│
└── task-parallel.ts       # Parallel subagent execution for task tool
```

### Tool Execution Flow (registry.ts `executeTool`)

This is the most critical function in the codebase — every tool call goes through it:

1. **Rate Limit** — `toolCallCount < MAX_TOOL_CALLS (10)` per turn
2. **Plan Mode** — `checkPlanModeAccess()` blocks write tools in plan mode
3. **Minimal Mode** — `checkMinimalModeAccess()` restricts to 8 core tools
4. **Doom Loop** — Detect 3+ identical consecutive calls
5. **Approval Override** — Check `requires_approval` flag (LLM can flag dangerous commands)
6. **Legacy Auto-Approval** — `shouldAutoApprove()` checks path/command patterns
7. **Bus Confirmation** — `bus.confirmToolExecution()` shows UI dialog with risk level
8. **PreToolUse Hook** — Plugin hooks can cancel or inject context
9. **Validation** — `tool.validate(params)` via Zod schema
10. **Execution** — `tool.execute(params, ctx)` with abort signal
11. **PostToolUse Hook** — Plugin hooks for logging/modification
12. **Git Auto-Commit** — Optional staging of modified files

### Risk Levels

| Level | Tools | UI Behavior |
|-------|-------|-------------|
| `low` | read, glob, grep, ls, websearch, skill | Auto-approved |
| `medium` | write, edit, create, browser, webfetch | Ask user (configurable) |
| `high` | bash | Always ask user |
| `critical` | bash with `requires_approval=true` | Always ask, highlighted |

### Tool Definition Pattern (define.ts)

All tools use `defineTool()` for consistent Zod-validated definition:

```typescript
export const myTool = defineTool({
  name: 'my_tool',
  description: 'What it does',
  schema: z.object({ path: z.string() }),
  permissions: ['read'],
  locations: (input) => [{ path: input.path, type: 'read' }],
  execute: async (params, ctx) => ({ success: true, output: '...' })
})
```

### Bash Tool (bash.ts, ~797 lines)

Most complex tool. Three execution modes:

1. **Regular** — `/bin/bash -c` with stdout/stderr capture
2. **PTY** — Interactive terminal (120x40) for vim, ssh, python REPL
3. **Sandbox** — Docker container if `settings.sandbox.mode === 'docker'`

Safety chain: `quickDangerCheck()` → `CommandValidator.validate()` → `PolicyEngine.check()` → `MessageBus.confirm()` → execute.

### Edit Tool (edit.ts + edit-replacers.ts)

Uses 8 fuzzy matching strategies tried in order:
1. **SimpleReplacer** — Exact string match
2. **LineTrimmedReplacer** — Match with whitespace normalized per line
3. **BlockAnchorReplacer** — Match by first/last line anchors, fuzzy middle
4. 5 more Levenshtein-distance-based strategies with increasing tolerance

**Why 8 strategies?** LLMs frequently get whitespace wrong, miss indentation, or slightly modify code when quoting it. Progressive fuzzy matching handles this gracefully.

### Tool Namespacing (namespacing.ts)

Prevents name collisions between built-in, MCP, and extension tools:

```
Built-in:  read_file          (no prefix)
MCP:       mcp__github__create_issue   (mcp__<server>__<tool>)
Extension: ext__docker__build          (ext__<plugin>__<tool>)
```

Functions: `namespaceTool()`, `stripNamespace()`, `isMcpTool()`, `isNamespaced()`

---

## Intelligence Modules

### codebase/ — Repository Analysis

**Why it exists:** Gives the agent understanding of project structure, file importance, and dependencies.

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `symbols.ts` | ~460 | `extractSymbols` | Regex-based extraction of functions, classes, types (TS, Python, Go, Rust) |
| `imports.ts` | ~538 | `parseImports`, `parseExports` | Parse import/export statements, resolve paths |
| `indexer.ts` | ~352 | `FileIndexer` | Index files with language detection, incremental updates, hash-based caching |
| `graph.ts` | ~332 | `buildDependencyGraph` | Build dependency graph from imports, detect cycles, transitive deps |
| `ranking.ts` | ~397 | `calculatePageRank` | PageRank for file importance (damping=0.85, 20 iterations) |
| `repomap.ts` | ~351 | `generateRepoMap` | Compact codebase summary for LLM context within token budget |
| `types.ts` | ~365 | `FileEntry`, `CodeSymbol`, `RepoMap` | All codebase types |

**codebase/treesitter/** — Bash command analysis:
- `bash.ts` (~362 lines) — `analyzeBash()`, `isSafeCommand()`, `getCommandRiskSummary()` — Classifies bash commands for safety
- `types.ts` (~189 lines) — `DESTRUCTIVE_COMMANDS`, `SAFE_COMMANDS` sets

**Data Flow:** `FileIndexer.scan()` → `extractSymbols()` + `parseImports()` → `buildDependencyGraph()` → `calculatePageRank()` → `generateRepoMap()` → injected into system prompt

**Relevance Scoring Weights:** PageRank 30% + Keyword matching 50% + Recency 20%

### context/ — Token Management & Compaction

**Why it exists:** LLMs have finite context windows. This module tracks usage and compacts conversations when they get too long.

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `tracker.ts` | ~292 | `ContextTracker`, `countTokens` | Token counting via gpt-tokenizer, budget management |
| `compactor.ts` | ~271 | `Compactor`, `createAutoCompactor` | Strategy orchestrator, auto-compaction trigger |
| `types.ts` | ~133 | `Message`, `CompactionStrategy` | Message type with `visibility` field, strategy interface |

**context/strategies/** — 6 compaction strategies (tried in order):

| Strategy | File | How It Works | LLM Cost |
|----------|------|-------------|----------|
| Tool Truncation | `tool-truncation.ts` | Truncate large tool outputs (keep last 30 lines) | None |
| Visibility | `visibility.ts` | Tag old messages as `agent_visible` (hidden from UI) | None |
| Sliding Window | `sliding-window.ts` | Keep most recent N messages | None |
| Summarize | `summarize.ts` | LLM summarizes older messages | 1 call |
| Hierarchical | `hierarchical.ts` | Tree of summaries at multiple detail levels | Multiple |
| Verified Summarize | `verified-summarize.ts` | Summarize + state snapshot + verification probe | 2-3 calls |
| Split Point | `split-point.ts` | Find safe conversation boundaries for splitting | None |

**Message Visibility** (`MessageVisibility`):
- `'all'` — Shown in UI + sent to LLM (default)
- `'user_visible'` — Shown in UI, not sent to LLM
- `'agent_visible'` — Sent to LLM, hidden from UI (compacted messages)

**Auto-compaction:** `createAutoCompactor()` triggers compaction when context exceeds configured threshold % of max tokens.

### lsp/ — Language Server Protocol

| File | Lines | Purpose |
|------|-------|---------|
| `client.ts` | ~400 | LSP client with stdio transport |
| `manager.ts` | ~350 | Multi-language LSP management (TS, Python, Go, Rust, Java) |
| `types.ts` | ~200 | LSP types |
| `index.ts` | ~50 | Public API |

### Other Intelligence Modules

**diff/** — Diff tracking for code changes:
- `unified.ts` — `createDiff()`, `parseDiffHunks()`, `getDiffStats()` using diff npm package
- `tracker.ts` — `DiffTracker` class tracks pending/applied/rejected edits with event subscription

**focus-chain/** — Task progress tracking:
- `parser.ts` — Parse markdown task lists, update/add/remove tasks, calculate progress
- `manager.ts` — `FocusChainManager` manages active task chains

**models/** — LLM model registry:
- `registry.ts` — `getModel()`, `getContextLimit()`, `estimateCost()` for ~16 models
- `types.ts` — `ModelInfo` with capabilities, pricing, context limits

---

## Safety & Permissions

### Three-Layer Security Model

```
Layer 1: PolicyEngine (policy/)
  → Rule-based: 50 built-in rules, wildcard matching, approval modes
  → Fast, deterministic, no LLM calls

Layer 2: PermissionManager (permissions/manager.ts)
  → Session-aware: remembers user decisions
  → Auto-approval: 73 safe commands, workspace-aware path checks

Layer 3: InspectorPipeline (permissions/inspector-pipeline.ts)
  → SecurityInspector: 25+ threat patterns with confidence scores
  → RepetitionInspector: Per-call stuck detection
  → AuditTrail: Records all decisions
```

### permissions/ — Permission Checks

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `manager.ts` | ~387 | `PermissionManager` | Core permission evaluation, session caching, rule matching |
| `rules.ts` | ~244 | `assessCommandRisk`, `BUILTIN_RULES` | 11 built-in rules + risk scoring (critical/high/medium/low) |
| `auto-approve.ts` | ~622 | `shouldAutoApprove` | Granular auto-approval with YOLO mode, 73 safe commands |
| `command-validator.ts` | ~454 | `CommandValidator` | Shell command validation, dangerous char detection, segment parsing |
| `quote-parser.ts` | ~431 | `parseCommandSegments` | State machine for quote-aware shell parsing |
| `security-inspector.ts` | ~302 | `SecurityInspector` | Pattern-based threat detection with confidence scores (0-1) |
| `repetition-inspector.ts` | ~151 | `RepetitionInspector` | Detect stuck patterns (same call repeated N times in window) |
| `inspector-pipeline.ts` | ~203 | `InspectorPipeline` | Chain: Security → Repetition, first blocker stops |
| `audit.ts` | ~142 | `AuditTrail` | In-memory append-only audit log (max 1000 entries) |
| `trusted-folders.ts` | ~318 | `TrustedFolderManager` | User-designated trusted directories (`~/.ava/trusted-folders.json`) |
| `types.ts` | ~179 | `RiskLevel`, `PermissionRequest` | All permission types |

**Risk Assessment (rules.ts):**
- **Critical:** `rm -rf /`, `dd`, `mkfs`, fork bomb
- **High:** `rm -rf`, `git push --force`, `chmod 777`
- **Medium:** `sudo`, `npm install`, `curl | sh`

**Auto-Approval (auto-approve.ts):**
- 73 safe commands: `ls`, `cat`, `grep`, `git status`, `npm ls`, `echo`, `jq`, etc.
- Workspace-aware: only auto-approves paths within the project
- YOLO mode: approves everything except `/etc/passwd`, SSH keys

### policy/ — Rule Engine

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `engine.ts` | ~325 | `PolicyEngine` | Priority-based rule evaluation, first-match-wins |
| `rules.ts` | ~403 | `BUILTIN_RULES`, `ApprovalMode` | ~50 rules organized by priority bands |
| `matcher.ts` | ~241 | `matchToolName`, `checkCompoundCommand` | Wildcard matching, compound command splitting |
| `types.ts` | ~84 | `PolicyRule`, `PolicyDecisionType` | allow/deny/ask_user decisions |

**Priority Bands:**
- 1000+: Mode-specific overrides (plan mode blocks all writes)
- 900: YOLO mode allows all
- 800: AUTO_EDIT mode allows write operations
- 500-600: Critical safety (SSH keys, /etc/shadow, .env)
- 100-200: Default mode (read=allow, write/execute=ask)
- 0: Fallback (ask_user)

**Approval Modes:** DEFAULT, AUTO_EDIT, YOLO, PLAN

**Compound Command Handling:** `checkCompoundCommand()` splits on `&&`, `||`, `|`, `;` and validates each segment. Pessimistic aggregation: any DENY → DENY overall.

---

## Configuration

### config/ — Settings Management

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `manager.ts` | ~300 | `SettingsManager` | CRUD for settings with Zod validation per category |
| `schema.ts` | ~175 | `SettingsSchema`, category schemas | Zod schemas with range validation |
| `types.ts` | ~326 | `Settings`, `DEFAULT_SETTINGS` | All interfaces + defaults |
| `credentials.ts` | ~200 | `CredentialsManager` | API key management via platform keychain |
| `storage.ts` | ~150 | `loadSettingsFromFile`, `saveSettingsToFile` | File persistence |
| `migration.ts` | ~200 | `migrateSettings` | Version migration for settings format changes |
| `integration.ts` | ~250 | `createAgentConfigFromSettings` | Bridge settings → agent config |
| `export.ts` | ~200 | `exportSettingsToFile`, `importSettingsFromFile` | Import/export/backup |

**Settings Categories:** `provider`, `agent`, `permissions`, `context`, `ui`, `git`, `sandbox`

Each category has: full Zod schema + partial schema (for updates) + TypeScript interface + defaults.

**Key:** `SettingsManager.set('agent', { maxTurns: 100 })` validates the partial update against `PartialAgentSettingsSchema`, merges with existing, validates full result against `AgentSettingsSchema`.

### auth/ — OAuth + PKCE

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `manager.ts` | ~200 | `OAuthManager` | Token lifecycle (issue, refresh, validate) |
| `flows.ts` | ~200 | `startDeviceCodeFlow`, `startPKCEFlow` | OAuth flow implementations |
| `types.ts` | ~150 | `OAuthConfig`, `OAuthTokens` | Auth types + provider configs |
| `storage.ts` | ~100 | Token persistence | Secure credential store |
| `validation.ts` | ~100 | `validateToken` | JWT validation |
| `index.ts` | ~50 | Public API | |

**Supported OAuth Providers:** Anthropic (PKCE), OpenAI/Copilot (device code), Google (PKCE)

---

## Extensibility

### extensions/ — Plugin System

**Why it exists:** Obsidian-style plugins that bundle MCP servers, context files, and tool exclusions.

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `manager.ts` | ~437 | `ExtensionManager` | Install/uninstall/enable/disable, discovery |
| `manifest.ts` | ~172 | `loadExtensionConfig` | Parse `ava-extension.json` config files |
| `storage.ts` | ~145 | `ExtensionStorage` | File system operations, metadata persistence |
| `types.ts` | ~148 | `ExtensionConfig`, `Extension` | Extension types |

**Installation Types:** `local` (copy), `link` (symlink, for dev), `git` (clone)

**Discovery Paths:** `~/.ava/extensions/` (global) + `.ava/extensions/` (project)

**Config Format:** `ava-extension.json` with `mcpServers`, `contextFiles`, `excludeTools`

### hooks/ — Lifecycle Hooks

**Why it exists:** Operators can inject custom behavior at tool execution boundaries via shell scripts.

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `executor.ts` | ~508 | `HookRunner`, `discoverHooks` | Discover and execute hook scripts |
| `factory.ts` | ~300 | `createPreToolUseContext`, `parseHookOutput` | Context creation, output parsing, result merging |
| `types.ts` | ~242 | `HookType`, `HookResult` | 5 hook types, context types |

**Hook Types:** `PreToolUse`, `PostToolUse`, `TaskStart`, `TaskComplete`, `TaskCancel`

**Hook Discovery:**
- Global: `~/.ava/hooks/{HookType}/*.{sh,js,ts,py,rb,ps1}`
- Project: `.ava/hooks/{HookType}/*.{sh,js,ts,py,rb}`

**Protocol:** JSON in via stdin → hook script executes → JSON out via stdout. Can return `{ cancel: true }` to block tool execution.

### mcp/ — Model Context Protocol

**Why it exists:** Connect to external tool servers (GitHub, Slack, databases) via the MCP protocol.

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `client.ts` | ~412 | `MCPClientManager` | Connect/disconnect, discover tools, call tools |
| `bridge.ts` | ~178 | `createToolFromMCP`, `setupMCPTools` | Convert MCP tools → AVA tools + register |
| `discovery.ts` | ~185 | `discoverMCPServers` | Load config from `~/.ava/mcp.json`, `.mcp.json`, etc. |
| `oauth.ts` | ~503 | `startOAuthFlow`, `completeOAuthFlow` | PKCE OAuth for authenticated MCP servers |
| `types.ts` | ~174 | `MCPServerConfig`, `DiscoveredMCPTool` | MCP types |

**Transports:** `stdio` (local process), `sse` (Server-Sent Events), `http` (Streamable HTTP)

**Tool Naming:** MCP tools registered as `mcp_{serverName}_{toolName}`

**Config Paths (first wins):** `~/.ava/mcp.json` → `~/.claude/claude_desktop_config.json` → `.ava/mcp.json` → `.mcp.json`

### skills/ — Auto-Invoked Knowledge

**Why it exists:** Markdown files with YAML frontmatter that auto-activate based on file glob patterns.

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `discovery.ts` | ~227 | `discoverSkills`, `findSkillsForFile` | Find SKILL.md files, match by glob |
| `loader.ts` | ~213 | `loadSkill`, `parseFrontmatter` | Parse YAML frontmatter + markdown content |
| `types.ts` | ~74 | `Skill`, `SkillFrontmatter` | Skill types |
| `index.ts` | ~115 | `getSkills` | Cached skill access (5-min TTL) |

**Discovery:** `.ava/skills/*/SKILL.md` (project) → `~/.ava/skills/*/SKILL.md` (global)

### custom-commands/ — User Slash Commands

**Why it exists:** TOML-based user commands with template placeholders.

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `discovery.ts` | ~198 | `discoverCommands` | Scan for .toml files, derive command names |
| `parser.ts` | ~177 | `parseCommandToml` | Lightweight TOML parser |
| `template.ts` | ~238 | `resolveTemplate` | Resolve `@{file}`, `!{shell}`, `{{args}}` placeholders |
| `types.ts` | ~114 | `CustomCommandDef`, `Placeholder` | Command types |

**Naming:** `commands/git/commit.toml` → `/git:commit` (namespace separator `:`)

**Placeholders:**
- `@{path/to/file.md}` — Inject file contents
- `!{git diff}` — Execute shell command, inject output
- `{{args}}` — User-provided arguments

---

## Infrastructure

### llm/ — LLM Provider Clients

**Why it exists:** Provider-agnostic streaming interface. Factory pattern with lazy-loaded providers.

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `client.ts` | ~266 | `createClient`, `LLMClient` | Factory + interface for streaming completions |
| `providers/anthropic.ts` | ~318 | Anthropic client | Native SSE format, tool use blocks, OAuth |
| `providers/openai.ts` | ~412 | OpenAI client | Chat Completions API, Codex proxy for OAuth |
| `providers/google.ts` | ~236 | Google client | Gemini API |
| `providers/openrouter.ts` | ~206 | OpenRouter client | Multi-model proxy |
| `providers/ollama.ts` | ~235 | Ollama client | Local models |
| `utils/openai-compat.ts` | ~325 | `createOpenAICompatClient` | DRY factory for OpenAI-compatible providers |
| `utils/errors.ts` | ~78 | `classifyHttpError` | HTTP error classification |
| `utils/sse.ts` | ~76 | `parseSSELines`, `readSSEStream` | SSE stream parsing |

**14 Providers:** Anthropic, OpenAI, Google, OpenRouter, DeepSeek, Groq, Mistral, Cohere, Together, xAI, Ollama, GLM, Kimi, Copilot

**DRY Pattern:** DeepSeek, Groq, Mistral, xAI, Together all use `createOpenAICompatClient()` (~15 lines each) because they follow the OpenAI Chat Completions API format.

**Provider Inference:** `createClient()` guesses provider from model name: `claude-*` → anthropic, `gpt-*` → openai, `gemini-*` → google, etc.

### session/ — Session Management

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `manager.ts` | ~772 | `SessionManager` | Session CRUD, LRU cache, checkpoints, forking, auto-save |
| `file-storage.ts` | ~313 | `FileSessionStorage` | Disk persistence (`~/.ava/sessions/*.json`) |
| `resume.ts` | ~365 | `SessionSelector` | Resolve session by: `latest`, numeric index, UUID, prefix, search |
| `doom-loop.ts` | ~260 | `DoomLoopDetector` | Session-level infinite loop detection (3 identical calls) |
| `types.ts` | ~265 | `SessionState`, `Checkpoint`, `TodoItem` | Session types |

**Session Features:**
- **LRU Cache** — Hot sessions in memory (default 10), evicts least recently used
- **Checkpoints** — Named snapshots with git SHA, rollback support
- **Forking** — Branch a session from any checkpoint ("Session (fork #N)" naming)
- **Auto-Save** — Timer-based persistence (default 60s interval)
- **Resume** — `SessionSelector` resolves "latest", "1" (index), UUID prefix, or text search

### bus/ — Message Bus

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `message-bus.ts` | ~346 | `MessageBus` | Pub/sub with request/response correlation |
| `types.ts` | ~151 | `BusMessageType`, message types | 11 message types for tool confirmation, execution, user interaction |

**Key Methods:**
- `subscribe(type, handler)` — Listen for specific message types
- `publish(message)` — Broadcast to subscribers
- `request(req, responseType, timeout)` — Send request, await correlated response
- `confirm(request)` — Tool confirmation flow (checks PolicyEngine first)

**Message Types:** `TOOL_CONFIRMATION_REQUEST/RESPONSE`, `TOOL_EXECUTION_START/SUCCESS/FAILURE`, `ASK_USER_REQUEST/RESPONSE`, `TOOL_POLICY_REJECTION`, `UPDATE_POLICY`, `TOOL_CALLS_UPDATE`

### scheduler/ — Background Tasks

| File | Lines | Key Export | Purpose |
|------|-------|-----------|---------|
| `scheduler.ts` | ~263 | `Scheduler` | Register/unregister tasks, start/stop, concurrency control |
| `types.ts` | ~59 | `ScheduledTask`, `TaskResult` | Task config with interval, scope, enabled flag |

**Concurrency:** Max 3 concurrent tasks (configurable). Timer-based with error handling.

### Other Infrastructure

**question/** — LLM-to-user questions:
- `manager.ts` — `QuestionManager.ask()` returns `Promise<QuestionResult>` (blocks until user answers)
- Timeout: 5 minutes default, auto-rejects

**git/** — Git utilities:
- `git/snapshot.ts` — Create/restore git snapshots for rollback
- `git/types.ts` — `GitConfig` (enabled, autoCommit, branchPrefix, messagePrefix)

**instructions/** — Project/directory instruction loading:
- `loader.ts` — Loads `AVA.md` / `.ava/instructions.md` from project and home dirs

**integrations/** — External service clients:
- `exa.ts` — Exa neural search API client (used by websearch + codesearch)

**types/** — Shared type definitions:
- `llm.ts` — `ChatMessage`, `LLMProvider`, `StreamDelta`, `ToolUseBlock`, `ProviderConfig`

---

## Key Patterns

### 1. Singleton with DI

Every major service uses `let _instance` + `get/set/reset` functions. Tests call `reset()` in `afterEach`. Platform swap calls `set()` at startup.

### 2. Event-Driven Architecture

Components communicate via typed events. Pattern: `on(listener)` returns unsubscribe function, `emit(event)` broadcasts to all listeners. Used in: `AgentEventEmitter`, `SessionManager`, `ExtensionManager`, `HookRunner`, `DiffTracker`, `MessageBus`.

### 3. Zod Validation at Boundaries

All user input (settings, tool params, config files) validated with Zod schemas. Internal code trusts types. Pattern: `defineTool({ schema: z.object({...}) })` auto-validates before `execute()`.

### 4. Strategy Pattern for Compaction

`Compactor` holds an ordered list of `CompactionStrategy` implementations. Tries each in sequence until one succeeds. Easy to add new strategies.

### 5. Factory + Registry

Tools: `defineTool()` → `registerTool()` → `getTool()` → `executeTool()`
LLM: `registerClient()` → `createClient()` → `client.stream()`
Workers: `registry.register()` → `createWorkerTool()` → `delegate_<name>`

### 6. Progressive Fuzzy Matching

Edit tool tries 8 replacement strategies from exact to fuzzy. First match wins. Handles LLM whitespace errors gracefully.

### 7. Barrel Exports

Every module has `index.ts` re-exporting its public API. Top-level `src/index.ts` re-exports all 29 modules. **Caution:** `export *` from multiple modules can cause name collisions — use `as` renames when needed.

---

## Common Tasks

### Adding a New Tool

1. Create `tools/<name>.ts`
2. Use `defineTool()` with Zod schema
3. Import and register in `tools/index.ts`
4. Add to `PLAN_MODE_ALLOWED_TOOLS` or `PLAN_MODE_BLOCKED_TOOLS` in `agent/modes/plan.ts`
5. Add risk level mapping in `tools/registry.ts`

### Adding a New LLM Provider

1. If OpenAI-compatible: Use `createOpenAICompatClient()` in ~15 lines (see `llm/providers/deepseek.ts`)
2. If custom: Implement `LLMClient` interface with `stream()` async generator
3. Register in `llm/providers/index.ts`
4. Add to `LLMProvider` type in `types/llm.ts`
5. Add to `LLMProviderSchema` in `config/schema.ts`
6. Add model entries in `models/registry.ts`

### Adding a New Compaction Strategy

1. Implement `CompactionStrategy` interface (name, compact method)
2. Add to `context/strategies/`
3. Export from `context/strategies/index.ts`
4. Register in `createCompactor()` or `createAggressiveCompactor()` in `compactor.ts`

### Adding a New Worker (Senior Lead)

1. Create definition in `commander/workers/definitions.ts`
2. Specify: name, tools (allowlist), maxTurns, systemPrompt
3. Register via `WorkerRegistry.register()`
4. Worker auto-appears as `delegate_<name>` tool

### Adding a New Policy Rule

1. Add to `BUILTIN_RULES` in `policy/rules.ts`
2. Specify: name, toolName (supports wildcards), decision, priority band, modes
3. Higher priority wins; first match in same priority stops evaluation

### Adding a New Hook Type

1. Add to `HookType` union in `hooks/types.ts`
2. Create context type (`<Name>Context`)
3. Add context creator in `hooks/factory.ts`
4. Add discovery path in `hooks/executor.ts`

---

*Last updated: 2026-02-15 — synthesized from 6 comprehensive module explorations covering all 200+ source files*
