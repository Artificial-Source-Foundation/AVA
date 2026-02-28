import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('react-patterns plugin', () => {
  it('emits skills:register event with correct skill data', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)

    const registerEvents = emittedEvents.filter((e) => e.event === 'skills:register')
    expect(registerEvents).toHaveLength(1)

    const skill = registerEvents[0].data as {
      name: string
      globs: string[]
      content: string
      source: string
    }
    expect(skill.name).toBe('React Patterns')
    expect(skill.globs).toEqual(['**/*.tsx', '**/*.jsx'])
    expect(skill.content).toContain('Component Composition')
    expect(skill.content).toContain('Hooks Best Practices')
    expect(skill.source).toBe('plugin:react-patterns')
  })

  it('logs activation message', () => {
    const { api } = createMockExtensionAPI()
    activate(api)
    expect(api.log.info).toHaveBeenCalledWith('React Patterns skill registered')
  })

  it('returns a disposable', () => {
    const { api } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(disposable).toBeDefined()
    expect(typeof disposable.dispose).toBe('function')
    // Should not throw
    disposable.dispose()
  })

  it('skill content covers key React topics', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)

    const skill = emittedEvents[0].data as { content: string }
    expect(skill.content).toContain('useMemo')
    expect(skill.content).toContain('useCallback')
    expect(skill.content).toContain('React.memo')
    expect(skill.content).toContain('TypeScript')
  })
})
