import { describe, expect, it, vi } from 'vitest'
import type { JSONRPCMessage } from './transport.js'
import { SSETransport, StdioTransport } from './transport.js'

// ---- StdioTransport ----

function createMockShell() {
  let pushChunk: ((chunk: Uint8Array) => void) | null = null
  let closeStream: (() => void) | null = null

  const stdout = new ReadableStream<Uint8Array>({
    start(controller) {
      pushChunk = (chunk) => controller.enqueue(chunk)
      closeStream = () => controller.close()
    },
  })

  const stdinChunks: string[] = []
  const stdin = new WritableStream<Uint8Array>({
    write(chunk) {
      stdinChunks.push(new TextDecoder().decode(chunk))
    },
  })

  const shell = {
    exec: vi.fn(),
    spawn: vi.fn(() => ({
      pid: 42,
      stdin,
      stdout,
      stderr: null,
      kill: vi.fn(),
      wait: vi.fn(),
    })),
  }

  return {
    shell,
    stdinChunks,
    pushChunk: (c: Uint8Array) => pushChunk?.(c),
    closeStream: () => closeStream?.(),
  }
}

describe('StdioTransport', () => {
  it('sends JSON-RPC messages as newline-delimited JSON', async () => {
    const { shell, stdinChunks, closeStream } = createMockShell()
    const transport = new StdioTransport(shell, 'node', ['server.js'])

    await transport.start()
    const msg: JSONRPCMessage = { jsonrpc: '2.0', id: 1, method: 'test' }
    await transport.send(msg)

    expect(stdinChunks[0]).toBe('{"jsonrpc":"2.0","id":1,"method":"test"}\n')

    closeStream()
    await transport.close()
  })

  it('receives and parses newline-delimited JSON messages', async () => {
    const { shell, pushChunk, closeStream } = createMockShell()
    const transport = new StdioTransport(shell, 'node', ['server.js'])

    const received: JSONRPCMessage[] = []
    transport.onMessage((msg) => received.push(msg))

    await transport.start()

    const msg = { jsonrpc: '2.0', id: 1, result: { ok: true } }
    pushChunk(new TextEncoder().encode(`${JSON.stringify(msg)}\n`))

    // Give the read loop time to process
    await new Promise((r) => setTimeout(r, 10))

    expect(received).toHaveLength(1)
    expect(received[0]).toEqual(msg)

    closeStream()
    await transport.close()
  })

  it('handles partial messages across chunks', async () => {
    const { shell, pushChunk, closeStream } = createMockShell()
    const transport = new StdioTransport(shell, 'node', ['server.js'])

    const received: JSONRPCMessage[] = []
    transport.onMessage((msg) => received.push(msg))

    await transport.start()

    const full = '{"jsonrpc":"2.0","id":1,"result":"ok"}\n'
    // Send in two chunks
    pushChunk(new TextEncoder().encode(full.slice(0, 20)))
    await new Promise((r) => setTimeout(r, 5))
    pushChunk(new TextEncoder().encode(full.slice(20)))
    await new Promise((r) => setTimeout(r, 10))

    expect(received).toHaveLength(1)
    expect(received[0]).toEqual({ jsonrpc: '2.0', id: 1, result: 'ok' })

    closeStream()
    await transport.close()
  })

  it('skips non-JSON lines', async () => {
    const { shell, pushChunk, closeStream } = createMockShell()
    const transport = new StdioTransport(shell, 'node', ['server.js'])

    const received: JSONRPCMessage[] = []
    transport.onMessage((msg) => received.push(msg))

    await transport.start()

    pushChunk(
      new TextEncoder().encode('Server starting...\n{"jsonrpc":"2.0","id":1,"result":true}\n')
    )
    await new Promise((r) => setTimeout(r, 10))

    expect(received).toHaveLength(1)

    closeStream()
    await transport.close()
  })

  it('throws if transport not started', async () => {
    const { shell } = createMockShell()
    const transport = new StdioTransport(shell, 'node', ['server.js'])

    await expect(transport.send({ jsonrpc: '2.0', id: 1, method: 'test' })).rejects.toThrow(
      'Transport not started'
    )
  })
})

// ---- SSETransport ----

describe('SSETransport', () => {
  it('sends POST requests to the message endpoint', async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      body: null,
    })
    vi.stubGlobal('fetch', fetchMock)

    const transport = new SSETransport('http://localhost:3000/sse')
    await transport.start()

    // Give SSE connect time to fire (it will fail but that's ok)
    await new Promise((r) => setTimeout(r, 10))

    const msg: JSONRPCMessage = { jsonrpc: '2.0', id: 1, method: 'test' }
    await transport.send(msg)

    // The second fetch call is the POST (first is the SSE GET)
    const postCall = fetchMock.mock.calls.find(
      (call: unknown[]) => (call[1] as { method?: string })?.method === 'POST'
    )
    expect(postCall).toBeDefined()
    expect(JSON.parse((postCall![1] as { body: string }).body)).toEqual(msg)

    await transport.close()
    vi.unstubAllGlobals()
  })

  it('closes cleanly via abort', async () => {
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, body: null })
    vi.stubGlobal('fetch', fetchMock)

    const transport = new SSETransport('http://localhost:3000/sse')
    await transport.start()
    await transport.close()

    // Should not throw after close
    vi.unstubAllGlobals()
  })
})
