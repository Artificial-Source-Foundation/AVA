import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('rules extension', () => {
  it('activates and listens for rules:register', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('rules:register')).toBe(true)
  })

  it('listens for session:opened', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('session:opened')).toBe(true)
  })

  it('listens for agent:turn-start', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('agent:turn-start')).toBe(true)
  })

  it('registers rules and matches auto rules on turn-start', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)

    // Register an auto rule
    api.emit('rules:register', {
      name: 'testing',
      description: 'Test conventions',
      globs: ['**/*.test.ts'],
      activation: 'auto',
      content: 'Use describe/it blocks',
      source: '.ava/rules/testing.md',
    })

    // Trigger turn with matching files
    api.emit('agent:turn-start', { sessionId: 'test', files: ['src/app.test.ts'] })

    const matched = emittedEvents.find((e) => e.event === 'rules:matched')
    expect(matched).toBeDefined()
  })

  it('does not emit when no auto rules match', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)

    api.emit('rules:register', {
      name: 'py-rule',
      description: 'Python',
      globs: ['**/*.py'],
      activation: 'auto',
      content: 'Python rules',
      source: '.ava/rules/python.md',
    })

    api.emit('agent:turn-start', { sessionId: 'test', files: ['src/app.ts'] })

    const matched = emittedEvents.find((e) => e.event === 'rules:matched')
    expect(matched).toBeUndefined()
  })

  it('cleans up on dispose', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(eventHandlers.has('rules:register')).toBe(false)
    expect(eventHandlers.has('agent:turn-start')).toBe(false)
  })
})
