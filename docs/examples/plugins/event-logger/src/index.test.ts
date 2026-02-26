import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

const ctx = { sessionId: 's', workingDirectory: '/', signal: new AbortController().signal }

describe('event-logger plugin', () => {
  it('registers event handlers for tracked events', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('agent:turn:start')).toBe(true)
    expect(eventHandlers.has('tool:before')).toBe(true)
    expect(eventHandlers.has('tool:after')).toBe(true)
  })

  it('registers the /events command', () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    expect(registeredCommands).toHaveLength(1)
    expect(registeredCommands[0].name).toBe('events')
  })

  it('logs emitted events to storage', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)

    // Emit an event that the logger tracks
    api.emit('agent:turn:start', { turn: 1 })
    // Give the async logEvent a tick to complete
    await new Promise((r) => setTimeout(r, 10))

    const result = await registeredCommands[0].execute('', ctx)
    expect(result).toContain('agent:turn:start')
  })

  it('shows no events initially', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)
    const result = await registeredCommands[0].execute('', ctx)
    expect(result).toBe('No events logged yet.')
  })

  it('clears event log', async () => {
    const { api, registeredCommands } = createMockExtensionAPI()
    activate(api)

    api.emit('agent:turn:start', {})
    await new Promise((r) => setTimeout(r, 10))

    const clearResult = await registeredCommands[0].execute('clear', ctx)
    expect(clearResult).toBe('Event log cleared.')

    const listResult = await registeredCommands[0].execute('', ctx)
    expect(listResult).toBe('No events logged yet.')
  })

  it('cleans up all handlers on dispose', () => {
    const { api, eventHandlers, registeredCommands } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(eventHandlers.size).toBeGreaterThan(0)
    expect(registeredCommands).toHaveLength(1)
    disposable.dispose()
    expect(eventHandlers.size).toBe(0)
    expect(registeredCommands).toHaveLength(0)
  })
})
