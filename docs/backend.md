# AVA Backend Reference

Condensed module index for `packages/core-v2/` (minimal core) and `packages/extensions/` (built-in extensions).

## Core-v2 (~50 files, ~7K lines)

### agent/ — Turn-based agent loop
- `loop.ts` — `AgentExecutor.run()`: stream LLM → collect tool calls → run middleware → execute (~730 lines)
- `types.ts` — `AgentConfig`, `AgentResult`, `AgentInputs`, `AgentEvent`, `MAX_STEPS` terminate mode
- `repair.ts` — `repairToolName()` — 4 strategies: exact, case-insensitive, hyphen/underscore, prefix match
- `output-files.ts` — `saveOverflowOutput()` saves truncated tool output to `~/.ava/tool-output/`, 7-day lazy cleanup
- `structured-output.ts` — `buildStructuredOutputTool(schema)`, `validateStructuredOutput()`, forced `tool_choice`
- `efficient-results.ts` — `normalizeWhitespace()`, `stripAnsi()`, `smartSummarize()`, `groupGrepResults()`, `efficientToolResult()` dispatcher

### llm/ — LLM client interface
- `client.ts` — `LLMClient` interface, `getAuth()`, provider registry (`registerProvider`/`getProvider`)
- `types.ts` — `ChatMessage`, `StreamDelta`, `ToolDefinition`, `LLMProvider` (16 providers), per-message overrides (`_system`, `_format`, `_variant`)

### tools/ — 7 core tools + registry
- `registry.ts` — `registerTool`/`getTool`/`executeTool` + middleware chain
- `define.ts` — `defineTool()` with Zod validation
- `read.ts`, `write.ts`, `edit.ts`, `bash.ts`, `glob.ts`, `grep.ts` — Core tools
- `pty.ts` — PTY tool using `getPlatform().pty`, ANSI stripping, progress streaming
- `utils.ts` — Path resolution, binary detection, truncation
- `sanitize.ts` — Content sanitization for edit tool
- `edit-replacers.ts` — 8 fuzzy matching strategies

### extensions/ — Extension API + lifecycle + hooks
- `api.ts` — `createExtensionAPI()`, `ExtensionAPI` interface, global registries, `registerHook`/`callHook`
- `manager.ts` — Load/activate/deactivate lifecycle
- `loader.ts` — Discover + import extension modules
- `types.ts` — `Extension`, `ExtensionManifest`, `Disposable`, `ToolMiddleware`, `AgentMode`, `HookHandler`, `HookResult`

### config/ — Extensible settings
- `manager.ts` — `SettingsManager.registerCategory(namespace, schema, defaults)`

### session/ — Session storage + archival + DAG
- `manager.ts` — CRUD, auto-save, pluggable storage, `archive()`, `setBusy()`, `listGlobal()`, `fork()`, `getTree()`, `getBranches()`
- `dag.ts` — DAG traversal: `getAncestors()`, `getDescendants()`, `flattenTree()`, `findRoot()`, `getDepth()`
- `storage.ts` — `SessionStorage` interface + serialization helpers
- `memory-storage.ts` — In-memory storage (default)
- `sqlite-storage.ts` — SQLite-backed storage (persistent)
- `export.ts` — `exportSessionToMarkdown()` + `exportSessionToJSON()`
- `slug.ts` — `generateSlug(goal)` with stop word filtering

### bus/ — Message bus
- `message-bus.ts` — Pure pub/sub + request/response (no policy dependency)

### platform.ts — Platform abstraction
- `IPlatformProvider` with `IFileSystem`, `IShell`, `ICredentialStore`, `IDatabase`, `IPTY`

## Extensions (34+ modules)

### Provider extensions (16)
Each in `providers/<name>/`: anthropic, openai, openrouter, google, deepseek, groq, mistral, cohere, together, xai, ollama, glm, kimi, copilot, litellm, azure.

Shared utilities in `providers/_shared/`: `openai-compat.ts` (factory for OpenAI-compatible providers), `sse.ts` (Server-Sent Events parser), `errors.ts` (provider error handling + `parseRetryAfterMs()`), `transforms.ts` (`filterEmptyContentBlocks`, `enforceAlternatingRoles`).

### Safety: permissions/
- `middleware.ts` — Tool execution middleware (priority 0, blocks/allows), `buildApprovalKey()` with arity fingerprinting
- `bash-parser.ts` — Lightweight bash tokenizer (quotes, pipes, redirects, separators)
- `arity.ts` — 100+ command arity map, `extractCommandPrefix()` for permission fingerprints
- `types.ts` — `PermissionLevel`, `PermissionRule`, risk assessment

### Intelligence: agent-modes/
- `plan-mode.ts` — Read-only tools, structured planning
- `plan-save.ts` — `savePlanToFile()` to `.ava/plans/<timestamp>-<slug>.md`
- `doom-loop.ts` — Detects repeated failed tool calls
- `recovery.ts` — Grace period recovery on agent termination
- `evaluator.ts` — Turn evaluation and scoring

### Quality: validator/
- `pipeline.ts` — `ValidationPipeline.run()` with pluggable validators
- Validators: syntax, typescript, lint, test, self-review

### Team: commander/
- `agent-definition.ts` — Unified `AgentDefinition` type for all tiers, `deniedTools?` field
- `registry.ts` — Central agent registry (register/get/filter)
- `workers.ts` — 15 built-in agents (1 commander, 4 leads, 9 workers + explorer)
- `delegate.ts` — Per-worker delegate tools, spawns child `AgentExecutor`, file cache, budget awareness, worktree isolation, `deniedTools` enforcement
- `explore.ts` — Read-only explore worker definition (7 allowed + 15 denied tools)
- `planning.ts` — Task decomposition with topological sort
- `settings-sync.ts` — Settings → registry bridge
- `index.ts` — Model pack integration (`applyModelPack()` from models extension)

### Hooks: hooks/
- `runner.ts` — `HookRunner` executes PreToolUse/PostToolUse scripts
- `formatter.ts` — Auto-formatter middleware (priority 50), detects biome/prettier/deno

### Tools: tools-extended/
20 additional tools beyond core 6 (create_file, delete_file, websearch, webfetch, bash_background/output/kill, etc.)
- `custom-tools.ts` — Auto-discover user tools from `.ava/tools/` + `~/.ava/tools/`

### Prompts: prompts/
- `families.ts` — `detectModelFamily()`, `FAMILY_PROMPT_SECTIONS` for Claude/GPT/Gemini/Llama
- `builder.ts` — System prompt building with family-based approach

### Context: context/
- Token tracking, compaction, and compression strategies
- `pruneStrategy` — 40K token budget, protected tools (skill/memory_read/load_skill)

### Memory: memory/
- `store.ts` — `MemoryStore` CRUD with categories (project, user, session, debug)
- `tools.ts` — `memory_read`, `memory_write`, `memory_list`, `memory_delete` tools

### Codebase: codebase/
- `indexer.ts` — File discovery + dependency graph
- `symbol-extractor.ts` — Regex-based symbol extraction (TS/JS, Python, Rust, Go, Java, C++)

### Instructions: instructions/
- `subdirectory.ts` — Walk-up AGENTS.md resolution with 3-layer dedup
- `url-loader.ts` — URL instruction fetching with 5s timeout
- `loader.ts` — Accepts `urls?: string[]` from config, `'remote'` scope

### LSP: lsp/
- `client.ts` — Full LSP client (initialize, hover, definition, references, diagnostics, documentSymbols, workspaceSymbols, codeActions, rename)
- `tools.ts` — 9 LSP tools (diagnostics, hover, definition, references, completions, document_symbols, workspace_symbols, code_actions, rename)
- `server-manager.ts` — Per-language server lifecycle management
- `transport.ts` — Content-Length framed JSON-RPC transport
- `queries.ts` — Hover/location/diagnostic formatting helpers

### MCP: mcp/
- `client.ts` — JSON-RPC 2.0 client (initialize, tools, resources, prompts, sampling)
- `manager.ts` — Connection lifecycle (connect → initialize → ready)
- `transport.ts` — Stdio + SSE transports
- `oauth.ts` — PKCE auth code flow + token refresh/revoke
- `reconnect.ts` — Exponential backoff with jitter
- `acp.ts` — ACP protocol implementation (run/stream/steer delegate to server extension events)

### Diff: diff/
- `tracker.ts` — File diff tracking with before/after snapshots
- `summary.ts` — `summarizeDiffSession()` returns files/additions/deletions
- `types.ts` — `FileDiff` with `toolCallIndex`, `messageIndex`
- `index.ts` — `diff:revert-to` event handler, `agent:finish` summary

### File Watcher: file-watcher/
- `FileWatcher` class (polling), watches `.git/HEAD`, emits `git:branch-changed`

### Sharing: sharing/ (stub)
- `/share` command, POST to configured endpoint

### Integrations: integrations/
- `well-known.ts` — `fetchWellKnownConfig(domain)` from `/.well-known/ava`

### Server: server/
- `router.ts` — `node:http` REST server with 6 endpoints (POST /run, GET /stream SSE, POST /steer, GET /status, DELETE /abort, GET /health)
- `session-router.ts` — Maps run IDs to AgentExecutor instances, lifecycle management, SSE event push
- `auth.ts` — Token-based auth via `~/.ava/server-tokens.json`
- `index.ts` — Extension activate, `ava serve` command, `server:register-route` event for other extensions

### Recall: recall/
- `indexer.ts` — SQLite FTS5 virtual table (`recall_fts`), porter stemmer, `indexSession()`, `reindexAll()`
- `search.ts` — FTS5 MATCH queries with BM25 ranking, `searchWithAncestors()` for cross-branch search
- `tool.ts` — `recall` tool definition using `defineTool()`
- `index.ts` — Listens to `agent:finish` to index, registers `recall` tool + `/recall` command

### Recipes: recipes/
- `schema.ts` — Zod schema for Recipe (name, params, steps, schedule)
- `parser.ts` — YAML/JSON parsing, `{{param}}` substitution, `{{steps.X.result}}` chaining
- `runner.ts` — `executeRecipe()`: sequential/parallel step execution, result chaining, error handling
- `index.ts` — Discovers from `.ava/recipes/` + `~/.ava/recipes/`, registers `/recipe` command

### GitHub Bot: github-bot/
- `webhook.ts` — GitHub webhook handler: HMAC-SHA256 signature verification, `@ava` mention extraction, ACL
- `context.ts` — PR/issue context collection via `gh` CLI
- `poster.ts` — Result posting as markdown comments with collapsible details
- `index.ts` — Registers webhook route via `server:register-route` event

### Plugin Reviews: plugins/
- `reviews.ts` — `ReviewStore`: submit/get/average/delete reviews with ratings
- `catalog.ts` — `sortCatalog()`, `filterCatalog()` (by tags, minRating, author)

### Providers: specific additions
- `anthropic/cache.ts` — `addCacheControlMarkers()` for prompt caching
- `openrouter/cache.ts` — OpenRouter cache markers
- `openai/responses-body.ts` — OpenAI Responses API body builder, `shouldUseResponsesAPI()` for GPT-5/o3/o4/Codex
- `mistral/transform.ts` — `truncateMistralIds()` (alphanumeric, max 9 chars)
- `litellm/` — LiteLLM provider using `createOpenAICompatClient` at `localhost:4000/v1`
- `azure/` — Azure OpenAI with `api-key` header and deployment endpoint format

## Frontend Integration (src/ ↔ core-v2)

### Settings Sync — `src/services/settings-sync.ts`
Bidirectional bridge: core-v2 `SettingsManager` events → `CustomEvent('ava:core-settings-changed')`. Loop prevention via `markPushing()`.

### Extension Event Hooks — `src/hooks/useExtensionEvents.ts`
3 reactive SolidJS hooks bridging `onEvent()` → signals: `useExtensionEvent<T>()`, `useExtensionEvents()`, `useExtensionEventLog<T>()`.

### Model Status — `src/hooks/useModelStatus.ts`
Subscribes to `models:updated` + `models:ready` events. Exposes `modelCount`, `lastUpdate`, `refresh()`.

### Context Budget Sync — `src/services/core-bridge.ts`
Subscribes to `context:compacting` and `agent:finish` events → syncs `ContextBudget`. Session store uses reactive `budgetTick` signal.

### Chat ↔ AgentExecutor — `src/hooks/chat/stream-lifecycle.ts`
Chat mode now uses `AgentExecutor` (same as agent mode). All tool execution goes through the full middleware chain. Diff capture via temporary `ToolMiddleware` at priority 25.

## Build & Test

```bash
pnpm build:all                                    # Build everything
npx vitest run packages/core-v2/                  # Core-v2 tests
npx vitest run packages/extensions/               # Extension tests
npx vitest run                                    # All tests (~4,859 tests, ~310 files)
```

## Extension Manifest Format

```json
{
  "name": "ava-permissions",
  "version": "1.0.0",
  "description": "Safety & permission system",
  "main": "src/index.ts",
  "builtIn": true,
  "enabledByDefault": true,
  "priority": 0
}
```

Extensions activate in priority order (0 = first). Each exports `activate(api: ExtensionAPI): Disposable`.
