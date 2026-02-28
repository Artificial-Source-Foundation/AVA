import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { MCPClient } from './client.js'
import type { JSONRPCMessage, MCPTransport } from './transport.js'

function createMockTransport(): MCPTransport & {
  sentMessages: JSONRPCMessage[]
  simulateResponse: (msg: JSONRPCMessage) => void
} {
  let handler: ((msg: JSONRPCMessage) => void) | null = null
  const sentMessages: JSONRPCMessage[] = []

  return {
    sentMessages,
    async start() {},
    async send(msg) {
      sentMessages.push(msg)
    },
    onMessage(h) {
      handler = h
    },
    async close() {},
    simulateResponse(msg) {
      handler?.(msg)
    },
  }
}

describe('MCPClient', () => {
  let transport: ReturnType<typeof createMockTransport>
  let client: MCPClient

  beforeEach(() => {
    transport = createMockTransport()
    client = new MCPClient(transport, { timeoutMs: 500 })
  })

  afterEach(async () => {
    await client.close()
  })

  it('sends initialize and notifications/initialized', async () => {
    const initPromise = client.initialize()

    // Respond to initialize request
    await new Promise((r) => setTimeout(r, 5))
    const initReq = transport.sentMessages[0]
    expect(initReq).toBeDefined()
    expect((initReq as { method: string }).method).toBe('initialize')

    transport.simulateResponse({
      jsonrpc: '2.0',
      id: (initReq as { id: number }).id,
      result: { capabilities: { tools: {} }, protocolVersion: '2024-11-05' },
    })

    const caps = await initPromise

    expect(caps).toEqual({ tools: {} })
    // Should have sent notifications/initialized
    const notif = transport.sentMessages[1]
    expect(notif).toBeDefined()
    expect((notif as { method: string }).method).toBe('notifications/initialized')
  })

  it('lists tools after initialization', async () => {
    // Initialize first
    const initPromise = client.initialize()
    await new Promise((r) => setTimeout(r, 5))
    transport.simulateResponse({
      jsonrpc: '2.0',
      id: 1,
      result: { capabilities: {}, protocolVersion: '2024-11-05' },
    })
    await initPromise

    // Now list tools
    const listPromise = client.listTools()
    await new Promise((r) => setTimeout(r, 5))

    const listReq = transport.sentMessages.find((m) => 'method' in m && m.method === 'tools/list')
    expect(listReq).toBeDefined()

    transport.simulateResponse({
      jsonrpc: '2.0',
      id: (listReq as { id: number }).id,
      result: {
        tools: [{ name: 'read_file', description: 'Read a file', inputSchema: { type: 'object' } }],
      },
    })

    const tools = await listPromise
    expect(tools).toHaveLength(1)
    expect(tools[0].name).toBe('read_file')
  })

  it('calls a tool and returns result', async () => {
    // Initialize
    const initPromise = client.initialize()
    await new Promise((r) => setTimeout(r, 5))
    transport.simulateResponse({
      jsonrpc: '2.0',
      id: 1,
      result: { capabilities: {}, protocolVersion: '2024-11-05' },
    })
    await initPromise

    // Call tool
    const callPromise = client.callTool('read_file', { path: '/foo.txt' })
    await new Promise((r) => setTimeout(r, 5))

    const callReq = transport.sentMessages.find((m) => 'method' in m && m.method === 'tools/call')
    expect(callReq).toBeDefined()

    transport.simulateResponse({
      jsonrpc: '2.0',
      id: (callReq as { id: number }).id,
      result: { content: [{ type: 'text', text: 'file contents' }] },
    })

    const result = await callPromise
    expect(result.content[0].text).toBe('file contents')
  })

  it('handles error responses from server', async () => {
    // Initialize
    const initPromise = client.initialize()
    await new Promise((r) => setTimeout(r, 5))
    transport.simulateResponse({
      jsonrpc: '2.0',
      id: 1,
      result: { capabilities: {}, protocolVersion: '2024-11-05' },
    })
    await initPromise

    // Call tool that errors
    const callPromise = client.callTool('bad_tool', {})
    await new Promise((r) => setTimeout(r, 5))

    const callReq = transport.sentMessages.find((m) => 'method' in m && m.method === 'tools/call')
    transport.simulateResponse({
      jsonrpc: '2.0',
      id: (callReq as { id: number }).id,
      error: { code: -32601, message: 'Method not found' },
    })

    await expect(callPromise).rejects.toThrow('MCP error (-32601): Method not found')
  })

  it('times out pending requests', async () => {
    // Don't respond to initialize — should timeout at 500ms
    const initPromise = client.initialize()
    await expect(initPromise).rejects.toThrow('Request timed out')
  })

  it('rejects pending requests on close', async () => {
    const initPromise = client.initialize()
    await new Promise((r) => setTimeout(r, 5))

    await client.close()

    await expect(initPromise).rejects.toThrow('Client closed')
  })

  it('throws if not initialized', async () => {
    await expect(client.listTools()).rejects.toThrow('Client not initialized')
    await expect(client.callTool('foo', {})).rejects.toThrow('Client not initialized')
  })
})
