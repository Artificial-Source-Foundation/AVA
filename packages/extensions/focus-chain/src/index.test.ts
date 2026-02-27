import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('focus-chain extension', () => {
  it('activates and listens for agent lifecycle events', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('agent:turn-start')).toBe(true)
    expect(eventHandlers.has('agent:turn-end')).toBe(true)
    expect(eventHandlers.has('agent:completed')).toBe(true)
  })

  it('emits focus:updated on turn-start', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)
    api.emit('agent:turn-start', { sessionId: 'test', description: 'Working' })
    const updated = emittedEvents.find((e) => e.event === 'focus:updated')
    expect(updated).toBeDefined()
  })

  it('emits focus:updated on turn-end', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)
    api.emit('agent:turn-start', { sessionId: 'test' })
    api.emit('agent:turn-end', { sessionId: 'test' })
    const updates = emittedEvents.filter((e) => e.event === 'focus:updated')
    expect(updates.length).toBeGreaterThanOrEqual(2)
  })

  it('emits focus:completed when agent completes', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)
    api.emit('agent:turn-start', { sessionId: 'test' })
    api.emit('agent:completed', { sessionId: 'test' })
    const completed = emittedEvents.find((e) => e.event === 'focus:completed')
    expect(completed).toBeDefined()
  })

  it('cleans up on dispose', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(eventHandlers.has('agent:turn-start')).toBe(false)
  })
})
