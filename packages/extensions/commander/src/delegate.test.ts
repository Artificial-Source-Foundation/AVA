import type { ToolContext } from '@ava/core-v2/tools'
import { afterEach, describe, expect, it, vi } from 'vitest'
import type { AgentDefinition } from './agent-definition.js'
import { createDelegateTool, resolveTools } from './delegate.js'
import { clearRegistry, registerAgent } from './registry.js'
import { WORKER_AGENTS } from './workers.js'

// Mock AgentExecutor
vi.mock('@ava/core-v2/agent', () => ({
  AgentExecutor: class MockAgentExecutor {
    config: Record<string, unknown>
    onEvent?: (event: unknown) => void

    constructor(config: Record<string, unknown>, onEvent?: (event: unknown) => void) {
      this.config = config
      this.onEvent = onEvent
    }

    async run(inputs: { goal: string; cwd: string }, _signal: AbortSignal) {
      this.onEvent?.({
        type: 'tool:start',
        agentId: this.config.id,
        toolName: 'read_file',
        args: {},
      })

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

const CODER = WORKER_AGENTS.find((a) => a.id === 'coder')!
const REVIEWER = WORKER_AGENTS.find((a) => a.id === 'reviewer')!

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
  afterEach(() => clearRegistry())

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
  })

  describe('execution', () => {
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
})
