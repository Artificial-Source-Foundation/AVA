import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { afterEach, describe, expect, it } from 'vitest'
import { activate } from './index.js'
import { resetMCP } from './manager.js'

describe('mcp extension', () => {
  afterEach(() => resetMCP())

  it('activates and emits mcp:ready', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)
    const ready = emittedEvents.find((e) => e.event === 'mcp:ready')
    expect(ready).toBeDefined()
  })

  it('listens for mcp:connected, mcp:add-server, mcp:remove-server', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('mcp:connected')).toBe(true)
    expect(eventHandlers.has('mcp:add-server')).toBe(true)
    expect(eventHandlers.has('mcp:remove-server')).toBe(true)
  })

  it('adds servers via mcp:add-server event', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)
    api.emit('mcp:add-server', {
      name: 'test-server',
      uri: 'http://localhost:3000',
      transport: 'sse' as const,
    })
    const added = emittedEvents.find((e) => e.event === 'mcp:server-added')
    expect(added).toBeDefined()
  })

  it('removes servers via mcp:remove-server event', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)
    api.emit('mcp:add-server', {
      name: 'test-server',
      uri: 'http://localhost:3000',
      transport: 'sse' as const,
    })
    api.emit('mcp:remove-server', { name: 'test-server' })
    const removed = emittedEvents.find((e) => e.event === 'mcp:server-removed')
    expect(removed).toBeDefined()
  })

  it('cleans up connections on dispose', () => {
    const { api } = createMockExtensionAPI()
    const disposable = activate(api)
    api.emit('mcp:add-server', {
      name: 'test',
      uri: 'http://localhost',
      transport: 'stdio' as const,
    })
    disposable.dispose()
    // resetMCP is called on dispose
    expect(api.log.debug).toHaveBeenCalledWith('MCP extension deactivated')
  })
})
