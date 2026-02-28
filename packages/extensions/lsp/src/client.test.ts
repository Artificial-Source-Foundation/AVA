import { describe, expect, it } from 'vitest'
import { LSPClient } from './client.js'
import type { LSPMessage, LSPTransport } from './transport.js'

function createMockTransport(): LSPTransport & {
  messageHandler: ((msg: LSPMessage) => void) | null
  sentMessages: LSPMessage[]
} {
  let messageHandler: ((msg: LSPMessage) => void) | null = null
  const sentMessages: LSPMessage[] = []

  return {
    messageHandler,
    sentMessages,
    async start() {},
    async send(msg: LSPMessage) {
      sentMessages.push(msg)
    },
    onMessage(handler) {
      messageHandler = handler
      // Also update the returned reference
      ;(this as { messageHandler: typeof messageHandler }).messageHandler = handler
    },
    onError() {},
    onClose() {},
    async close() {},
  }
}

describe('LSPClient', () => {
  it('initializes with server', async () => {
    const transport = createMockTransport()
    const client = new LSPClient(transport, { timeoutMs: 100 })

    // Simulate server response after initialize request
    const initPromise = client.initialize('file:///project')
    // Respond to the initialize request
    setTimeout(() => {
      transport.messageHandler?.({
        jsonrpc: '2.0',
        id: 1,
        result: {
          capabilities: { hoverProvider: true },
          serverInfo: { name: 'test-server' },
        },
      })
    }, 5)

    const result = await initPromise
    expect(result.capabilities.hoverProvider).toBe(true)
  })

  it('throws if not initialized', async () => {
    const transport = createMockTransport()
    const client = new LSPClient(transport)
    await expect(client.hover('file:///test.ts', { line: 0, character: 0 })).rejects.toThrow(
      'not initialized'
    )
  })

  it('times out pending requests', async () => {
    const transport = createMockTransport()
    const client = new LSPClient(transport, { timeoutMs: 50 })

    // Never respond
    await expect(client.initialize('file:///project')).rejects.toThrow('timed out')
  })

  it('handles server errors', async () => {
    const transport = createMockTransport()
    const client = new LSPClient(transport, { timeoutMs: 100 })

    const initPromise = client.initialize('file:///project')
    setTimeout(() => {
      transport.messageHandler?.({
        jsonrpc: '2.0',
        id: 1,
        error: { code: -32600, message: 'Invalid request' },
      })
    }, 5)

    await expect(initPromise).rejects.toThrow('Invalid request')
  })

  it('routes diagnostics to handler', async () => {
    const transport = createMockTransport()
    const client = new LSPClient(transport, { timeoutMs: 100 })

    const diagnostics: unknown[] = []
    client.onDiagnostics((params) => diagnostics.push(params))

    // Init first
    const initPromise = client.initialize('file:///project')
    setTimeout(() => {
      transport.messageHandler?.({
        jsonrpc: '2.0',
        id: 1,
        result: { capabilities: {} },
      })
    }, 5)
    await initPromise

    // Simulate server push notification
    transport.messageHandler?.({
      jsonrpc: '2.0',
      method: 'textDocument/publishDiagnostics',
      params: {
        uri: 'file:///test.ts',
        diagnostics: [
          {
            range: { start: { line: 0, character: 0 }, end: { line: 0, character: 5 } },
            message: 'error',
          },
        ],
      },
    })

    expect(diagnostics).toHaveLength(1)
  })

  it('closes cleanly', async () => {
    const transport = createMockTransport()
    const client = new LSPClient(transport)
    await client.close()
    // Should not throw
  })

  it('sends proper JSON-RPC messages', async () => {
    const transport = createMockTransport()
    const client = new LSPClient(transport, { timeoutMs: 100 })

    const initPromise = client.initialize('file:///project')
    setTimeout(() => {
      transport.messageHandler?.({
        jsonrpc: '2.0',
        id: 1,
        result: { capabilities: {} },
      })
    }, 5)
    await initPromise

    // Check that initialize + initialized were sent
    expect(transport.sentMessages).toHaveLength(2)
    expect(transport.sentMessages[0]!.method).toBe('initialize')
    expect(transport.sentMessages[1]!.method).toBe('initialized')
  })
})
