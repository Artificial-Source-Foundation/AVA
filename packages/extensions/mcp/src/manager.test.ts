import { afterEach, describe, expect, it } from 'vitest'
import { addServer, getConnections, getTools, removeServer, resetMCP } from './manager.js'
import type { MCPServer, MCPTool } from './types.js'

const server = (name: string): MCPServer => ({
  name,
  uri: `stdio://${name}`,
  transport: 'stdio',
})

describe('MCP manager', () => {
  afterEach(() => {
    resetMCP()
  })

  it('starts with no connections', () => {
    expect(getConnections().size).toBe(0)
  })

  it('adds a server in disconnected state', () => {
    addServer(server('test-server'))
    const conn = getConnections().get('test-server')
    expect(conn).toBeDefined()
    expect(conn!.status).toBe('disconnected')
    expect(conn!.tools).toEqual([])
    expect(conn!.server.name).toBe('test-server')
  })

  it('removes a server', () => {
    addServer(server('s1'))
    addServer(server('s2'))
    expect(getConnections().size).toBe(2)
    removeServer('s1')
    expect(getConnections().size).toBe(1)
    expect(getConnections().has('s1')).toBe(false)
    expect(getConnections().has('s2')).toBe(true)
  })

  it('removing non-existent server is a no-op', () => {
    expect(() => removeServer('nope')).not.toThrow()
  })

  it('getTools returns empty when no servers', () => {
    expect(getTools()).toEqual([])
  })

  it('getTools returns empty when all servers disconnected', () => {
    addServer(server('s1'))
    expect(getTools()).toEqual([])
  })

  it('getTools returns tools only from connected servers', () => {
    addServer(server('s1'))
    addServer(server('s2'))

    // Simulate connecting s1 with tools
    const conn1 = getConnections().get('s1')!
    ;(conn1 as { status: string }).status = 'connected'
    const tool: MCPTool = {
      name: 'tool1',
      description: 'A tool',
      inputSchema: { type: 'object' },
      serverName: 's1',
    }
    conn1.tools.push(tool)

    const tools = getTools()
    expect(tools).toHaveLength(1)
    expect(tools[0].name).toBe('tool1')
    expect(tools[0].serverName).toBe('s1')
  })

  it('aggregates tools from multiple connected servers', () => {
    addServer(server('s1'))
    addServer(server('s2'))

    const conn1 = getConnections().get('s1')!
    ;(conn1 as { status: string }).status = 'connected'
    conn1.tools.push({ name: 't1', description: '', inputSchema: {}, serverName: 's1' })

    const conn2 = getConnections().get('s2')!
    ;(conn2 as { status: string }).status = 'connected'
    conn2.tools.push({ name: 't2', description: '', inputSchema: {}, serverName: 's2' })

    expect(getTools()).toHaveLength(2)
  })

  it('resetMCP clears all connections', () => {
    addServer(server('s1'))
    addServer(server('s2'))
    expect(getConnections().size).toBe(2)
    resetMCP()
    expect(getConnections().size).toBe(0)
    expect(getTools()).toEqual([])
  })
})
