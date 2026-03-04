/**
 * Commander delegate tools smoke test — verifies all 5 delegate tools.
 */

import { afterEach, describe, expect, it, vi } from 'vitest'
import type { AgentDefinition } from './agent-definition.js'
import { createDelegateTool, resolveTools } from './delegate.js'
import { WORKER_AGENTS } from './workers.js'

// Mock AgentExecutor + executor registry
vi.mock('@ava/core-v2/agent', () => ({
  AgentExecutor: class {
    constructor(
      public config: Record<string, unknown>,
      public onEvent?: (event: unknown) => void
    ) {}
    async run() {
      return {
        success: true,
        output: 'Done',
        terminateMode: 'GOAL',
        turns: 1,
        tokensUsed: { input: 10, output: 5 },
        durationMs: 100,
      }
    }
  },
  registerExecutor: vi.fn(),
  unregisterExecutor: vi.fn(),
}))

const DELEGATE_AGENTS = WORKER_AGENTS.filter((a) =>
  ['coder', 'tester', 'reviewer', 'researcher', 'debugger'].includes(a.id)
)

describe('Commander delegate tools smoke test', () => {
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('has 5 delegate-able worker agents', () => {
    expect(DELEGATE_AGENTS).toHaveLength(5)
  })

  describe('tool definitions', () => {
    it.each(
      DELEGATE_AGENTS.map((a) => [a.name, a])
    )('delegate_%s has valid definition', (_name, agent) => {
      const tool = createDelegateTool(agent)
      expect(tool.definition.name).toBe(`delegate_${agent.name}`)
      expect(tool.definition.description).toBeTruthy()
      expect(tool.definition.input_schema.properties).toHaveProperty('task')
      expect(tool.definition.input_schema.required).toContain('task')
    })
  })

  describe('execution', () => {
    it.each(
      DELEGATE_AGENTS.map((a) => [a.name, a])
    )('delegate_%s executes successfully', async (_name, agent) => {
      const tool = createDelegateTool(agent)
      const result = await tool.execute(
        { task: 'Smoke test task' },
        {
          sessionId: 'smoke-test',
          workingDirectory: '/project',
          signal: new AbortController().signal,
          onEvent: vi.fn(),
        }
      )
      expect(result.success).toBe(true)
    })
  })

  describe('resolveTools', () => {
    it('workers without delegates get no delegate_ tools', () => {
      for (const agent of DELEGATE_AGENTS) {
        const tools = resolveTools(agent)
        // Workers without a delegates array get no delegate_ tools
        if (!agent.delegates?.length) {
          expect(tools.every((t) => !t.startsWith('delegate_'))).toBe(true)
        }
      }
    })

    it('leads get delegate tools for their workers', () => {
      const lead: AgentDefinition = {
        id: 'test-lead',
        name: 'test-lead',
        displayName: 'Test Lead',
        description: 'Test',
        tier: 'lead',
        systemPrompt: '',
        tools: ['read_file'],
        delegates: ['coder'],
      }
      const tools = resolveTools(lead)
      expect(tools).toContain('delegate_coder')
    })
  })
})
