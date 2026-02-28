import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { activate } from './index.js'
import { clearRegistry } from './registry.js'
import { BUILTIN_AGENTS, LEAD_AGENTS, WORKER_AGENTS } from './workers.js'

// Mock AgentExecutor to avoid real agent execution
vi.mock('@ava/core-v2/agent', () => ({
  AgentExecutor: class {
    async run() {
      return { success: true, output: 'done', terminateMode: 'GOAL' }
    }
  },
}))

describe('Commander Extension (Praxis)', () => {
  afterEach(() => clearRegistry())

  it('registers delegate tools for all leads and workers', () => {
    const { api, registeredTools } = createMockExtensionAPI('commander')

    activate(api)

    const expectedCount = LEAD_AGENTS.length + WORKER_AGENTS.length
    expect(registeredTools).toHaveLength(expectedCount)

    const toolNames = registeredTools.map((t) => t.definition.name)
    // Leads
    expect(toolNames).toContain('delegate_frontend-lead')
    expect(toolNames).toContain('delegate_backend-lead')
    expect(toolNames).toContain('delegate_qa-lead')
    expect(toolNames).toContain('delegate_fullstack-lead')
    // Workers
    expect(toolNames).toContain('delegate_coder')
    expect(toolNames).toContain('delegate_tester')
    expect(toolNames).toContain('delegate_reviewer')
    expect(toolNames).toContain('delegate_researcher')
    expect(toolNames).toContain('delegate_debugger')
    expect(toolNames).toContain('delegate_architect')
    expect(toolNames).toContain('delegate_planner')
    expect(toolNames).toContain('delegate_devops')
  })

  it('registers praxis agent mode', () => {
    const { api, registeredModes } = createMockExtensionAPI('commander')

    activate(api)

    expect(registeredModes).toHaveLength(1)
    expect(registeredModes[0].name).toBe('praxis')
    expect(registeredModes[0].description).toContain('3-tier')
  })

  it('praxis mode filterTools strips coding tools, keeps delegate tools', () => {
    const { api, registeredModes } = createMockExtensionAPI('commander')

    activate(api)

    const praxisMode = registeredModes[0]
    const tools = [
      {
        name: 'read_file',
        description: '',
        input_schema: { type: 'object' as const, properties: {} },
      },
      {
        name: 'write_file',
        description: '',
        input_schema: { type: 'object' as const, properties: {} },
      },
      {
        name: 'delegate_frontend-lead',
        description: '',
        input_schema: { type: 'object' as const, properties: {} },
      },
      {
        name: 'delegate_backend-lead',
        description: '',
        input_schema: { type: 'object' as const, properties: {} },
      },
      {
        name: 'question',
        description: '',
        input_schema: { type: 'object' as const, properties: {} },
      },
      {
        name: 'attempt_completion',
        description: '',
        input_schema: { type: 'object' as const, properties: {} },
      },
      { name: 'bash', description: '', input_schema: { type: 'object' as const, properties: {} } },
    ]

    const filtered = praxisMode.filterTools!(tools)

    const names = filtered.map((t) => t.name)
    expect(names).toContain('delegate_frontend-lead')
    expect(names).toContain('delegate_backend-lead')
    expect(names).toContain('question')
    expect(names).toContain('attempt_completion')
    expect(names).not.toContain('read_file')
    expect(names).not.toContain('write_file')
    expect(names).not.toContain('bash')
  })

  it('praxis mode systemPrompt includes lead descriptions', () => {
    const { api, registeredModes } = createMockExtensionAPI('commander')

    activate(api)

    const praxisMode = registeredModes[0]
    const prompt = praxisMode.systemPrompt!('Base prompt')

    expect(prompt).toContain('Base prompt')
    expect(prompt).toContain('Praxis')
    expect(prompt).toContain('Commander')
    expect(prompt).toContain('Frontend Lead')
    expect(prompt).toContain('Backend Lead')
    expect(prompt).toContain('delegate_frontend-lead')
    expect(prompt).toContain('Planning Protocol')
  })

  it('does not register when disabled via settings', () => {
    const mock = createMockExtensionAPI('commander')
    mock.api.getSettings = <T>(_ns: string): T => ({ enabled: false }) as T

    const disposable = activate(mock.api)

    expect(mock.registeredTools).toHaveLength(0)
    expect(mock.registeredModes).toHaveLength(0)
    expect(disposable).toHaveProperty('dispose')
  })

  it('dispose cleans up all registrations', () => {
    const { api, registeredTools, registeredModes } = createMockExtensionAPI('commander')

    const disposable = activate(api)

    expect(registeredTools.length).toBeGreaterThan(0)
    expect(registeredModes.length).toBeGreaterThan(0)

    disposable.dispose()

    expect(registeredTools).toHaveLength(0)
    expect(registeredModes).toHaveLength(0)
  })

  it('logs registration count', () => {
    const { api } = createMockExtensionAPI('commander')

    activate(api)

    expect(api.log.debug).toHaveBeenCalledWith(
      expect.stringContaining(`${BUILTIN_AGENTS.length} agents total`)
    )
  })
})
