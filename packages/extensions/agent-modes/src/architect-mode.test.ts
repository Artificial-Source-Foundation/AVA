import type { ToolDefinition } from '@ava/core-v2/llm'
import { describe, expect, it, vi } from 'vitest'
import {
  buildExecutorSystemPrompt,
  createArchitectMode,
  getArchitectExecutionTools,
  registerArchitectMode,
} from './architect-mode.js'

const TOOLSET: ToolDefinition[] = [
  { name: 'read_file', description: '', input_schema: { type: 'object', properties: {} } },
  { name: 'glob', description: '', input_schema: { type: 'object', properties: {} } },
  { name: 'grep', description: '', input_schema: { type: 'object', properties: {} } },
  { name: 'ls', description: '', input_schema: { type: 'object', properties: {} } },
  { name: 'websearch', description: '', input_schema: { type: 'object', properties: {} } },
  { name: 'edit', description: '', input_schema: { type: 'object', properties: {} } },
  { name: 'write_file', description: '', input_schema: { type: 'object', properties: {} } },
  { name: 'bash', description: '', input_schema: { type: 'object', properties: {} } },
]

describe('architect mode', () => {
  it('planning phase only has read-only tools', () => {
    const mode = createArchitectMode({
      plannerProvider: 'anthropic',
      plannerModel: 'claude-opus-4-6',
      executorProvider: 'anthropic',
      executorModel: 'claude-sonnet-4-6',
      maxPlanSteps: 10,
    })

    const filtered = mode.filterTools?.(TOOLSET) ?? []
    expect(filtered.map((tool) => tool.name)).toEqual([
      'read_file',
      'glob',
      'grep',
      'ls',
      'websearch',
    ])
  })

  it('execution phase has all tools', () => {
    const executionTools = getArchitectExecutionTools(TOOLSET)
    expect(executionTools).toHaveLength(TOOLSET.length)
  })

  it('plan is passed to executor in system prompt', () => {
    const base = 'You are AVA.'
    const plan = '1. Update src/a.ts\n2. Add tests in src/a.test.ts'
    const prompt = buildExecutorSystemPrompt(base, plan)

    expect(prompt).toContain(base)
    expect(prompt).toContain('ARCHITECT MODE (Execution Phase)')
    expect(prompt).toContain(plan)
    expect(prompt).toContain('Follow this plan exactly')
  })

  it('config supports different provider/model per phase', () => {
    const mode = createArchitectMode({
      plannerProvider: 'openrouter',
      plannerModel: 'anthropic/claude-opus-4-6',
      executorProvider: 'openrouter',
      executorModel: 'anthropic/claude-sonnet-4-6',
      maxPlanSteps: 7,
    })

    const prompt = mode.systemPrompt?.('Base prompt') ?? ''
    expect(prompt).toContain('openrouter/anthropic/claude-opus-4-6')
    expect(prompt).toContain('openrouter/anthropic/claude-sonnet-4-6')
    expect(prompt).toContain('at most 7 implementation steps')
  })

  it('registerArchitectMode registers mode with API', () => {
    const api = {
      registerAgentMode: vi.fn().mockReturnValue({ dispose: vi.fn() }),
    }

    const disposable = registerArchitectMode(api as never)
    expect(api.registerAgentMode).toHaveBeenCalledTimes(1)
    expect(disposable).toBeDefined()
  })
})
