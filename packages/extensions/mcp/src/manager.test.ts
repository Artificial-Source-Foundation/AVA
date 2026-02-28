import { afterEach, describe, expect, it, vi } from 'vitest'
import type { MCPServer } from './types.js'

// Mock client — class that uses plain functions to avoid vi.fn() hoisting issues
const mockClientInstances: Array<{
  initialize: ReturnType<typeof vi.fn>
  listTools: ReturnType<typeof vi.fn>
  callTool: ReturnType<typeof vi.fn>
  close: ReturnType<typeof vi.fn>
}> = []

vi.mock('./client.js', () => {
  return {
    MCPClient: class MockMCPClient {
      initialize: ReturnType<typeof vi.fn>
      listTools: ReturnType<typeof vi.fn>
      callTool: ReturnType<typeof vi.fn>
      close: ReturnType<typeof vi.fn>

      constructor() {
        this.initialize = vi.fn().mockResolvedValue({ tools: {} })
        this.listTools = vi
          .fn()
          .mockResolvedValue([
            { name: 'read_file', description: 'Read a file', inputSchema: { type: 'object' } },
          ])
        this.callTool = vi.fn().mockResolvedValue({
          content: [{ type: 'text', text: 'result' }],
        })
        this.close = vi.fn().mockResolvedValue(undefined)
        mockClientInstances.push(this)
      }
    },
  }
})

vi.mock('./transport.js', () => {
  return {
    StdioTransport: class MockStdioTransport {
      start = vi.fn().mockResolvedValue(undefined)
      send = vi.fn().mockResolvedValue(undefined)
      onMessage = vi.fn()
      close = vi.fn().mockResolvedValue(undefined)
    },
    SSETransport: class MockSSETransport {
      start = vi.fn().mockResolvedValue(undefined)
      send = vi.fn().mockResolvedValue(undefined)
      onMessage = vi.fn()
      close = vi.fn().mockResolvedValue(undefined)
    },
  }
})

const { callTool, connectServer, disconnectServer, getConnections, getTools, resetMCP } =
  await import('./manager.js')

const mockShell = {
  exec: vi.fn(),
  spawn: vi.fn(),
}

const stdioServer: MCPServer = {
  name: 'test-stdio',
  uri: 'stdio://test',
  transport: 'stdio',
  command: 'node',
  args: ['server.js'],
}

const sseServer: MCPServer = {
  name: 'test-sse',
  uri: 'http://localhost:3000/sse',
  transport: 'sse',
}

describe('MCP manager', () => {
  afterEach(async () => {
    await resetMCP()
    mockClientInstances.length = 0
  })

  it('starts with no connections', () => {
    expect(getConnections().size).toBe(0)
  })

  it('connects a stdio server and returns tools', async () => {
    const tools = await connectServer(stdioServer, mockShell)
    expect(tools).toHaveLength(1)
    expect(tools[0].name).toBe('read_file')
    expect(tools[0].serverName).toBe('test-stdio')

    const conn = getConnections().get('test-stdio')
    expect(conn).toBeDefined()
    expect(conn!.status).toBe('connected')
  })

  it('connects an SSE server', async () => {
    const tools = await connectServer(sseServer, mockShell)
    expect(tools).toHaveLength(1)

    const conn = getConnections().get('test-sse')
    expect(conn!.status).toBe('connected')
  })

  it('disconnects a server', async () => {
    await connectServer(stdioServer, mockShell)
    expect(getConnections().size).toBe(1)

    await disconnectServer('test-stdio')
    expect(getConnections().size).toBe(0)
  })

  it('disconnecting non-existent server is a no-op', async () => {
    await expect(disconnectServer('nope')).resolves.toBeUndefined()
  })

  it('getTools returns tools from connected servers', async () => {
    await connectServer(stdioServer, mockShell)
    const tools = getTools()
    expect(tools).toHaveLength(1)
    expect(tools[0].name).toBe('read_file')
  })

  it('callTool delegates to the MCP client', async () => {
    await connectServer(stdioServer, mockShell)
    const result = await callTool('test-stdio', 'read_file', { path: '/foo' })
    expect(result.success).toBe(true)
    expect(result.output).toBe('result')
  })

  it('callTool returns error for disconnected server', async () => {
    const result = await callTool('nonexistent', 'foo', {})
    expect(result.success).toBe(false)
    expect(result.output).toContain('not connected')
  })

  it('resetMCP disconnects all servers', async () => {
    await connectServer(stdioServer, mockShell)
    await connectServer(sseServer, mockShell)
    expect(getConnections().size).toBe(2)

    await resetMCP()
    expect(getConnections().size).toBe(0)
  })

  it('throws if stdio server has no command', async () => {
    const bad: MCPServer = { name: 'bad', uri: 'stdio://bad', transport: 'stdio' }
    await expect(connectServer(bad, mockShell)).rejects.toThrow('requires a command')
  })
})
