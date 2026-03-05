# AVA Plugin SDK

Build extensions for AVA using the same API that powers built-in features.

---

## Quick Start

```bash
# Scaffold a new plugin
ava plugin init my-plugin

# Structure created:
# my-plugin/
#   ava-extension.json    — Plugin manifest
#   package.json          — NPM package
#   tsconfig.json         — TypeScript config
#   src/index.ts          — Entry point
#   src/index.test.ts     — Tests

cd my-plugin
pnpm install
pnpm run build
pnpm run test
```

---

## Plugin Anatomy

### Manifest (`ava-extension.json`)

```json
{
  "name": "my-plugin",
  "version": "0.1.0",
  "description": "What your plugin does",
  "main": "dist/index.js",
  "capabilities": ["tools", "commands"],
  "priority": 50,
  "settings": {
    "apiKey": {
      "type": "string",
      "default": "",
      "description": "API key for the service"
    }
  }
}
```

| Field | Required | Description |
|-------|----------|-------------|
| `name` | Yes | Unique plugin identifier (kebab-case) |
| `version` | Yes | Semver version string |
| `description` | No | Short description |
| `main` | Yes | Entry point (relative to plugin root) |
| `capabilities` | No | What the plugin registers |
| `priority` | No | Load order (lower = earlier, default 50) |
| `settings` | No | Configurable settings schema |

### Entry Point (`src/index.ts`)

Every plugin exports an `activate` function:

```typescript
import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  // Register your tools, commands, middleware, etc.
  const toolDisposable = api.registerTool(myTool)
  const cmdDisposable = api.registerCommand(myCommand)

  return {
    dispose() {
      toolDisposable.dispose()
      cmdDisposable.dispose()
    },
  }
}
```

The `Disposable` return value is called when the plugin is deactivated — clean up all registrations here.

---

## ExtensionAPI Reference

### Tools

```typescript
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

const myTool = defineTool({
  name: 'my_tool',
  description: 'What this tool does',
  schema: z.object({
    input: z.string().describe('The input to process'),
  }),
  async execute(input, ctx) {
    return { success: true, output: `Processed: ${input.input}` }
  },
})

api.registerTool(myTool)
```

### Commands

```typescript
api.registerCommand({
  name: 'greet',
  description: 'Say hello. Usage: /greet [name]',
  async execute(args, ctx) {
    return `Hello, ${args.trim() || 'world'}!`
  },
})
```

### Middleware

Intercept tool calls before or after execution:

```typescript
api.addToolMiddleware({
  name: 'my-middleware',
  priority: 50, // Lower = runs first

  async before(ctx) {
    // Block a tool call:
    // return { blocked: true, reason: 'Not allowed' }

    // Modify args:
    // return { args: { ...ctx.args, modified: true } }

    // Pass through:
    return undefined
  },

  async after(ctx, result) {
    // Modify result:
    // return { result: { ...result, output: result.output + '\n[modified]' } }
    return undefined
  },
})
```

### Agent Modes

```typescript
api.registerAgentMode({
  name: 'read-only',
  description: 'Only allow read operations',
  filterTools(tools) {
    return tools.filter(t => !['write_file', 'edit', 'bash'].includes(t.name))
  },
  systemPrompt(base) {
    return base + '\nYou are in read-only mode. Do not modify any files.'
  },
})
```

### Providers

```typescript
api.registerProvider('my-llm', () => ({
  async *stream(messages, config, signal) {
    // Yield StreamDelta objects
    yield { content: 'Hello from my LLM!' }
    yield { done: true, usage: { inputTokens: 10, outputTokens: 5 } }
  },
}))
```

### Events

```typescript
// Listen
api.on('agent:turn:start', (data) => {
  api.log.info('Turn started', data)
})

// Emit custom events
api.emit('my-plugin:action', { type: 'something' })
```

### Storage

Per-plugin private key-value storage:

```typescript
await api.storage.set('key', { count: 42 })
const data = await api.storage.get<{ count: number }>('key')
const allKeys = await api.storage.keys()
await api.storage.delete('key')
```

### Logging

```typescript
api.log.debug('Verbose detail')
api.log.info('Normal info')
api.log.warn('Warning')
api.log.error('Error occurred')
```

### Platform

Access filesystem and shell through the platform abstraction:

```typescript
const content = await api.platform.fs.readFile('/path/to/file')
const exists = await api.platform.fs.exists('/path')
const result = await api.platform.shell.exec('git status')
```

---

## Testing Your Plugin

Use `createMockExtensionAPI()` for isolated, fast tests:

```typescript
import { describe, expect, it } from 'vitest'
import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { activate } from './index.js'

describe('my-plugin', () => {
  it('registers a tool', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(registeredTools).toHaveLength(1)
    expect(registeredTools[0].definition.name).toBe('my_tool')
    disposable.dispose()
    expect(registeredTools).toHaveLength(0)
  })

  it('executes the tool', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    activate(api)
    const result = await registeredTools[0].execute(
      { input: 'test' },
      { sessionId: 's', workingDirectory: '/', signal: new AbortController().signal }
    )
    expect(result.success).toBe(true)
  })
})
```

The mock API provides tracking arrays for everything registered:
- `registeredTools`, `registeredCommands`, `registeredModes`
- `registeredValidators`, `registeredProviders`, `registeredMiddleware`
- `emittedEvents`, `eventHandlers`

All `api.log.*` methods are `vi.fn()` spies you can assert on.

---

## Publishing

### Catalog Format

Plugins are listed in the community catalog (`plugin-catalog.json`):

```json
{
  "id": "my-plugin",
  "name": "My Plugin",
  "description": "What it does",
  "category": "workflow",
  "version": "1.0.0",
  "source": "community",
  "trust": "reviewed",
  "changelogSummary": "Initial release",
  "repo": "github-user/ava-my-plugin",
  "downloadUrl": "https://github.com/.../releases/download/v1.0.0/my-plugin-1.0.0.tgz"
}
```

### Steps

1. Build your plugin: `pnpm run build`
2. Run tests: `pnpm run test`
3. Create a GitHub release with the built tarball
4. Submit a PR to the plugin catalog repository

---

## Examples

Working example plugins with full source and tests:

| Plugin | Demonstrates | Location |
|--------|-------------|----------|
| [timestamp-tool](../../docs/examples/plugins/timestamp-tool/) | `registerTool()`, `defineTool()`, Zod schema | `docs/examples/plugins/timestamp-tool/` |
| [file-stats](../../docs/examples/plugins/file-stats/) | `registerTool()`, `platform.fs` access | `docs/examples/plugins/file-stats/` |
| [polite-middleware](../../docs/examples/plugins/polite-middleware/) | `addToolMiddleware()`, priority | `docs/examples/plugins/polite-middleware/` |
| [session-notes](../../docs/examples/plugins/session-notes/) | `registerCommand()`, `storage` API | `docs/examples/plugins/session-notes/` |
| [event-logger](../../docs/examples/plugins/event-logger/) | `api.on()`, `emit()`, events + storage | `docs/examples/plugins/event-logger/` |
| [deploy-command](../../docs/examples/plugins/deploy-command/) | command argument parsing + safety checks | `docs/examples/plugins/deploy-command/` |
| [react-patterns](../../docs/examples/plugins/react-patterns/) | project-specific guidance registration pattern | `docs/examples/plugins/react-patterns/` |
