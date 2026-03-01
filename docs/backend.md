# AVA Backend Reference

Condensed module index for `packages/core-v2/` (minimal core) and `packages/extensions/` (built-in extensions).

## Core-v2 (~40 files, ~5K lines)

### agent/ — Turn-based agent loop
- `loop.ts` — `AgentExecutor.run()`: stream LLM → collect tool calls → run middleware → execute
- `types.ts` — `AgentConfig`, `AgentResult`, `AgentInputs`, `AgentEvent`

### llm/ — LLM client interface
- `client.ts` — `LLMClient` interface, `getAuth()`, provider registry (`registerProvider`/`getProvider`)
- `types.ts` — `ChatMessage`, `StreamDelta`, `ToolDefinition`, `LLMProvider` (14 providers)

### tools/ — 6 core tools + registry
- `registry.ts` — `registerTool`/`getTool`/`executeTool` + middleware chain
- `define.ts` — `defineTool()` with Zod validation
- `read.ts`, `write.ts`, `edit.ts`, `bash.ts`, `glob.ts`, `grep.ts` — Core tools
- `utils.ts` — Path resolution, binary detection, truncation
- `sanitize.ts` — Content sanitization for edit tool
- `edit-replacers.ts` — 8 fuzzy matching strategies

### extensions/ — Extension API + lifecycle
- `api.ts` — `createExtensionAPI()`, `ExtensionAPI` interface, global registries
- `manager.ts` — Load/activate/deactivate lifecycle
- `loader.ts` — Discover + import extension modules
- `types.ts` — `Extension`, `ExtensionManifest`, `Disposable`, `ToolMiddleware`, `AgentMode`

### config/ — Extensible settings
- `manager.ts` — `SettingsManager.registerCategory(namespace, schema, defaults)`

### session/ — Session storage
- `manager.ts` — CRUD, auto-save, pluggable storage backend
- `storage.ts` — `SessionStorage` interface + serialization helpers
- `memory-storage.ts` — In-memory storage (default)
- `sqlite-storage.ts` — SQLite-backed storage (persistent)

### bus/ — Message bus
- `message-bus.ts` — Pure pub/sub + request/response (no policy dependency)

### platform.ts — Platform abstraction
- `IPlatformProvider` with `IFileSystem`, `IShell`, `ICredentialStore`, `IDatabase`, `IPTY`

## Extensions (25 modules)

### Provider extensions (14)
Each in `providers/<name>/`: anthropic, openai, openrouter, google, deepseek, groq, mistral, cohere, together, xai, ollama, glm, kimi, copilot.

Shared utilities in `providers/_shared/`: `openai-compat.ts` (factory for OpenAI-compatible providers), `sse.ts` (Server-Sent Events parser), `errors.ts` (provider error handling).

### Safety: permissions/
- `middleware.ts` — Tool execution middleware (priority 0, blocks/allows)
- `types.ts` — `PermissionLevel`, `PermissionRule`, risk assessment

### Intelligence: agent-modes/
- `plan-mode.ts` — Read-only tools, structured planning
- `doom-loop.ts` — Detects repeated failed tool calls
- `recovery.ts` — Grace period recovery on agent termination
- `evaluator.ts` — Turn evaluation and scoring

### Quality: validator/
- `pipeline.ts` — `ValidationPipeline.run()` with pluggable validators
- Validators: syntax, typescript, lint, test, self-review

### Team: commander/
- `agent-definition.ts` — Unified `AgentDefinition` type for all tiers
- `registry.ts` — Central agent registry (register/get/filter)
- `workers.ts` — 13 built-in agents (1 commander, 4 leads, 8 workers)
- `delegate.ts` — Per-worker delegate tools, spawns child `AgentExecutor`
- `planning.ts` — Task decomposition with topological sort
- `settings-sync.ts` — Settings → registry bridge

### Hooks: hooks/
- `runner.ts` — `HookRunner` executes PreToolUse/PostToolUse scripts

### Tools: tools-extended/
18 additional tools beyond core 6 (create_file, delete_file, websearch, webfetch, etc.)

### Prompts: prompts/
System prompt building with model-specific variants.

### Context: context/
Token tracking, compaction, and compression strategies.

### Memory: memory/
- `store.ts` — `MemoryStore` CRUD with categories (project, user, session, debug)
- `tools.ts` — `memory_read`, `memory_write`, `memory_list`, `memory_delete` tools

### Codebase: codebase/
- `indexer.ts` — File discovery + dependency graph
- `symbol-extractor.ts` — Regex-based symbol extraction (TS/JS, Python, Rust, Go, Java, C++)

### LSP: lsp/
- `client.ts` — Full LSP client (initialize, hover, definition, references, diagnostics)
- `server-manager.ts` — Per-language server lifecycle management
- `transport.ts` — Content-Length framed JSON-RPC transport
- `queries.ts` — Hover/location/diagnostic formatting helpers

### MCP: mcp/
- `client.ts` — JSON-RPC 2.0 client (initialize, tools, resources, prompts, sampling)
- `manager.ts` — Connection lifecycle (connect → initialize → ready)
- `transport.ts` — Stdio + SSE transports
- `oauth.ts` — PKCE auth code flow + token refresh/revoke
- `reconnect.ts` — Exponential backoff with jitter

## Build & Test

```bash
pnpm build:all                                    # Build everything
npx vitest run packages/core-v2/                  # Core-v2 tests
npx vitest run packages/extensions/               # Extension tests
npx vitest run                                    # All tests (~3,857 tests, ~240 files)
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
