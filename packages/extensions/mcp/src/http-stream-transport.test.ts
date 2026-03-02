import { afterEach, describe, expect, it, vi } from 'vitest'
import { HttpStreamTransport } from './http-stream-transport.js'
import type { JSONRPCMessage } from './transport.js'

// ---- Helpers ----

function createReadableStream(chunks: string[]): ReadableStream<Uint8Array> {
  const encoder = new TextEncoder()
  let index = 0
  return new ReadableStream({
    pull(controller) {
      if (index < chunks.length) {
        controller.enqueue(encoder.encode(chunks[index]!))
        index++
      } else {
        controller.close()
      }
    },
  })
}

function mockHeaders(values: Record<string, string>): Headers {
  const headers = new Headers()
  for (const [key, value] of Object.entries(values)) {
    headers.set(key, value)
  }
  return headers
}

afterEach(() => {
  vi.unstubAllGlobals()
})

// ---- Connection ----

describe('HttpStreamTransport', () => {
  describe('start', () => {
    it('opens a GET request for server notifications', async () => {
      const fetchMock = vi.fn().mockResolvedValue({
        ok: true,
        body: createReadableStream([]),
        headers: mockHeaders({}),
      })
      vi.stubGlobal('fetch', fetchMock)

      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })
      await transport.start()

      // Give the notification listener time to fire
      await new Promise((r) => setTimeout(r, 10))

      const getCall = fetchMock.mock.calls.find(
        (call: unknown[]) => (call[1] as { method?: string })?.method === 'GET'
      )
      expect(getCall).toBeDefined()
      expect((getCall![1] as { headers: Record<string, string> }).headers.Accept).toBe(
        'text/event-stream'
      )

      await transport.close()
    })
  })

  // ---- Sending ----

  describe('send', () => {
    it('sends POST requests with JSON body', async () => {
      const fetchMock = vi.fn().mockResolvedValue({
        ok: true,
        body: null,
        headers: mockHeaders({ 'Content-Type': 'application/json' }),
        json: vi.fn().mockResolvedValue({ jsonrpc: '2.0', id: 1, result: 'ok' }),
      })
      vi.stubGlobal('fetch', fetchMock)

      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })
      await transport.start()

      // Give the notification listener time to fire
      await new Promise((r) => setTimeout(r, 10))

      const msg: JSONRPCMessage = { jsonrpc: '2.0', id: 1, method: 'initialize' }
      await transport.send(msg)

      const postCall = fetchMock.mock.calls.find(
        (call: unknown[]) => (call[1] as { method?: string })?.method === 'POST'
      )
      expect(postCall).toBeDefined()
      expect(JSON.parse((postCall![1] as { body: string }).body)).toEqual(msg)

      await transport.close()
    })

    it('throws when transport not started', async () => {
      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })

      await expect(transport.send({ jsonrpc: '2.0', id: 1, method: 'test' })).rejects.toThrow(
        'Transport not started'
      )
    })

    it('throws on non-ok HTTP response', async () => {
      const fetchMock = vi.fn()
      // GET for notifications — ok
      fetchMock.mockResolvedValueOnce({
        ok: true,
        body: createReadableStream([]),
        headers: mockHeaders({}),
      })
      // POST — error
      fetchMock.mockResolvedValueOnce({
        ok: false,
        status: 500,
        statusText: 'Internal Server Error',
        headers: mockHeaders({}),
      })
      vi.stubGlobal('fetch', fetchMock)

      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })
      await transport.start()
      await new Promise((r) => setTimeout(r, 10))

      await expect(transport.send({ jsonrpc: '2.0', id: 1, method: 'test' })).rejects.toThrow(
        'HTTP POST failed (500)'
      )

      await transport.close()
    })

    it('includes custom headers in requests', async () => {
      const fetchMock = vi.fn().mockResolvedValue({
        ok: true,
        body: null,
        headers: mockHeaders({}),
        json: vi.fn().mockResolvedValue({}),
      })
      vi.stubGlobal('fetch', fetchMock)

      const transport = new HttpStreamTransport({
        url: 'http://localhost:8080/mcp',
        headers: { Authorization: 'Bearer token123' },
      })
      await transport.start()
      await new Promise((r) => setTimeout(r, 10))

      await transport.send({ jsonrpc: '2.0', id: 1, method: 'test' })

      const postCall = fetchMock.mock.calls.find(
        (call: unknown[]) => (call[1] as { method?: string })?.method === 'POST'
      )
      expect((postCall![1] as { headers: Record<string, string> }).headers.Authorization).toBe(
        'Bearer token123'
      )

      await transport.close()
    })
  })

  // ---- Session ID ----

  describe('session management', () => {
    it('captures session ID from response headers', async () => {
      const fetchMock = vi.fn()
      // GET for notifications
      fetchMock.mockResolvedValueOnce({
        ok: true,
        body: createReadableStream([]),
        headers: mockHeaders({}),
      })
      // POST response with session ID
      fetchMock.mockResolvedValueOnce({
        ok: true,
        body: null,
        headers: mockHeaders({
          'Content-Type': 'application/json',
          'Mcp-Session-Id': 'session-abc-123',
        }),
        json: vi.fn().mockResolvedValue({ jsonrpc: '2.0', id: 1, result: {} }),
      })
      vi.stubGlobal('fetch', fetchMock)

      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })
      await transport.start()
      await new Promise((r) => setTimeout(r, 10))

      expect(transport.getSessionId()).toBeNull()

      await transport.send({ jsonrpc: '2.0', id: 1, method: 'initialize' })

      expect(transport.getSessionId()).toBe('session-abc-123')

      await transport.close()
    })

    it('includes session ID in subsequent requests', async () => {
      const fetchMock = vi.fn()
      // GET for notifications
      fetchMock.mockResolvedValueOnce({
        ok: true,
        body: createReadableStream([]),
        headers: mockHeaders({}),
      })
      // First POST — returns session ID
      fetchMock.mockResolvedValueOnce({
        ok: true,
        body: null,
        headers: mockHeaders({
          'Content-Type': 'application/json',
          'Mcp-Session-Id': 'session-xyz',
        }),
        json: vi.fn().mockResolvedValue({ jsonrpc: '2.0', id: 1, result: {} }),
      })
      // Second POST — should include session ID
      fetchMock.mockResolvedValueOnce({
        ok: true,
        body: null,
        headers: mockHeaders({ 'Content-Type': 'application/json' }),
        json: vi.fn().mockResolvedValue({ jsonrpc: '2.0', id: 2, result: {} }),
      })
      vi.stubGlobal('fetch', fetchMock)

      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })
      await transport.start()
      await new Promise((r) => setTimeout(r, 10))

      await transport.send({ jsonrpc: '2.0', id: 1, method: 'initialize' })
      await transport.send({ jsonrpc: '2.0', id: 2, method: 'tools/list' })

      // The third fetch call (second POST) should have the session header
      const thirdCall = fetchMock.mock.calls[2]
      expect((thirdCall![1] as { headers: Record<string, string> }).headers['Mcp-Session-Id']).toBe(
        'session-xyz'
      )

      await transport.close()
    })

    it('clears session ID on close', async () => {
      const fetchMock = vi.fn()
      fetchMock.mockResolvedValueOnce({
        ok: true,
        body: createReadableStream([]),
        headers: mockHeaders({ 'Mcp-Session-Id': 'session-1' }),
      })
      vi.stubGlobal('fetch', fetchMock)

      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })
      await transport.start()
      await new Promise((r) => setTimeout(r, 10))

      await transport.close()

      expect(transport.getSessionId()).toBeNull()
    })
  })

  // ---- Receiving responses ----

  describe('receiving messages', () => {
    it('handles JSON response from POST', async () => {
      const fetchMock = vi.fn()
      // GET for notifications
      fetchMock.mockResolvedValueOnce({
        ok: true,
        body: createReadableStream([]),
        headers: mockHeaders({}),
      })
      // POST with JSON response
      const responseMsg = { jsonrpc: '2.0', id: 1, result: { tools: [] } }
      fetchMock.mockResolvedValueOnce({
        ok: true,
        body: null,
        headers: mockHeaders({ 'Content-Type': 'application/json' }),
        json: vi.fn().mockResolvedValue(responseMsg),
      })
      vi.stubGlobal('fetch', fetchMock)

      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })
      const received: JSONRPCMessage[] = []
      transport.onMessage((msg) => received.push(msg))

      await transport.start()
      await new Promise((r) => setTimeout(r, 10))

      await transport.send({ jsonrpc: '2.0', id: 1, method: 'tools/list' })

      expect(received).toHaveLength(1)
      expect(received[0]).toEqual(responseMsg)

      await transport.close()
    })

    it('handles SSE streaming response from POST', async () => {
      const fetchMock = vi.fn()
      // GET for notifications
      fetchMock.mockResolvedValueOnce({
        ok: true,
        body: createReadableStream([]),
        headers: mockHeaders({}),
      })
      // POST with SSE response
      const sseData =
        'data: {"jsonrpc":"2.0","id":1,"result":{"capabilities":{}}}\n\n' +
        'data: {"jsonrpc":"2.0","method":"notifications/progress","params":{"progress":50}}\n\n'
      fetchMock.mockResolvedValueOnce({
        ok: true,
        body: createReadableStream([sseData]),
        headers: mockHeaders({ 'Content-Type': 'text/event-stream' }),
      })
      vi.stubGlobal('fetch', fetchMock)

      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })
      const received: JSONRPCMessage[] = []
      transport.onMessage((msg) => received.push(msg))

      await transport.start()
      await new Promise((r) => setTimeout(r, 10))

      await transport.send({ jsonrpc: '2.0', id: 1, method: 'initialize' })

      expect(received).toHaveLength(2)
      expect(received[0]).toEqual({
        jsonrpc: '2.0',
        id: 1,
        result: { capabilities: {} },
      })
      expect(received[1]).toEqual({
        jsonrpc: '2.0',
        method: 'notifications/progress',
        params: { progress: 50 },
      })

      await transport.close()
    })

    it('handles SSE events from GET notification stream', async () => {
      const sseData = 'data: {"jsonrpc":"2.0","method":"notifications/tools/list_changed"}\n\n'

      const fetchMock = vi.fn().mockResolvedValue({
        ok: true,
        body: createReadableStream([sseData]),
        headers: mockHeaders({}),
      })
      vi.stubGlobal('fetch', fetchMock)

      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })
      const received: JSONRPCMessage[] = []
      transport.onMessage((msg) => received.push(msg))

      await transport.start()
      await new Promise((r) => setTimeout(r, 50))

      expect(received).toHaveLength(1)
      expect(received[0]).toEqual({
        jsonrpc: '2.0',
        method: 'notifications/tools/list_changed',
      })

      await transport.close()
    })

    it('handles chunked SSE data across multiple reads', async () => {
      const part1 = 'data: {"jsonrpc":"2.0","id":1,'
      const part2 = '"result":"hello"}\n\n'

      const fetchMock = vi.fn()
      // GET for notifications — two chunks
      fetchMock.mockResolvedValueOnce({
        ok: true,
        body: createReadableStream([part1, part2]),
        headers: mockHeaders({}),
      })
      vi.stubGlobal('fetch', fetchMock)

      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })
      const received: JSONRPCMessage[] = []
      transport.onMessage((msg) => received.push(msg))

      await transport.start()
      await new Promise((r) => setTimeout(r, 50))

      expect(received).toHaveLength(1)
      expect(received[0]).toEqual({ jsonrpc: '2.0', id: 1, result: 'hello' })

      await transport.close()
    })
  })

  // ---- Error handling ----

  describe('error handling', () => {
    it('calls error handler on notification stream failure', async () => {
      const fetchMock = vi.fn().mockRejectedValue(new Error('Network error'))
      vi.stubGlobal('fetch', fetchMock)

      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })
      const errors: Error[] = []
      transport.onError((err) => errors.push(err))

      await transport.start()
      await new Promise((r) => setTimeout(r, 50))

      expect(errors).toHaveLength(1)
      expect(errors[0]!.message).toBe('Network error')

      await transport.close()
    })

    it('calls close handler when notification stream ends', async () => {
      const fetchMock = vi.fn().mockResolvedValue({
        ok: true,
        body: createReadableStream([]),
        headers: mockHeaders({}),
      })
      vi.stubGlobal('fetch', fetchMock)

      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })
      const closed = vi.fn()
      transport.onClose(closed)

      await transport.start()
      await new Promise((r) => setTimeout(r, 50))

      expect(closed).toHaveBeenCalledOnce()

      await transport.close()
    })
  })

  // ---- Disconnection ----

  describe('close', () => {
    it('prevents further sends after close', async () => {
      const fetchMock = vi.fn().mockResolvedValue({
        ok: true,
        body: createReadableStream([]),
        headers: mockHeaders({}),
      })
      vi.stubGlobal('fetch', fetchMock)

      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })
      await transport.start()
      await transport.close()

      await expect(transport.send({ jsonrpc: '2.0', id: 1, method: 'test' })).rejects.toThrow(
        'Transport not started'
      )
    })

    it('can close without starting', async () => {
      const transport = new HttpStreamTransport({ url: 'http://localhost:8080/mcp' })
      // Should not throw
      await transport.close()
    })
  })
})
