/**
 * Sprint 4 integration test — agent modes, hooks, validator, commander.
 *
 * Verifies:
 * 1. Plan mode filters tools in the agent loop (read-only)
 * 2. Hooks middleware blocks tool calls via PreToolUse cancel
 * 3. Doom loop detector fires events on repeated identical calls
 * 4. Commander routes tasks to appropriate workers
 * 5. Full pipeline: agent + plan mode + hooks + permissions working together
 */

import type { AgentEvent } from '@ava/core-v2/agent'
import { AgentExecutor } from '@ava/core-v2/agent'
import { MessageBus } from '@ava/core-v2/bus'
import {
  addToolMiddleware,
  createExtensionAPI,
  emitEvent,
  getAgentModes,
  resetRegistries,
} from '@ava/core-v2/extensions'
import type {
  ChatMessage,
  LLMClient,
  LLMProvider,
  ProviderConfig,
  StreamDelta,
  ToolUseBlock,
} from '@ava/core-v2/llm'
import { registerProvider, resetProviders } from '@ava/core-v2/llm'
import type { IPlatformProvider } from '@ava/core-v2/platform'
import { setPlatform } from '@ava/core-v2/platform'
import { createSessionManager } from '@ava/core-v2/session'
import { defineTool, executeTool, registerTool, resetTools } from '@ava/core-v2/tools'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import * as z from 'zod'

// Extensions under test
import {
  check,
  configure as configureDoomLoop,
  registerDoomLoop,
  resetDoomLoop,
} from '../agent-modes/src/doom-loop.js'
import { planAgentMode, resetPlanMode } from '../agent-modes/src/plan-mode.js'
import { activate as activateCommander } from '../commander/src/index.js'
import { analyzeTask, selectWorker } from '../commander/src/router.js'
import { BUILTIN_WORKERS } from '../commander/src/workers.js'
import { createHooksMiddleware } from '../hooks/src/index.js'
import { registerHook, resetHooks } from '../hooks/src/runner.js'
import type { PreToolUseContext } from '../hooks/src/types.js'

// ─── Mock Helpers ──────────────────────────────────────────────────────────

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

      if (turn.content) yield { content: turn.content }
      if (turn.toolCalls) {
        for (const tc of turn.toolCalls) yield { toolUse: tc }
      }
      yield { done: true }
    },
  }
}

function makeToolCall(name: string, input: Record<string, unknown>): ToolUseBlock {
  return { type: 'tool_use', id: `call_${name}_${Date.now()}`, name, input }
}

function installMockPlatform() {
  const files = new Map<string, string>()
  const dirs = new Set<string>(['/tmp', '/tmp/project'])

  const platform = {
    fs: {
      files,
      dirs,
      async readFile(path: string) {
        const content = files.get(path)
        if (content === undefined) throw new Error(`ENOENT: ${path}`)
        return content
      },
      async writeFile(path: string, content: string) {
        files.set(path, content)
      },
      async readDir(path: string) {
        const prefix = path.endsWith('/') ? path : `${path}/`
        const entries = new Set<string>()
        for (const key of files.keys()) {
          if (key.startsWith(prefix)) {
            const name = key.slice(prefix.length).split('/')[0]
            if (name) entries.add(name)
          }
        }
        return [...entries].sort()
      },
      async exists(path: string) {
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
      async isFile(path: string) {
        return files.has(path)
      },
      async isDirectory(path: string) {
        return dirs.has(path)
      },
      async mkdir(path: string) {
        dirs.add(path)
      },
      async remove(path: string) {
        files.delete(path)
        dirs.delete(path)
      },
      async glob(pattern: string) {
        return [...files.keys()].filter((f) => f.includes(pattern.replace(/\*/g, '')))
      },
      async readBinary() {
        return new Uint8Array()
      },
      async writeBinary() {},
      async readDirWithTypes() {
        return []
      },
      async realpath(path: string) {
        return path
      },
    },
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

function makeExtApi() {
  return createExtensionAPI('test', new MessageBus(), createSessionManager())
}

// ─── Test Suite ────────────────────────────────────────────────────────────

describe('Sprint 4 Integration', () => {
  let platform: ReturnType<typeof installMockPlatform>

  beforeEach(() => {
    resetTools()
    resetProviders()
    resetRegistries()
    resetPlanMode()
    resetDoomLoop()
    resetHooks()
    platform = installMockPlatform()
  })

  afterEach(() => {
    resetTools()
    resetProviders()
    resetRegistries()
    resetPlanMode()
    resetDoomLoop()
    resetHooks()
  })

  // ─── Plan Mode + Agent Loop ──────────────────────────────────────────────

  describe('Plan mode integration', () => {
    it('agent in plan mode can read files but tools are filtered', async () => {
      // Register read + write tools
      registerTool(
        defineTool({
          name: 'read_file',
          description: 'Read a file',
          schema: z.object({ path: z.string() }),
          async execute(input) {
            return { success: true, output: await platform.fs.readFile(input.path) }
          },
        })
      )
      registerTool(
        defineTool({
          name: 'write_file',
          description: 'Write a file',
          schema: z.object({ path: z.string(), content: z.string() }),
          async execute(input) {
            await platform.fs.writeFile(input.path, input.content)
            return { success: true, output: `Wrote ${input.path}` }
          },
        })
      )
      registerTool(
        defineTool({
          name: 'glob',
          description: 'Find files',
          schema: z.object({ pattern: z.string() }),
          async execute() {
            return { success: true, output: '[]' }
          },
        })
      )

      // Register plan mode via ExtensionAPI
      const extApi = makeExtApi()
      extApi.registerAgentMode(planAgentMode)

      // Set up mock FS and provider
      platform.fs.files.set('/tmp/project/src/app.ts', 'const x = 1')
      const mockProvider = createMockProvider([
        {
          content: 'Reading the file...',
          toolCalls: [makeToolCall('read_file', { path: '/tmp/project/src/app.ts' })],
        },
        {
          content: '',
          toolCalls: [makeToolCall('attempt_completion', { result: 'Read complete' })],
        },
      ])
      registerProvider('mock', () => mockProvider)

      // Run agent in plan mode
      const events: AgentEvent[] = []
      const agent = new AgentExecutor(
        { provider: 'mock' as LLMProvider, maxTurns: 5, maxTimeMinutes: 1, toolMode: 'plan' },
        (e) => events.push(e)
      )

      const result = await agent.run(
        { goal: 'Review the codebase', cwd: '/tmp/project' },
        AbortSignal.timeout(5000)
      )

      expect(result.success).toBe(true)
      expect(result.output).toBe('Read complete')

      // Verify events emitted correctly
      const types = events.map((e) => e.type)
      expect(types).toContain('agent:start')
      expect(types).toContain('tool:start')
      expect(types).toContain('agent:finish')
    })

    it('plan mode filterTools removes write tools', () => {
      const tools = [
        { name: 'read_file', description: 'Read', parameters: {} },
        { name: 'write_file', description: 'Write', parameters: {} },
        { name: 'glob', description: 'Glob', parameters: {} },
        { name: 'edit', description: 'Edit', parameters: {} },
        { name: 'bash', description: 'Bash', parameters: {} },
        { name: 'attempt_completion', description: 'Complete', parameters: {} },
      ]

      const filtered = planAgentMode.filterTools!(tools)
      const names = filtered.map((t) => t.name)

      expect(names).toContain('read_file')
      expect(names).toContain('glob')
      expect(names).toContain('attempt_completion')
      expect(names).not.toContain('write_file')
      expect(names).not.toContain('edit')
      expect(names).not.toContain('bash')
    })

    it('plan mode system prompt appends instruction', () => {
      const base = 'You are AVA.'
      const modified = planAgentMode.systemPrompt!(base)

      expect(modified).toContain('You are AVA.')
      expect(modified).toContain('PLAN MODE')
      expect(modified).toContain('no file modifications')
    })
  })

  // ─── Hooks as Middleware ──────────────────────────────────────────────────

  describe('Hooks middleware integration', () => {
    it('PreToolUse hook blocks tool execution through middleware chain', async () => {
      registerTool(
        defineTool({
          name: 'echo',
          description: 'Echo',
          schema: z.object({ message: z.string() }),
          async execute(input) {
            return { success: true, output: input.message }
          },
        })
      )

      // Register a blocking hook
      registerHook({
        type: 'PreToolUse',
        name: 'secret-guard',
        handler: async (ctx: PreToolUseContext) => {
          if (ctx.parameters?.message === 'secret') {
            return { cancel: true, errorMessage: 'Cannot echo secrets' }
          }
          return {}
        },
      })

      // Wire hooks into the tool middleware chain
      addToolMiddleware(createHooksMiddleware())

      // Attempt to echo a secret — should be blocked
      const blocked = await executeTool(
        'echo',
        { message: 'secret' },
        {
          sessionId: 'test',
          workingDirectory: '/tmp',
        }
      )
      expect(blocked.success).toBe(false)
      expect(blocked.error).toContain('secrets')

      // Normal message should pass
      const passed = await executeTool(
        'echo',
        { message: 'hello' },
        {
          sessionId: 'test',
          workingDirectory: '/tmp',
        }
      )
      expect(passed.success).toBe(true)
      expect(passed.output).toBe('hello')
    })

    it('hooks middleware runs after permissions middleware in priority order', async () => {
      const order: string[] = []

      registerTool(
        defineTool({
          name: 'echo',
          description: 'Echo',
          schema: z.object({ message: z.string() }),
          async execute(input) {
            return { success: true, output: input.message }
          },
        })
      )

      // Permissions middleware at priority 0
      addToolMiddleware({
        name: 'permissions',
        priority: 0,
        async before() {
          order.push('permissions')
          return undefined
        },
      })

      // Register a tracking hook
      registerHook({
        type: 'PreToolUse',
        name: 'tracker',
        handler: async () => {
          order.push('hook')
          return {}
        },
      })

      // Hooks middleware at priority 10
      addToolMiddleware(createHooksMiddleware())

      await executeTool(
        'echo',
        { message: 'test' },
        {
          sessionId: 'test',
          workingDirectory: '/tmp',
        }
      )

      // Permissions (priority 0) should run before hooks (priority 10)
      expect(order).toEqual(['permissions', 'hook'])
    })

    it('agent with hooks: PreToolUse blocks dangerous operation', async () => {
      registerTool(
        defineTool({
          name: 'write_file',
          description: 'Write',
          schema: z.object({ path: z.string(), content: z.string() }),
          async execute(input) {
            await platform.fs.writeFile(input.path, input.content)
            return { success: true, output: `Wrote ${input.path}` }
          },
        })
      )

      // Hook that blocks writes to /etc
      registerHook({
        type: 'PreToolUse',
        name: 'system-guard',
        handler: async (ctx: PreToolUseContext) => {
          const path = (ctx.parameters?.path ?? '') as string
          if (path.startsWith('/etc')) {
            return { cancel: true, errorMessage: 'Cannot write to system directories' }
          }
          return {}
        },
      })
      addToolMiddleware(createHooksMiddleware())

      // Provider tries to write to /etc then completes
      const mockProvider = createMockProvider([
        {
          content: 'Writing system config...',
          toolCalls: [makeToolCall('write_file', { path: '/etc/passwd', content: 'hacked' })],
        },
        { content: 'Could not write. Giving up.' },
      ])
      registerProvider('mock', () => mockProvider)

      const agent = new AgentExecutor({
        provider: 'mock' as LLMProvider,
        maxTurns: 5,
        maxTimeMinutes: 1,
      })

      await agent.run(
        { goal: 'Modify system config', cwd: '/tmp/project' },
        AbortSignal.timeout(5000)
      )

      // File should NOT exist
      expect(platform.fs.files.has('/etc/passwd')).toBe(false)
    })
  })

  // ─── Doom Loop Detection ─────────────────────────────────────────────────

  describe('Doom loop detection integration', () => {
    it('detects repeated tool calls and emits event via extension API', () => {
      const doomEvents: unknown[] = []
      const extApi = makeExtApi()

      // Subscribe to doom-loop events
      extApi.on('doom-loop:detected', (data) => doomEvents.push(data))

      // Register doom loop detector with this API
      registerDoomLoop(extApi)

      // Simulate 3 identical tool:finish events
      for (let i = 0; i < 3; i++) {
        emitEvent('tool:finish', {
          name: 'read_file',
          args: { path: '/tmp/same.ts' },
          sessionId: 's1',
        })
      }

      expect(doomEvents).toHaveLength(1)
      const event = doomEvents[0] as { tool: string; consecutiveCount: number }
      expect(event.tool).toBe('read_file')
      expect(event.consecutiveCount).toBe(3)
    })

    it('doom loop check with configurable threshold', () => {
      configureDoomLoop({ threshold: 2 })

      // First call — no detection
      const r1 = check('s1', 'echo', { message: 'same' })
      expect(r1.detected).toBe(false)
      expect(r1.consecutiveCount).toBe(1)

      // Second call — detected (threshold = 2)
      const r2 = check('s1', 'echo', { message: 'same' })
      expect(r2.detected).toBe(true)
      expect(r2.consecutiveCount).toBe(2)
      expect(r2.suggestion).toContain('echo')

      // Different call breaks the chain
      const r3 = check('s1', 'echo', { message: 'different' })
      expect(r3.detected).toBe(false)
      expect(r3.consecutiveCount).toBe(1)
    })
  })

  // ─── Commander Task Routing ──────────────────────────────────────────────

  describe('Commander integration', () => {
    it('routes coding task to coder worker', () => {
      const analysis = analyzeTask('Implement a new user authentication system')
      expect(analysis.taskType).toBe('write')

      const worker = selectWorker(analysis, BUILTIN_WORKERS)
      expect(worker).not.toBeNull()
      expect(worker!.name).toBe('coder')

      // Coder has write tools but not bash
      expect(worker!.tools).toContain('write_file')
      expect(worker!.tools).toContain('edit')
      expect(worker!.tools).toContain('read_file')
    })

    it('routes test task to tester worker with bash access', () => {
      const analysis = analyzeTask('Write comprehensive unit tests for the auth module')
      expect(analysis.taskType).toBe('test')

      const worker = selectWorker(analysis, BUILTIN_WORKERS)
      expect(worker!.name).toBe('tester')
      expect(worker!.tools).toContain('bash')
    })

    it('routes review task to reviewer with read-only tools', () => {
      const analysis = analyzeTask('Review the code changes for security issues')
      expect(analysis.taskType).toBe('review')

      const worker = selectWorker(analysis, BUILTIN_WORKERS)
      expect(worker!.name).toBe('reviewer')

      // Reviewer should NOT have write tools
      expect(worker!.tools).toContain('read_file')
      expect(worker!.tools).toContain('grep')
      expect(worker!.tools).not.toContain('write_file')
      expect(worker!.tools).not.toContain('edit')
    })

    it('team mode registers and modifies system prompt', () => {
      const extApi = makeExtApi()

      // Activate commander (registers team mode)
      const disposable = activateCommander(extApi)

      // Check that team mode is registered
      const modes = getAgentModes()
      expect(modes.has('team')).toBe(true)

      const teamMode = modes.get('team')!
      const prompt = teamMode.systemPrompt!('You are AVA.')
      expect(prompt).toContain('Team Lead')
      expect(prompt).toContain('Coder')
      expect(prompt).toContain('Tester')

      disposable.dispose()
      expect(getAgentModes().has('team')).toBe(false)
    })
  })

  // ─── Full Pipeline: Multiple Extensions Working Together ─────────────────

  describe('Full pipeline: modes + hooks + permissions', () => {
    it('permissions block before hooks run (priority ordering)', async () => {
      const callLog: string[] = []

      registerTool(
        defineTool({
          name: 'echo',
          description: 'Echo',
          schema: z.object({ message: z.string() }),
          async execute(input) {
            return { success: true, output: input.message }
          },
        })
      )

      // Permissions at priority 0 — blocks everything
      addToolMiddleware({
        name: 'permissions',
        priority: 0,
        async before() {
          callLog.push('permissions:before')
          return { blocked: true, reason: 'Permission denied' }
        },
      })

      // Hook at priority 10 — should NOT run since permissions blocked
      registerHook({
        type: 'PreToolUse',
        name: 'logger',
        handler: async () => {
          callLog.push('hook:PreToolUse')
          return {}
        },
      })
      addToolMiddleware(createHooksMiddleware())

      const result = await executeTool(
        'echo',
        { message: 'test' },
        {
          sessionId: 'test',
          workingDirectory: '/tmp',
        }
      )

      expect(result.success).toBe(false)
      // Only permissions should have run, hooks should not
      expect(callLog).toEqual(['permissions:before'])
    })

    it('agent with plan mode + hooks: read-only tools, hook logging', async () => {
      const hookLog: string[] = []

      // Register tools
      registerTool(
        defineTool({
          name: 'read_file',
          description: 'Read',
          schema: z.object({ path: z.string() }),
          async execute(input) {
            return { success: true, output: await platform.fs.readFile(input.path) }
          },
        })
      )
      registerTool(
        defineTool({
          name: 'grep',
          description: 'Grep',
          schema: z.object({ pattern: z.string() }),
          async execute() {
            return { success: true, output: 'match: line 1' }
          },
        })
      )

      // Register plan mode
      const extApi = makeExtApi()
      extApi.registerAgentMode(planAgentMode)

      // Register audit hook
      registerHook({
        type: 'PostToolUse',
        name: 'audit',
        handler: async (ctx) => {
          hookLog.push(`${(ctx as { toolName?: string }).toolName}`)
          return {}
        },
      })
      addToolMiddleware(createHooksMiddleware())

      // Set up file
      platform.fs.files.set('/tmp/project/index.ts', 'export const main = () => {}')

      const mockProvider = createMockProvider([
        {
          content: 'Let me read the file...',
          toolCalls: [makeToolCall('read_file', { path: '/tmp/project/index.ts' })],
        },
        {
          content: '',
          toolCalls: [makeToolCall('attempt_completion', { result: 'Research done' })],
        },
      ])
      registerProvider('mock', () => mockProvider)

      const agent = new AgentExecutor({
        provider: 'mock' as LLMProvider,
        maxTurns: 5,
        maxTimeMinutes: 1,
        toolMode: 'plan',
      })

      const result = await agent.run(
        { goal: 'Investigate the code', cwd: '/tmp/project' },
        AbortSignal.timeout(5000)
      )

      expect(result.success).toBe(true)
      expect(result.output).toBe('Research done')

      // Hook should have logged the read_file call
      expect(hookLog).toContain('read_file')
    })

    it('multiple extensions dispose cleanly', () => {
      const extApi = makeExtApi()

      // Register plan mode
      const planDisposable = extApi.registerAgentMode(planAgentMode)

      // Register team mode via commander
      const cmdDisposable = activateCommander(extApi)

      expect(getAgentModes().has('plan')).toBe(true)
      expect(getAgentModes().has('team')).toBe(true)

      // Dispose plan mode
      planDisposable.dispose()
      expect(getAgentModes().has('plan')).toBe(false)
      expect(getAgentModes().has('team')).toBe(true)

      // Dispose commander
      cmdDisposable.dispose()
      expect(getAgentModes().has('team')).toBe(false)
    })
  })
})
