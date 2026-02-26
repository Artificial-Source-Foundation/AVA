# AVA Extension Examples

This directory contains working examples that demonstrate how to build AVA extensions.

## Extension Anatomy

Every extension has:

1. **Manifest** (`ava-extension.json`) — metadata, priority, enabled by default
2. **Entry point** (`src/index.ts`) — exports `activate(api: ExtensionAPI): Disposable`
3. **Types** (`src/types.ts`) — optional, for extension-specific interfaces

```
my-extension/
├── ava-extension.json    # { name, version, builtIn, enabledByDefault, priority }
├── src/
│   ├── index.ts          # activate(api) → Disposable
│   └── types.ts          # Extension-specific types
```

## ExtensionAPI Methods

| Method | Purpose |
|--------|---------|
| `registerTool(tool)` | Add a tool to the agent's toolbox |
| `registerProvider(name, factory)` | Add an LLM provider |
| `registerAgentMode(mode)` | Add an agent mode (plan, team, etc.) |
| `addToolMiddleware(middleware)` | Intercept tool execution pipeline |
| `on(event, handler)` | Subscribe to events |
| `emit(event, data)` | Emit events (cross-extension communication) |
| `storage.get/set/delete/keys` | Per-extension private key-value storage |
| `log.debug/info/warn/error` | Extension-scoped logging |

All registration methods return a `Disposable` — call `.dispose()` to unregister.

## Examples

- [`extensions/word-count-tool`](extensions/word-count-tool/) — Simple tool extension
- [`extensions/rate-limiter`](extensions/rate-limiter/) — Tool middleware extension
- [`extensions/custom-provider`](extensions/custom-provider/) — LLM provider extension

## Built-in Extension Examples

Look at the actual built-in extensions in `packages/extensions/` for production patterns:

| Extension | Pattern | Key File |
|-----------|---------|----------|
| `providers/anthropic` | LLM provider | `src/client.ts` |
| `providers/_shared` | OpenAI-compat factory | `src/openai-compat.ts` |
| `permissions` | Tool middleware | `src/middleware.ts` |
| `agent-modes` | Agent mode | `src/plan-mode.ts` |
| `hooks` | Lifecycle hooks | `src/runner.ts` |
| `validator` | QA pipeline | `src/pipeline.ts` |
| `commander` | Team hierarchy | `src/router.ts` |
