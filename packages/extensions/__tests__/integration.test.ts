/**
 * Sprint 3 integration test — agent loop + mock provider + permissions + tools.
 *
 * Verifies the full pipeline:
 * 1. Agent loop streams from a mock LLM provider
 * 2. Provider yields tool calls
 * 3. Permissions middleware allows/blocks calls
 * 4. Extended tools execute against mock FS
 * 5. Agent completes with correct result
 */

import type { AgentEvent } from '@ava/core-v2/agent'
import { AgentExecutor } from '@ava/core-v2/agent'
import type {
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
} from '@ava/core-v2/extensions'
import { addToolMiddleware, resetRegistries } from '@ava/core-v2/extensions'
import type {
  ChatMessage,
  LLMClient,
  LLMProvider,
  ProviderConfig,
  StreamDelta,
  ToolUseBlock,
} from '@ava/core-v2/llm'
import { createClient, registerProvider, resetProviders } from '@ava/core-v2/llm'
import type { IPlatformProvider } from '@ava/core-v2/platform'
import { setPlatform } from '@ava/core-v2/platform'
import { defineTool, executeTool, registerTool, resetTools } from '@ava/core-v2/tools'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import * as z from 'zod'

// ─── Mock Platform ──────────────────────────────────────────────────────────

function installMockPlatform() {
  const files = new Map<string, string>()
  const dirs = new Set<string>(['/tmp', '/tmp/project'])

  const fs = {
    files,
    dirs,
    async readFile(path: string): Promise<string> {
      const content = files.get(path)
      if (content === undefined) throw new Error(`ENOENT: ${path}`)
      return content
    },
    async writeFile(path: string, content: string): Promise<void> {
      files.set(path, content)
    },
    async readDir(path: string): Promise<string[]> {
      const prefix = path.endsWith('/') ? path : `${path}/`
      const entries = new Set<string>()
      for (const key of files.keys()) {
        if (key.startsWith(prefix)) {
          const rest = key.slice(prefix.length)
          const name = rest.split('/')[0]
          if (name) entries.add(name)
        }
      }
      return [...entries].sort()
    },
    async exists(path: string): Promise<boolean> {
      return files.has(path) || dirs.has(path)
    },
    async stat(path: string) {
      if (dirs.has(path)) return { isFile: false, isDirectory: true, size: 0, mtime: Date.now() }
      if (files.has(path))
        return {
          isFile: true,
          isDirectory: false,
          size: files.get(path)!.length,
          mtime: Date.now(),
        }
      throw new Error(`ENOENT: ${path}`)
    },
    async isFile(path: string): Promise<boolean> {
      return files.has(path)
    },
    async isDirectory(path: string): Promise<boolean> {
      return dirs.has(path)
    },
    async mkdir(path: string): Promise<void> {
      dirs.add(path)
    },
    async remove(path: string): Promise<void> {
      files.delete(path)
      dirs.delete(path)
    },
    async glob(pattern: string): Promise<string[]> {
      return [...files.keys()].filter((f) => f.includes(pattern.replace(/\*/g, '')))
    },
    async readBinary(): Promise<Uint8Array> {
      return new Uint8Array()
    },
    async writeBinary(): Promise<void> {},
    async readDirWithTypes() {
      return []
    },
    async realpath(path: string) {
      return path
    },
  }

  const platform = {
    fs,
    shell: {
      async exec() {
        return { stdout: '', stderr: '', exitCode: 0 }
      },
      spawn() {
        return {}
      },
    },
    credentials: {
      async get() {
        return null
      },
      async set() {},
      async delete() {},
      async has() {
        return false
      },
    },
    database: {
      async query() {
        return []
      },
      async execute() {},
      async migrate() {},
      async close() {},
    },
  }

  setPlatform(platform as unknown as IPlatformProvider)
  return platform
}

// ─── Mock LLM Provider ─────────────────────────────────────────────────────

type TurnResponse = {
  content?: string
  toolCalls?: ToolUseBlock[]
}

function createMockProvider(turns: TurnResponse[]): LLMClient {
  let turnIndex = 0

  return {
    async *stream(
      _messages: ChatMessage[],
      _config: ProviderConfig,
      _signal?: AbortSignal
    ): AsyncGenerator<StreamDelta, void, unknown> {
      const turn = turns[turnIndex]
      turnIndex++

      if (!turn) {
        yield { content: 'No more turns configured', done: true }
        return
      }

      if (turn.content) {
        yield { content: turn.content }
      }

      if (turn.toolCalls) {
        for (const tc of turn.toolCalls) {
          yield { toolUse: tc }
        }
      }

      yield { done: true }
    },
  }
}

// ─── Helpers ────────────────────────────────────────────────────────────────

function makeToolCall(name: string, input: Record<string, unknown>): ToolUseBlock {
  return { type: 'tool_use', id: `call_${name}_${Date.now()}`, name, input }
}

// ─── Tests ──────────────────────────────────────────────────────────────────

describe('Sprint 3 Integration', () => {
  let platform: ReturnType<typeof installMockPlatform>

  beforeEach(() => {
    resetTools()
    resetProviders()
    resetRegistries()
    platform = installMockPlatform()
  })

  afterEach(() => {
    resetTools()
    resetProviders()
    resetRegistries()
  })

  describe('Agent loop with mock provider', () => {
    it('completes when LLM returns no tool calls', async () => {
      const mockProvider = createMockProvider([{ content: 'Hello! I can help you with that.' }])
      registerProvider('mock', () => mockProvider)

      const agent = new AgentExecutor({
        provider: 'mock' as LLMProvider,
        maxTurns: 5,
        maxTimeMinutes: 1,
      })

      const result = await agent.run(
        { goal: 'Say hello', cwd: '/tmp/project' },
        AbortSignal.timeout(5000)
      )

      expect(result.success).toBe(true)
      expect(result.output).toBe('Hello! I can help you with that.')
      expect(result.turns).toBe(1)
    })

    it('executes tool calls and continues', async () => {
      // Register a simple echo tool
      const echoTool = defineTool({
        name: 'echo',
        description: 'Echo back',
        schema: z.object({ message: z.string() }),
        async execute(input) {
          return { success: true, output: input.message }
        },
      })
      registerTool(echoTool)

      const mockProvider = createMockProvider([
        // Turn 1: LLM calls echo tool
        { content: 'Let me echo that.', toolCalls: [makeToolCall('echo', { message: 'ping' })] },
        // Turn 2: LLM responds with final answer (no tool calls)
        { content: 'The echo returned: ping. Done!' },
      ])
      registerProvider('mock', () => mockProvider)

      const events: AgentEvent[] = []
      const agent = new AgentExecutor(
        { provider: 'mock' as LLMProvider, maxTurns: 5, maxTimeMinutes: 1 },
        (event) => events.push(event)
      )

      const result = await agent.run(
        { goal: 'Echo ping', cwd: '/tmp/project' },
        AbortSignal.timeout(5000)
      )

      expect(result.success).toBe(true)
      expect(result.turns).toBe(2)
      expect(result.output).toBe('The echo returned: ping. Done!')

      // Check events were emitted
      const types = events.map((e) => e.type)
      expect(types).toContain('agent:start')
      expect(types).toContain('turn:start')
      expect(types).toContain('tool:start')
      expect(types).toContain('tool:finish')
      expect(types).toContain('turn:end')
      expect(types).toContain('agent:finish')
    })

    it('stops at max turns', async () => {
      const echoTool = defineTool({
        name: 'echo',
        description: 'Echo',
        schema: z.object({ message: z.string() }),
        async execute(input) {
          return { success: true, output: input.message }
        },
      })
      registerTool(echoTool)

      // Provider always returns tool calls, never stops
      const infiniteTurns = Array.from({ length: 5 }, () => ({
        content: 'Calling echo again...',
        toolCalls: [makeToolCall('echo', { message: 'loop' })],
      }))
      const mockProvider = createMockProvider(infiniteTurns)
      registerProvider('mock', () => mockProvider)

      const agent = new AgentExecutor({
        provider: 'mock' as LLMProvider,
        maxTurns: 3,
        maxTimeMinutes: 1,
      })

      const result = await agent.run(
        { goal: 'Loop forever', cwd: '/tmp/project' },
        AbortSignal.timeout(5000)
      )

      expect(result.success).toBe(false)
      expect(result.terminateMode).toBe('MAX_TURNS')
      expect(result.turns).toBe(3)
    })

    it('handles attempt_completion tool', async () => {
      const mockProvider = createMockProvider([
        {
          content: 'Task done.',
          toolCalls: [
            makeToolCall('attempt_completion', { result: 'All files created successfully.' }),
          ],
        },
      ])
      registerProvider('mock', () => mockProvider)

      const agent = new AgentExecutor({
        provider: 'mock' as LLMProvider,
        maxTurns: 5,
        maxTimeMinutes: 1,
      })

      const result = await agent.run(
        { goal: 'Complete the task', cwd: '/tmp/project' },
        AbortSignal.timeout(5000)
      )

      expect(result.success).toBe(true)
      expect(result.output).toBe('All files created successfully.')
      expect(result.turns).toBe(1)
    })
  })

  describe('Permissions middleware integration', () => {
    it('blocks .git directory writes through the tool pipeline', async () => {
      const writeTool = defineTool({
        name: 'write_file',
        description: 'Write file',
        schema: z.object({ path: z.string(), content: z.string() }),
        async execute(input) {
          platform.fs.files.set(input.path, input.content)
          return { success: true, output: `Wrote ${input.path}` }
        },
      })
      registerTool(writeTool)

      // Add permission middleware that blocks .git writes
      const permMiddleware: ToolMiddleware = {
        name: 'test-permissions',
        priority: 0,
        async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
          const path = (ctx.args.path ?? '') as string
          if (path.includes('/.git/') || path.includes('/.git')) {
            if (
              ctx.toolName !== 'read_file' &&
              ctx.toolName !== 'glob' &&
              ctx.toolName !== 'grep'
            ) {
              return { blocked: true, reason: 'Cannot modify .git directory' }
            }
          }
          return undefined
        },
      }
      addToolMiddleware(permMiddleware)

      // Attempt to write to .git
      const result = await executeTool(
        'write_file',
        { path: '/tmp/project/.git/config', content: 'hacked' },
        { sessionId: 'test', workingDirectory: '/tmp/project' }
      )

      expect(result.success).toBe(false)
      expect(result.error).toContain('.git')
      expect(platform.fs.files.has('/tmp/project/.git/config')).toBe(false)
    })

    it('blocks rm -rf through middleware', async () => {
      const bashTool = defineTool({
        name: 'bash',
        description: 'Execute bash',
        schema: z.object({ command: z.string() }),
        async execute() {
          return { success: true, output: 'executed' }
        },
      })
      registerTool(bashTool)

      const permMiddleware: ToolMiddleware = {
        name: 'test-permissions',
        priority: 0,
        async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
          if (ctx.toolName === 'bash') {
            const cmd = (ctx.args.command ?? '') as string
            if (/rm\s+-rf\s+[/~]/.test(cmd)) {
              return { blocked: true, reason: 'Destructive rm -rf blocked' }
            }
          }
          return undefined
        },
      }
      addToolMiddleware(permMiddleware)

      const result = await executeTool(
        'bash',
        { command: 'rm -rf /' },
        { sessionId: 'test', workingDirectory: '/tmp/project' }
      )

      expect(result.success).toBe(false)
      expect(result.error).toContain('rm -rf')
    })

    it('allows reads to pass through middleware', async () => {
      platform.fs.files.set('/tmp/project/hello.txt', 'Hello, world!')

      const readTool = defineTool({
        name: 'read_file',
        description: 'Read file',
        schema: z.object({ path: z.string() }),
        async execute(input) {
          const content = await platform.fs.readFile(input.path)
          return { success: true, output: content }
        },
      })
      registerTool(readTool)

      // Middleware that auto-approves reads
      const permMiddleware: ToolMiddleware = {
        name: 'test-permissions',
        priority: 0,
        async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
          if (ctx.toolName === 'read_file' || ctx.toolName === 'glob' || ctx.toolName === 'grep') {
            return undefined // allow
          }
          return { blocked: true, reason: 'Not auto-approved' }
        },
      }
      addToolMiddleware(permMiddleware)

      const result = await executeTool(
        'read_file',
        { path: '/tmp/project/hello.txt' },
        { sessionId: 'test', workingDirectory: '/tmp/project' }
      )

      expect(result.success).toBe(true)
      expect(result.output).toBe('Hello, world!')
    })
  })

  describe('Extended tools with mock FS', () => {
    it('create_file creates a new file via platform FS', async () => {
      const createTool = defineTool({
        name: 'create_file',
        description: 'Create a new file',
        schema: z.object({ path: z.string(), content: z.string() }),
        async execute(input) {
          const exists = await platform.fs.exists(input.path)
          if (exists) {
            return { success: false, output: '', error: `File already exists: ${input.path}` }
          }
          await platform.fs.writeFile(input.path, input.content)
          return { success: true, output: `Created ${input.path} (${input.content.length} chars)` }
        },
      })
      registerTool(createTool)

      const result = await executeTool(
        'create_file',
        { path: '/tmp/project/new.txt', content: 'New file content' },
        { sessionId: 'test', workingDirectory: '/tmp/project' }
      )

      expect(result.success).toBe(true)
      expect(result.output).toContain('Created')
      expect(platform.fs.files.get('/tmp/project/new.txt')).toBe('New file content')
    })

    it('create_file fails if file exists', async () => {
      platform.fs.files.set('/tmp/project/existing.txt', 'old content')

      const createTool = defineTool({
        name: 'create_file',
        description: 'Create a new file',
        schema: z.object({ path: z.string(), content: z.string() }),
        async execute(input) {
          const exists = await platform.fs.exists(input.path)
          if (exists) {
            return { success: false, output: '', error: `File already exists: ${input.path}` }
          }
          await platform.fs.writeFile(input.path, input.content)
          return { success: true, output: `Created ${input.path}` }
        },
      })
      registerTool(createTool)

      const result = await executeTool(
        'create_file',
        { path: '/tmp/project/existing.txt', content: 'overwrite' },
        { sessionId: 'test', workingDirectory: '/tmp/project' }
      )

      expect(result.success).toBe(false)
      expect(result.error).toContain('already exists')
      expect(platform.fs.files.get('/tmp/project/existing.txt')).toBe('old content')
    })

    it('ls lists directory contents', async () => {
      platform.fs.files.set('/tmp/project/a.ts', '// a')
      platform.fs.files.set('/tmp/project/b.ts', '// b')
      platform.fs.files.set('/tmp/project/c.ts', '// c')

      const lsTool = defineTool({
        name: 'ls',
        description: 'List directory',
        schema: z.object({
          path: z.string().optional(),
          maxFiles: z.number().optional(),
        }),
        async execute(input, ctx) {
          const dirPath = input.path || ctx.workingDirectory
          const entries = await platform.fs.readDir(dirPath)
          return { success: true, output: entries.join('\n') }
        },
      })
      registerTool(lsTool)

      const result = await executeTool(
        'ls',
        { path: '/tmp/project' },
        { sessionId: 'test', workingDirectory: '/tmp/project' }
      )

      expect(result.success).toBe(true)
      expect(result.output).toContain('a.ts')
      expect(result.output).toContain('b.ts')
      expect(result.output).toContain('c.ts')
    })
  })

  describe('Full pipeline: agent + middleware + tools', () => {
    it('agent creates a file via tool call with permissions', async () => {
      // Register create_file tool
      const createTool = defineTool({
        name: 'create_file',
        description: 'Create a new file',
        schema: z.object({ path: z.string(), content: z.string() }),
        async execute(input) {
          const exists = await platform.fs.exists(input.path)
          if (exists) {
            return { success: false, output: '', error: `File already exists: ${input.path}` }
          }
          await platform.fs.writeFile(input.path, input.content)
          return { success: true, output: `Created ${input.path} (${input.content.length} chars)` }
        },
      })
      registerTool(createTool)

      // Permission middleware: allow create_file but block .git writes
      const permMiddleware: ToolMiddleware = {
        name: 'permissions',
        priority: 0,
        async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
          const path = (ctx.args.path ?? '') as string
          if (path.includes('/.git')) {
            return { blocked: true, reason: 'Cannot modify .git' }
          }
          return undefined
        },
      }
      addToolMiddleware(permMiddleware)

      // Mock provider: calls create_file then completes
      const mockProvider = createMockProvider([
        {
          content: 'Creating the file...',
          toolCalls: [
            makeToolCall('create_file', {
              path: '/tmp/project/hello.ts',
              content: 'console.log("hello")',
            }),
          ],
        },
        {
          content: '',
          toolCalls: [makeToolCall('attempt_completion', { result: 'Created hello.ts' })],
        },
      ])
      registerProvider('mock', () => mockProvider)

      const events: AgentEvent[] = []
      const agent = new AgentExecutor(
        { provider: 'mock' as LLMProvider, maxTurns: 5, maxTimeMinutes: 1 },
        (e) => events.push(e)
      )

      const result = await agent.run(
        { goal: 'Create hello.ts with a console.log', cwd: '/tmp/project' },
        AbortSignal.timeout(5000)
      )

      // Agent should succeed
      expect(result.success).toBe(true)
      expect(result.output).toBe('Created hello.ts')
      expect(result.turns).toBe(2)

      // File should exist in mock FS
      expect(platform.fs.files.get('/tmp/project/hello.ts')).toBe('console.log("hello")')

      // tool:start and tool:finish events should have been emitted
      const toolStarts = events.filter((e) => e.type === 'tool:start')
      expect(toolStarts.length).toBeGreaterThanOrEqual(1)
    })

    it('agent is blocked when trying to write to .git', async () => {
      const writeTool = defineTool({
        name: 'write_file',
        description: 'Write file',
        schema: z.object({ path: z.string(), content: z.string() }),
        async execute(input) {
          await platform.fs.writeFile(input.path, input.content)
          return { success: true, output: `Wrote ${input.path}` }
        },
      })
      registerTool(writeTool)

      // Permission middleware blocks .git
      addToolMiddleware({
        name: 'permissions',
        priority: 0,
        async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
          const path = (ctx.args.path ?? '') as string
          if (path.includes('/.git')) {
            return { blocked: true, reason: 'Cannot modify .git directory' }
          }
          return undefined
        },
      })

      // Provider tries to write to .git, then gives up
      const mockProvider = createMockProvider([
        {
          content: 'Writing to .git...',
          toolCalls: [
            makeToolCall('write_file', {
              path: '/tmp/project/.git/config',
              content: 'hacked',
            }),
          ],
        },
        // After blocked, LLM gives up
        { content: 'I cannot modify .git. Task aborted.' },
      ])
      registerProvider('mock', () => mockProvider)

      const agent = new AgentExecutor({
        provider: 'mock' as LLMProvider,
        maxTurns: 5,
        maxTimeMinutes: 1,
      })

      const _result = await agent.run(
        { goal: 'Hack .git config', cwd: '/tmp/project' },
        AbortSignal.timeout(5000)
      )

      // Agent finishes but didn't actually modify .git
      expect(platform.fs.files.has('/tmp/project/.git/config')).toBe(false)
    })

    it('middleware chain runs in priority order', async () => {
      const order: string[] = []

      const echoTool = defineTool({
        name: 'echo',
        description: 'Echo',
        schema: z.object({ message: z.string() }),
        async execute(input) {
          return { success: true, output: input.message }
        },
      })
      registerTool(echoTool)

      // Add middlewares in reverse priority order
      addToolMiddleware({
        name: 'third',
        priority: 20,
        async before() {
          order.push('third')
          return undefined
        },
      })
      addToolMiddleware({
        name: 'first',
        priority: 0,
        async before() {
          order.push('first')
          return undefined
        },
      })
      addToolMiddleware({
        name: 'second',
        priority: 10,
        async before() {
          order.push('second')
          return undefined
        },
      })

      await executeTool(
        'echo',
        { message: 'test' },
        { sessionId: 'test', workingDirectory: '/tmp/project' }
      )

      expect(order).toEqual(['first', 'second', 'third'])
    })

    it('after-middleware can modify tool results', async () => {
      const echoTool = defineTool({
        name: 'echo',
        description: 'Echo',
        schema: z.object({ message: z.string() }),
        async execute(input) {
          return { success: true, output: input.message }
        },
      })
      registerTool(echoTool)

      addToolMiddleware({
        name: 'uppercaser',
        priority: 0,
        async after(_ctx, result) {
          return { result: { ...result, output: result.output.toUpperCase() } }
        },
      })

      const result = await executeTool(
        'echo',
        { message: 'hello' },
        { sessionId: 'test', workingDirectory: '/tmp/project' }
      )

      expect(result.output).toBe('HELLO')
    })
  })

  describe('Event system integration', () => {
    it('agent events flow through the global event bus', async () => {
      const mockProvider = createMockProvider([{ content: 'Done.' }])
      registerProvider('mock', () => mockProvider)

      const _busEvents: unknown[] = []
      // We can't use the ExtensionAPI.on() in this test context since
      // we're testing the raw event emission. But emitEvent is called
      // by both AgentExecutor and executeTool, so we test via the
      // onEvent callback.

      const events: AgentEvent[] = []
      const agent = new AgentExecutor(
        { provider: 'mock' as LLMProvider, maxTurns: 5, maxTimeMinutes: 1 },
        (event) => events.push(event)
      )

      await agent.run({ goal: 'Quick test', cwd: '/tmp/project' }, AbortSignal.timeout(5000))

      const types = events.map((e) => e.type)
      expect(types[0]).toBe('agent:start')
      expect(types[1]).toBe('turn:start')
      expect(types).toContain('thought')
      expect(types).toContain('turn:end')
      expect(types[types.length - 1]).toBe('agent:finish')
    })

    it('agent abort signal stops execution', async () => {
      const controller = new AbortController()

      // Provider that takes "time" — abort between turns
      const echoTool = defineTool({
        name: 'echo',
        description: 'Echo',
        schema: z.object({ message: z.string() }),
        async execute(input) {
          // Abort after first tool call
          controller.abort()
          return { success: true, output: input.message }
        },
      })
      registerTool(echoTool)

      const mockProvider = createMockProvider([
        { content: 'Calling tool...', toolCalls: [makeToolCall('echo', { message: 'ping' })] },
        { content: 'This should not be reached.' },
      ])
      registerProvider('mock', () => mockProvider)

      const agent = new AgentExecutor({
        provider: 'mock' as LLMProvider,
        maxTurns: 10,
        maxTimeMinutes: 1,
      })

      const result = await agent.run({ goal: 'Test abort', cwd: '/tmp/project' }, controller.signal)

      expect(result.success).toBe(false)
      expect(result.terminateMode).toBe('ABORTED')
      // Should only have done 1-2 turns before abort
      expect(result.turns).toBeLessThanOrEqual(2)
    })
  })

  describe('Provider registration', () => {
    it('createClient throws for unregistered provider', () => {
      expect(() => {
        createClient('nonexistent')
      }).toThrow(/No LLM provider registered/)
    })

    it('multiple providers can coexist', async () => {
      const mockA = createMockProvider([{ content: 'I am provider A' }])
      const mockB = createMockProvider([{ content: 'I am provider B' }])

      registerProvider('provider-a', () => mockA)
      registerProvider('provider-b', () => mockB)

      const agentA = new AgentExecutor({
        provider: 'provider-a' as LLMProvider,
        maxTurns: 1,
        maxTimeMinutes: 1,
      })
      const agentB = new AgentExecutor({
        provider: 'provider-b' as LLMProvider,
        maxTurns: 1,
        maxTimeMinutes: 1,
      })

      const [resultA, resultB] = await Promise.all([
        agentA.run({ goal: 'Who are you?', cwd: '/tmp' }, AbortSignal.timeout(5000)),
        agentB.run({ goal: 'Who are you?', cwd: '/tmp' }, AbortSignal.timeout(5000)),
      ])

      expect(resultA.output).toBe('I am provider A')
      expect(resultB.output).toBe('I am provider B')
    })
  })
})
