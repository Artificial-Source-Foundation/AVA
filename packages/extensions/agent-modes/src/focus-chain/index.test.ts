import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('focus-chain extension', () => {
  it('activates and listens for agent lifecycle events', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('turn:start')).toBe(true)
    expect(eventHandlers.has('turn:end')).toBe(true)
    expect(eventHandlers.has('agent:finish')).toBe(true)
  })

  it('emits focus:updated on turn:start', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)
    api.emit('turn:start', { agentId: 'test', description: 'Working' })
    const updated = emittedEvents.find((e) => e.event === 'focus:updated')
    expect(updated).toBeDefined()
  })

  it('emits focus:updated on turn:end', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)
    api.emit('turn:start', { agentId: 'test' })
    api.emit('turn:end', { agentId: 'test' })
    const updates = emittedEvents.filter((e) => e.event === 'focus:updated')
    expect(updates.length).toBeGreaterThanOrEqual(2)
  })

  it('emits focus:completed when agent finishes', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)
    api.emit('turn:start', { agentId: 'test' })
    api.emit('agent:finish', { agentId: 'test' })
    const completed = emittedEvents.find((e) => e.event === 'focus:completed')
    expect(completed).toBeDefined()
  })

  it('cleans up on dispose', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(eventHandlers.has('turn:start')).toBe(false)
  })
})
