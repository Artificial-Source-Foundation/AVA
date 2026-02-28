import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { clearRegistry, getAgent } from './registry.js'
import { presetToDefinition, syncSettingsToRegistry } from './settings-sync.js'

describe('presetToDefinition', () => {
  it('converts a settings preset to an AgentDefinition', () => {
    const def = presetToDefinition({
      id: 'custom-worker',
      name: 'Custom Worker',
      description: 'A custom worker',
      tier: 'worker',
      systemPrompt: 'You are custom',
      tools: ['read_file', 'grep'],
      isCustom: true,
    })

    expect(def.id).toBe('custom-worker')
    expect(def.tier).toBe('worker')
    expect(def.tools).toEqual(['read_file', 'grep'])
    expect(def.isBuiltIn).toBe(false)
  })

  it('defaults tier to worker', () => {
    const def = presetToDefinition({
      id: 'no-tier',
      name: 'No Tier',
      description: 'Missing tier',
    })

    expect(def.tier).toBe('worker')
  })
})

describe('syncSettingsToRegistry', () => {
  afterEach(() => clearRegistry())

  it('registers custom enabled agents from settings', () => {
    const { api } = createMockExtensionAPI('commander')

    api.getSettings = vi.fn().mockReturnValue({
      agents: [
        {
          id: 'my-agent',
          name: 'My Agent',
          description: 'Custom',
          isCustom: true,
          enabled: true,
          tier: 'worker',
          tools: ['bash'],
        },
        { id: 'disabled', name: 'Disabled', description: 'Off', isCustom: true, enabled: false },
      ],
    })

    syncSettingsToRegistry(api)

    expect(getAgent('my-agent')).toBeDefined()
    expect(getAgent('my-agent')?.tools).toEqual(['bash'])
    expect(getAgent('disabled')).toBeUndefined()
  })

  it('skips built-in agents', () => {
    const { api } = createMockExtensionAPI('commander')

    api.getSettings = vi.fn().mockReturnValue({
      agents: [
        { id: 'coder', name: 'Coder', description: 'Built-in', isCustom: false, enabled: true },
      ],
    })

    syncSettingsToRegistry(api)

    // Built-in agents should not be registered (they're registered by workers.ts)
    expect(getAgent('coder')).toBeUndefined()
  })

  it('handles missing settings gracefully', () => {
    const { api } = createMockExtensionAPI('commander')

    api.getSettings = vi.fn().mockImplementation(() => {
      throw new Error('no such category')
    })

    // Should not throw
    syncSettingsToRegistry(api)
  })
})
