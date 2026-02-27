import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('skills extension', () => {
  it('activates and listens for skills:register', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('skills:register')).toBe(true)
  })

  it('listens for agent:turn-start', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('agent:turn-start')).toBe(true)
  })

  it('registers skills and matches on turn-start', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)

    // Register a skill
    api.emit('skills:register', {
      name: 'react',
      description: 'React patterns',
      globs: ['**/*.tsx'],
      content: 'Use functional components',
      source: 'built-in',
    })

    // Trigger turn with matching files
    api.emit('agent:turn-start', { sessionId: 'test', files: ['src/App.tsx'] })

    const matched = emittedEvents.find((e) => e.event === 'skills:matched')
    expect(matched).toBeDefined()
  })

  it('does not emit when no skills match', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)

    api.emit('skills:register', {
      name: 'python',
      description: 'Python patterns',
      globs: ['**/*.py'],
      content: 'Use type hints',
      source: 'built-in',
    })

    api.emit('agent:turn-start', { sessionId: 'test', files: ['src/App.tsx'] })

    const matched = emittedEvents.find((e) => e.event === 'skills:matched')
    expect(matched).toBeUndefined()
  })

  it('cleans up on dispose', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(eventHandlers.has('skills:register')).toBe(false)
    expect(eventHandlers.has('agent:turn-start')).toBe(false)
  })
})
