import { afterEach, describe, expect, it, vi } from 'vitest'

const healthStartMock = vi.fn()
const healthStopMock = vi.fn()
const serverStartMock = vi.fn().mockResolvedValue(undefined)
const serverStopMock = vi.fn().mockResolvedValue(undefined)

vi.mock('./health.js', () => ({
  MCPHealthMonitor: class MockHealthMonitor {
    start = healthStartMock
    stop = healthStopMock
  },
}))

vi.mock('./server.js', () => ({
  MCPToolServer: class MockMCPToolServer {
    start = serverStartMock
    stop = serverStopMock
  },
}))

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
  getConnectedServers: vi.fn().mockReturnValue([]),
  ping: vi.fn().mockResolvedValue(true),
  restart: vi.fn().mockResolvedValue(undefined),
  refreshServerTools: vi.fn().mockResolvedValue([]),
  resetMCP: vi.fn().mockResolvedValue(undefined),
}))

import { createMockExtensionAPI } from '../../../core-v2/src/__test-utils__/mock-extension-api.js'
import { activate } from './index.js'
import { connectServer, resetMCP } from './manager.js'

function activateMCP(api: unknown) {
  return activate(api as never)
}

describe('mcp extension', () => {
  afterEach(() => {
    vi.clearAllMocks()
  })

  it('activates and emits mcp:ready', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activateMCP(api)
    const ready = emittedEvents.find((e) => e.event === 'mcp:ready')
    expect(ready).toBeDefined()
    expect(healthStartMock).toHaveBeenCalledOnce()
  })

  it('listens for mcp:add-server and mcp:remove-server', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    activateMCP(api)
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

    activateMCP(api)
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

    activateMCP(api)
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

    activateMCP(api)
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

    activateMCP(api)
    await new Promise((r) => setTimeout(r, 10))

    expect(api.log.error).toHaveBeenCalled()
    const errorEvent = emittedEvents.find((e) => e.event === 'mcp:error')
    expect(errorEvent).toBeDefined()
  })

  it('adds server dynamically via mcp:add-server event', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activateMCP(api)

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
    activateMCP(api)

    api.emit('mcp:remove-server', { name: 'test' })
    await new Promise((r) => setTimeout(r, 10))

    const removed = emittedEvents.find((e) => e.event === 'mcp:server-removed')
    expect(removed).toBeDefined()
  })

  it('calls resetMCP on dispose', () => {
    const { api } = createMockExtensionAPI()
    const disposable = activateMCP(api)
    disposable.dispose()
    expect(healthStopMock).toHaveBeenCalledOnce()
    expect(resetMCP).toHaveBeenCalled()
  })

  it('starts mcp server mode when enabled in settings', async () => {
    const { api } = createMockExtensionAPI()
    api.getSettings = <T>(_ns: string) =>
      ({
        serverMode: {
          enabled: true,
          stdio: true,
        },
      }) as T

    const disposable = activateMCP(api)
    await new Promise((r) => setTimeout(r, 5))

    expect(serverStartMock).toHaveBeenCalledOnce()
    disposable.dispose()
    expect(serverStopMock).toHaveBeenCalledOnce()
  })

  it('re-registers tools when manager emits tools list changed callback', async () => {
    const { api, emittedEvents, registeredTools } = createMockExtensionAPI()

    ;(connectServer as ReturnType<typeof vi.fn>).mockImplementationOnce(
      async (
        _server,
        options?: {
          onToolsListChanged?: (
            serverName: string,
            tools: Array<{
              name: string
              description: string
              inputSchema: Record<string, unknown>
              serverName: string
            }>
          ) => void
        }
      ) => {
        const initialTools = [
          {
            name: 'read_file',
            description: 'Read a file',
            inputSchema: { type: 'object' },
            serverName: 'test',
          },
        ]

        setTimeout(() => {
          options?.onToolsListChanged?.('test', [
            {
              name: 'write_file',
              description: 'Write a file',
              inputSchema: { type: 'object' },
              serverName: 'test',
            },
          ])
        }, 1)

        return initialTools
      }
    )

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

    activateMCP(api)
    await new Promise((r) => setTimeout(r, 20))

    const updated = emittedEvents.find((e) => e.event === 'mcp:tools-updated')
    expect(updated).toBeDefined()
    expect(registeredTools.some((t) => t.definition?.name === 'mcp_test_write_file')).toBe(true)
  })
})
