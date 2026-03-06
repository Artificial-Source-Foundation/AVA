import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { activate } from './index.js'
import { clearRegistry } from './registry.js'
import { BUILTIN_AGENTS } from './workers.js'

// Mock AgentExecutor to avoid real agent execution
vi.mock('@ava/core-v2/agent', () => ({
  AgentExecutor: class {
    async run() {
      return { success: true, output: 'done', terminateMode: 'GOAL' }
    }
  },
  registerExecutor: vi.fn(),
  unregisterExecutor: vi.fn(),
}))

describe('Commander Extension (Praxis)', () => {
  afterEach(() => clearRegistry())

  it('registers delegate tools for all leads and workers', () => {
    const { api, registeredTools } = createMockExtensionAPI('commander')

    activate(api)

    // Only agents in DELEGATE_TOOL_AGENT_IDS get delegate tools (coder, researcher, reviewer, explorer)
    expect(registeredTools).toHaveLength(4)

    const toolNames = registeredTools.map((t) => t.definition.name)
    expect(toolNames).toContain('delegate_coder')
    expect(toolNames).toContain('delegate_reviewer')
    expect(toolNames).toContain('delegate_researcher')
    expect(toolNames).toContain('delegate_explorer')
  })

  it('registers praxis agent mode', () => {
    const { api, registeredModes } = createMockExtensionAPI('commander')

    activate(api)

    expect(registeredModes).toHaveLength(1)
    expect(registeredModes[0].name).toBe('praxis')
    expect(registeredModes[0].description).toContain('3-tier')
  })

  it('praxis mode filterTools keeps all tools (tiered delegation)', () => {
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
    // Tiered delegation: commander keeps ALL tools
    expect(names).toContain('delegate_frontend-lead')
    expect(names).toContain('delegate_backend-lead')
    expect(names).toContain('question')
    expect(names).toContain('attempt_completion')
    expect(names).toContain('read_file')
    expect(names).toContain('write_file')
    expect(names).toContain('bash')
  })

  it('praxis mode systemPrompt includes lead descriptions', () => {
    const { api, registeredModes } = createMockExtensionAPI('commander')

    activate(api)

    const praxisMode = registeredModes[0]
    const prompt = praxisMode.systemPrompt!('Base prompt')

    expect(prompt).toContain('Base prompt')
    expect(prompt).toContain('Praxis')
    expect(prompt).toContain('Commander')
    expect(prompt).toContain('Coder')
    expect(prompt).toContain('Reviewer')
    expect(prompt).toContain('delegate_coder')
    expect(prompt).toContain('Task Complexity Assessment')
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
