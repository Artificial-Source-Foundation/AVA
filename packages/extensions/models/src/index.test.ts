import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('models extension', () => {
  it('activates and listens for provider:registered', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('provider:registered')).toBe(true)
  })

  it('listens for models:register events', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('models:register')).toBe(true)
  })

  it('emits models:ready on activation', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)
    const ready = emittedEvents.find((e) => e.event === 'models:ready')
    expect(ready).toBeDefined()
    expect(ready!.data).toEqual({ count: 0 })
  })

  it('populates registry when provider:registered fires', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)

    api.emit('provider:registered', {
      provider: 'anthropic',
      models: [
        {
          id: 'claude-sonnet',
          provider: 'anthropic',
          displayName: 'Claude Sonnet',
          contextWindow: 200_000,
          maxOutput: 8192,
          supportsTools: true,
          supportsVision: true,
        },
      ],
    })

    const updated = emittedEvents.find((e) => e.event === 'models:updated')
    expect(updated).toBeDefined()
  })

  it('cleans up event listeners on dispose', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(eventHandlers.has('provider:registered')).toBe(false)
    expect(eventHandlers.has('models:register')).toBe(false)
  })
})
