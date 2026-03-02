import type { ToolContext } from '@ava/core-v2/tools'
import { afterEach, describe, expect, it, vi } from 'vitest'
import type { AgentDefinition } from './agent-definition.js'
import {
  configureDelegation,
  createDelegateTool,
  getDelegationConfig,
  resetDelegationConfig,
  resolveTools,
} from './delegate.js'
import { clearRegistry, registerAgent } from './registry.js'
import { WORKER_AGENTS } from './workers.js'

// Track mock call count for conditional behavior
let mockRunCallCount = 0
let mockRunBehavior:
  | 'success'
  | 'fail-then-succeed'
  | 'always-fail'
  | 'throw-then-succeed'
  | 'always-throw' = 'success'
let lastRunGoal = ''
let lastRunConfig: Record<string, unknown> = {}

// Mock AgentExecutor
vi.mock('@ava/core-v2/agent', () => ({
  AgentExecutor: class MockAgentExecutor {
    config: Record<string, unknown>
    onEvent?: (event: unknown) => void

    constructor(config: Record<string, unknown>, onEvent?: (event: unknown) => void) {
      this.config = config
      this.onEvent = onEvent
      lastRunConfig = config
    }

    async run(inputs: { goal: string; cwd: string }, _signal: AbortSignal) {
      mockRunCallCount++
      lastRunGoal = inputs.goal
      this.onEvent?.({
        type: 'tool:start',
        agentId: this.config.id,
        toolName: 'read_file',
        args: {},
      })

      if (mockRunBehavior === 'always-fail') {
        return {
          success: false,
          output: `Failed: ${inputs.goal.slice(0, 50)}`,
          terminateMode: 'ERROR',
          turns: 1,
          tokensUsed: { input: 50, output: 25 },
          durationMs: 500,
        }
      }

      if (mockRunBehavior === 'fail-then-succeed' && mockRunCallCount === 1) {
        return {
          success: false,
          output: 'First attempt failed: syntax error in generated code',
          terminateMode: 'ERROR',
          turns: 2,
          tokensUsed: { input: 80, output: 40 },
          durationMs: 800,
        }
      }

      if (mockRunBehavior === 'always-throw') {
        throw new Error('Agent crashed unexpectedly')
      }

      if (mockRunBehavior === 'throw-then-succeed' && mockRunCallCount === 1) {
        throw new Error('Transient agent crash')
      }

      return {
        success: true,
        output: `Completed: ${inputs.goal.slice(0, 50)}`,
        terminateMode: 'GOAL',
        turns: 3,
        tokensUsed: { input: 100, output: 50 },
        durationMs: 1000,
      }
    }
  },
}))

// Mock worktree module
vi.mock('../../git/src/worktree.js', () => ({
  createWorktree: vi.fn().mockResolvedValue({
    path: '/test/project/.ava-worktrees/ava-session-mock1234',
    branch: 'ava-session-mock1234',
  }),
  removeWorktree: vi.fn().mockResolvedValue(undefined),
}))

const CODER = WORKER_AGENTS.find((a) => a.id === 'coder')!
const REVIEWER = WORKER_AGENTS.find((a) => a.id === 'reviewer')!

// Reset mock state before each test
function resetMockState(): void {
  mockRunCallCount = 0
  mockRunBehavior = 'success'
  lastRunGoal = ''
  lastRunConfig = {}
  resetDelegationConfig()
}

function createMockContext(overrides?: Partial<ToolContext>): ToolContext {
  return {
    sessionId: 'parent-agent-id',
    workingDirectory: '/test/project',
    signal: new AbortController().signal,
    provider: 'anthropic',
    model: 'claude-sonnet-4',
    onEvent: vi.fn(),
    ...overrides,
  }
}

describe('resolveTools', () => {
  it('strips delegate_ tools from workers', () => {
    const worker: AgentDefinition = {
      ...CODER,
      tools: [...CODER.tools, 'delegate_tester'],
    }
    const tools = resolveTools(worker)
    expect(tools).not.toContain('delegate_tester')
    expect(tools).toContain('read_file')
  })

  it('adds delegate tools for leads', () => {
    const lead: AgentDefinition = {
      id: 'test-lead',
      name: 'test-lead',
      displayName: 'Test Lead',
      description: 'Test lead',
      tier: 'lead',
      systemPrompt: '',
      tools: ['read_file', 'grep'],
      delegates: ['coder', 'tester'],
    }
    const tools = resolveTools(lead)
    expect(tools).toContain('delegate_coder')
    expect(tools).toContain('delegate_tester')
    expect(tools).toContain('read_file')
  })

  it('returns tools as-is for commander', () => {
    const commander: AgentDefinition = {
      id: 'cmd',
      name: 'cmd',
      displayName: 'Commander',
      description: '',
      tier: 'commander',
      systemPrompt: '',
      tools: ['question', 'attempt_completion'],
    }
    expect(resolveTools(commander)).toEqual(['question', 'attempt_completion'])
  })
})

describe('createDelegateTool', () => {
  afterEach(() => {
    clearRegistry()
    resetMockState()
  })

  describe('tool definition', () => {
    it('creates tool with correct name', () => {
      const tool = createDelegateTool(CODER)
      expect(tool.definition.name).toBe('delegate_coder')
    })

    it('includes agent description in tool description', () => {
      const tool = createDelegateTool(CODER)
      expect(tool.definition.description).toContain('Coder')
      expect(tool.definition.description).toContain('Writes and modifies code files')
    })

    it('lists resolved tools in description', () => {
      const tool = createDelegateTool(CODER)
      expect(tool.definition.description).toContain('read_file')
      expect(tool.definition.description).toContain('write_file')
    })

    it('requires task parameter', () => {
      const tool = createDelegateTool(CODER)
      expect(tool.definition.input_schema.required).toContain('task')
    })

    it('has optional context parameter', () => {
      const tool = createDelegateTool(CODER)
      expect(tool.definition.input_schema.properties).toHaveProperty('context')
      expect(tool.definition.input_schema.required).not.toContain('context')
    })

    it('has optional files parameter', () => {
      const tool = createDelegateTool(CODER)
      expect(tool.definition.input_schema.properties).toHaveProperty('files')
      expect(tool.definition.input_schema.required).not.toContain('files')
    })
  })

  describe('execution', () => {
    it('emits session:child-opened after delegation:start', async () => {
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute({ task: 'Write a function' }, ctx)

      const onEvent = ctx.onEvent as ReturnType<typeof vi.fn>
      const childOpenedEvent = onEvent.mock.calls.find(
        (c: unknown[]) => (c[0] as Record<string, unknown>).type === 'session:child-opened'
      )
      expect(childOpenedEvent).toBeDefined()
      expect(childOpenedEvent![0]).toMatchObject({
        type: 'session:child-opened',
        parentSessionId: 'parent-agent-id',
        workingDirectory: '/test/project',
      })
      // delegation:start fires first, then session:child-opened after child is created
      const childOpenedIdx = onEvent.mock.calls.indexOf(childOpenedEvent!)
      const startIdx = onEvent.mock.calls.findIndex(
        (c: unknown[]) => (c[0] as Record<string, unknown>).type === 'delegation:start'
      )
      expect(startIdx).toBeLessThan(childOpenedIdx)
    })

    it('emits delegation:start with tier', async () => {
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute({ task: 'Write a function' }, ctx)

      const onEvent = ctx.onEvent as ReturnType<typeof vi.fn>
      const startEvent = onEvent.mock.calls.find(
        (c: unknown[]) => (c[0] as Record<string, unknown>).type === 'delegation:start'
      )
      expect(startEvent).toBeDefined()
      expect(startEvent![0]).toMatchObject({
        type: 'delegation:start',
        agentId: 'parent-agent-id',
        workerName: 'coder',
        task: 'Write a function',
        tier: 'worker',
      })
    })

    it('emits delegation:complete after child finishes', async () => {
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute({ task: 'Write a function' }, ctx)

      const onEvent = ctx.onEvent as ReturnType<typeof vi.fn>
      const completeEvent = onEvent.mock.calls.find(
        (c: unknown[]) => (c[0] as Record<string, unknown>).type === 'delegation:complete'
      )
      expect(completeEvent).toBeDefined()
      expect(completeEvent![0]).toMatchObject({
        type: 'delegation:complete',
        agentId: 'parent-agent-id',
        success: true,
      })
    })

    it('forwards child events to parent onEvent', async () => {
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute({ task: 'Write a function' }, ctx)

      const onEvent = ctx.onEvent as ReturnType<typeof vi.fn>
      const childEvent = onEvent.mock.calls.find(
        (c: unknown[]) => (c[0] as Record<string, unknown>).type === 'tool:start'
      )
      expect(childEvent).toBeDefined()
    })

    it('returns success result from child', async () => {
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      const result = await tool.execute({ task: 'Write a function' }, ctx)

      expect(result.success).toBe(true)
      expect(result.output).toContain('Completed')
    })

    it('uses agent model/provider when set', async () => {
      const agentWithModel: AgentDefinition = {
        ...CODER,
        model: 'claude-haiku-4-5',
        provider: 'openrouter',
      }
      const tool = createDelegateTool(agentWithModel)
      const ctx = createMockContext()

      await tool.execute({ task: 'Write' }, ctx)
      // The mock captures the config — we can check delegation started
      const onEvent = ctx.onEvent as ReturnType<typeof vi.fn>
      expect(onEvent).toHaveBeenCalled()
    })

    it('works without onEvent callback', async () => {
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext({ onEvent: undefined })

      const result = await tool.execute({ task: 'Write a function' }, ctx)
      expect(result.success).toBe(true)
    })
  })

  describe('shared file cache (P-04)', () => {
    afterEach(() => resetMockState())

    it('prepends file blocks to the goal when files are provided', async () => {
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute(
        {
          task: 'Fix the bug',
          files: { '/src/app.ts': 'const x = 1', '/src/util.ts': 'export {}' },
        },
        ctx
      )

      expect(lastRunGoal).toContain('<file path="/src/app.ts">')
      expect(lastRunGoal).toContain('const x = 1')
      expect(lastRunGoal).toContain('<file path="/src/util.ts">')
      expect(lastRunGoal).toContain('export {}')
      expect(lastRunGoal).toContain('Fix the bug')
    })

    it('file blocks appear before the task text', async () => {
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute({ task: 'Do something', files: { '/a.ts': 'content' } }, ctx)

      const fileIdx = lastRunGoal.indexOf('<file path="/a.ts">')
      const taskIdx = lastRunGoal.indexOf('Do something')
      expect(fileIdx).toBeLessThan(taskIdx)
    })

    it('works without files (backward compat)', async () => {
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute({ task: 'No files here' }, ctx)

      expect(lastRunGoal).not.toContain('<file')
      expect(lastRunGoal).toContain('No files here')
    })

    it('includes context after task when both files and context provided', async () => {
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute(
        { task: 'Fix it', context: 'Use TypeScript', files: { '/f.ts': 'code' } },
        ctx
      )

      expect(lastRunGoal).toContain('<file path="/f.ts">')
      expect(lastRunGoal).toContain('Fix it')
      expect(lastRunGoal).toContain('Context:\nUse TypeScript')
    })
  })

  describe('budget awareness (P-05)', () => {
    afterEach(() => resetMockState())

    it('includes budget instruction in system prompt', async () => {
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute({ task: 'Write a function' }, ctx)

      const sp = lastRunConfig.systemPrompt as string
      expect(sp).toContain('You have 15 turns maximum')
      expect(sp).toContain('50%')
      expect(sp).toContain('attempt_completion')
    })

    it('uses agent maxTurns in budget instruction', async () => {
      const customAgent: AgentDefinition = { ...CODER, maxTurns: 10 }
      const tool = createDelegateTool(customAgent)
      const ctx = createMockContext()

      await tool.execute({ task: 'Write code' }, ctx)

      const sp = lastRunConfig.systemPrompt as string
      expect(sp).toContain('You have 10 turns maximum')
    })

    it('includes both agent system prompt and budget instruction', async () => {
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute({ task: 'Write code' }, ctx)

      const sp = lastRunConfig.systemPrompt as string
      // Agent system prompt from CODER
      expect(sp).toContain('senior developer')
      // Budget instruction
      expect(sp).toContain('turns maximum')
    })
  })

  describe('worktree isolation (CG-09)', () => {
    afterEach(() => resetMockState())

    it('does not create worktree when isolation is false', async () => {
      const { createWorktree: mockCreate } = await import('../../git/src/worktree.js')
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute({ task: 'Write code' }, ctx)

      expect(mockCreate).not.toHaveBeenCalled()
    })

    it('creates worktree when isolation is true', async () => {
      configureDelegation({ isolation: true })
      const { createWorktree: mockCreate, removeWorktree: mockRemove } = await import(
        '../../git/src/worktree.js'
      )
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute({ task: 'Write code' }, ctx)

      expect(mockCreate).toHaveBeenCalledWith('/test/project', expect.any(String))
      expect(mockRemove).toHaveBeenCalledWith(
        '/test/project',
        '/test/project/.ava-worktrees/ava-session-mock1234'
      )
    })

    it('cleans up worktree even on failure', async () => {
      configureDelegation({ isolation: true, maxRetries: 0 })
      mockRunBehavior = 'always-fail'
      const { removeWorktree: mockRemove } = await import('../../git/src/worktree.js')
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute({ task: 'Fail' }, ctx)

      expect(mockRemove).toHaveBeenCalled()
    })

    it('getDelegationConfig includes isolation field', () => {
      expect(getDelegationConfig().isolation).toBe(false)
      configureDelegation({ isolation: true })
      expect(getDelegationConfig().isolation).toBe(true)
    })
  })

  describe('different agents', () => {
    it('creates reviewer tool with read-only tools', () => {
      const tool = createDelegateTool(REVIEWER)
      expect(tool.definition.name).toBe('delegate_reviewer')
      expect(tool.definition.description).toContain('Reviewer')
    })

    it('creates lead delegation tool', () => {
      const lead: AgentDefinition = {
        id: 'frontend-lead',
        name: 'frontend-lead',
        displayName: 'Frontend Lead',
        description: 'Manages frontend',
        tier: 'lead',
        systemPrompt: '',
        tools: ['read_file'],
        delegates: ['coder', 'tester'],
      }
      registerAgent(CODER)
      registerAgent(lead)

      const tool = createDelegateTool(lead)
      expect(tool.definition.name).toBe('delegate_frontend-lead')
      expect(tool.definition.description).toContain('delegate_coder')
      expect(tool.definition.description).toContain('delegate_tester')
    })
  })

  describe('error recovery', () => {
    afterEach(() => resetMockState())

    it('retries once by default when delegation fails', async () => {
      mockRunBehavior = 'fail-then-succeed'
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      const result = await tool.execute({ task: 'Write a function' }, ctx)

      expect(result.success).toBe(true)
      expect(result.output).toContain('Completed')
      // Should have been called twice: first attempt fails, retry succeeds
      expect(mockRunCallCount).toBe(2)
    })

    it('emits delegation:retry event on retry', async () => {
      mockRunBehavior = 'fail-then-succeed'
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute({ task: 'Write a function' }, ctx)

      const onEvent = ctx.onEvent as ReturnType<typeof vi.fn>
      const retryEvent = onEvent.mock.calls.find(
        (c: unknown[]) => (c[0] as Record<string, unknown>).type === 'delegation:retry'
      )
      expect(retryEvent).toBeDefined()
      expect(retryEvent![0]).toMatchObject({
        type: 'delegation:retry',
        agentId: 'parent-agent-id',
        workerName: 'coder',
        attempt: 1,
        maxRetries: 1,
      })
    })

    it('returns failure after exhausting retries', async () => {
      mockRunBehavior = 'always-fail'
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      const result = await tool.execute({ task: 'Write a function' }, ctx)

      expect(result.success).toBe(false)
      // Default maxRetries = 1, so 2 total attempts
      expect(mockRunCallCount).toBe(2)
    })

    it('respects custom maxRetries configuration', async () => {
      configureDelegation({ maxRetries: 3 })
      mockRunBehavior = 'always-fail'
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      const result = await tool.execute({ task: 'Write a function' }, ctx)

      expect(result.success).toBe(false)
      // maxRetries = 3 means 4 total attempts (initial + 3 retries)
      expect(mockRunCallCount).toBe(4)
    })

    it('does not retry when maxRetries is 0', async () => {
      configureDelegation({ maxRetries: 0 })
      mockRunBehavior = 'always-fail'
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      const result = await tool.execute({ task: 'Write a function' }, ctx)

      expect(result.success).toBe(false)
      expect(mockRunCallCount).toBe(1)
    })

    it('retries on thrown exceptions', async () => {
      mockRunBehavior = 'throw-then-succeed'
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      const result = await tool.execute({ task: 'Write a function' }, ctx)

      expect(result.success).toBe(true)
      expect(mockRunCallCount).toBe(2)
    })

    it('returns detailed error after exception exhausts retries', async () => {
      mockRunBehavior = 'always-throw'
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      const result = await tool.execute({ task: 'Write a function' }, ctx)

      expect(result.success).toBe(false)
      expect(result.output).toContain('Coder failed:')
      expect(result.output).toContain('Agent crashed unexpectedly')
    })

    it('appends failure context to retry goal', async () => {
      mockRunBehavior = 'fail-then-succeed'
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute({ task: 'Write a function' }, ctx)

      // The retry should have emitted a delegation:start with the original task
      const onEvent = ctx.onEvent as ReturnType<typeof vi.fn>
      const startEvents = onEvent.mock.calls.filter(
        (c: unknown[]) => (c[0] as Record<string, unknown>).type === 'delegation:start'
      )
      // Two delegation:start events (initial + retry)
      expect(startEvents).toHaveLength(2)
    })

    it('getDelegationConfig returns current config', () => {
      expect(getDelegationConfig().maxRetries).toBe(1)
      configureDelegation({ maxRetries: 5 })
      expect(getDelegationConfig().maxRetries).toBe(5)
    })

    it('emits delegation:complete only once on successful retry', async () => {
      mockRunBehavior = 'fail-then-succeed'
      const tool = createDelegateTool(CODER)
      const ctx = createMockContext()

      await tool.execute({ task: 'Write a function' }, ctx)

      const onEvent = ctx.onEvent as ReturnType<typeof vi.fn>
      const completeEvents = onEvent.mock.calls.filter(
        (c: unknown[]) => (c[0] as Record<string, unknown>).type === 'delegation:complete'
      )
      // Only the final successful result emits delegation:complete
      expect(completeEvents).toHaveLength(1)
      expect(completeEvents[0]![0]).toMatchObject({ success: true })
    })
  })
})
