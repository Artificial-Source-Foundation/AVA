import { afterEach, describe, expect, it, vi } from 'vitest'

// Mock manager module
vi.mock('./manager.js', () => ({
  connectServer: vi.fn().mockResolvedValue([
    {
      name: 'read_file',
      description: 'Read a file',
      inputSchema: { type: 'object' },
      serverName: 'test',
    },
  ]),
  disconnectServer: vi.fn().mockResolvedValue(undefined),
  callTool: vi.fn().mockResolvedValue({ success: true, output: 'tool result' }),
  getTools: vi.fn().mockReturnValue([]),
  resetMCP: vi.fn().mockResolvedValue(undefined),
}))

import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { activate } from './index.js'
import { connectServer, resetMCP } from './manager.js'

describe('mcp extension', () => {
  afterEach(() => {
    vi.clearAllMocks()
  })

  it('activates and emits mcp:ready', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)
    const ready = emittedEvents.find((e) => e.event === 'mcp:ready')
    expect(ready).toBeDefined()
  })

  it('listens for mcp:add-server and mcp:remove-server', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activate(api)
    expect(eventHandlers.has('mcp:add-server')).toBe(true)
    expect(eventHandlers.has('mcp:remove-server')).toBe(true)
  })

  it('connects servers from settings on activate', async () => {
    const { api } = createMockExtensionAPI()
    // Override getSettings to return servers
    api.getSettings = <T>(_ns: string) =>
      ({
        servers: [
          {
            name: 'test',
            uri: 'stdio://test',
            transport: 'stdio',
            command: 'node',
            args: ['server.js'],
          },
        ],
      }) as T

    activate(api)
    // Wait for async connectServer to be called
    await new Promise((r) => setTimeout(r, 10))

    expect(connectServer).toHaveBeenCalledOnce()
  })

  it('registers tools from connected server', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    api.getSettings = <T>(_ns: string) =>
      ({
        servers: [
          {
            name: 'test',
            uri: 'stdio://test',
            transport: 'stdio',
            command: 'node',
            args: ['server.js'],
          },
        ],
      }) as T

    activate(api)
    await new Promise((r) => setTimeout(r, 10))

    expect(registeredTools.length).toBeGreaterThan(0)
    const tool = registeredTools[0]
    expect(tool.definition?.name).toBe('mcp_test_read_file')
    expect(tool.definition?.description).toContain('[MCP: test]')
  })

  it('tool execute calls callTool', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    api.getSettings = <T>(_ns: string) =>
      ({
        servers: [
          {
            name: 'test',
            uri: 'stdio://test',
            transport: 'stdio',
            command: 'node',
            args: ['server.js'],
          },
        ],
      }) as T

    activate(api)
    await new Promise((r) => setTimeout(r, 10))

    const tool = registeredTools[0]
    const result = await tool.execute!({ path: '/foo' }, {} as never)
    expect(result.success).toBe(true)
    expect(result.output).toBe('tool result')
  })

  it('handles connection errors gracefully', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    ;(connectServer as ReturnType<typeof vi.fn>).mockRejectedValueOnce(
      new Error('Connection refused')
    )

    api.getSettings = <T>(_ns: string) =>
      ({
        servers: [{ name: 'broken', uri: 'stdio://broken', transport: 'stdio', command: 'bad' }],
      }) as T

    activate(api)
    await new Promise((r) => setTimeout(r, 10))

    expect(api.log.error).toHaveBeenCalled()
    const errorEvent = emittedEvents.find((e) => e.event === 'mcp:error')
    expect(errorEvent).toBeDefined()
  })

  it('adds server dynamically via mcp:add-server event', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)

    api.emit('mcp:add-server', {
      name: 'dynamic',
      uri: 'stdio://dynamic',
      transport: 'stdio',
      command: 'node',
      args: ['server.js'],
    })

    await new Promise((r) => setTimeout(r, 10))

    const connected = emittedEvents.find((e) => e.event === 'mcp:connected')
    expect(connected).toBeDefined()
  })

  it('removes server via mcp:remove-server event', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activate(api)

    api.emit('mcp:remove-server', { name: 'test' })
    await new Promise((r) => setTimeout(r, 10))

    const removed = emittedEvents.find((e) => e.event === 'mcp:server-removed')
    expect(removed).toBeDefined()
  })

  it('calls resetMCP on dispose', () => {
    const { api } = createMockExtensionAPI()
    const disposable = activate(api)
    disposable.dispose()
    expect(resetMCP).toHaveBeenCalled()
  })
})
