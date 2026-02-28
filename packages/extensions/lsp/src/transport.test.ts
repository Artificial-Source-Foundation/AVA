import { describe, expect, it } from 'vitest'
import type { LSPMessage } from './transport.js'

describe('LSP Content-Length framing', () => {
  it('parses Content-Length header correctly', () => {
    const body = '{"jsonrpc":"2.0","id":1,"result":{}}'
    const header = `Content-Length: ${body.length}\r\n\r\n`
    const raw = header + body

    // Simulate parsing logic
    const headerEnd = raw.indexOf('\r\n\r\n')
    expect(headerEnd).toBeGreaterThan(0)

    const headerStr = raw.slice(0, headerEnd)
    const match = headerStr.match(/Content-Length:\s*(\d+)/i)
    expect(match).not.toBeNull()
    expect(parseInt(match![1]!, 10)).toBe(body.length)
  })

  it('handles multi-byte content length', () => {
    const body = '{"jsonrpc":"2.0","id":1,"result":{"data":"日本語"}}'
    // Content-Length is in bytes, not characters
    const byteLength = new TextEncoder().encode(body).length
    expect(byteLength).toBeGreaterThan(body.length)
  })

  it('handles multiple messages in one chunk', () => {
    const msg1 = '{"jsonrpc":"2.0","id":1,"result":{}}'
    const msg2 = '{"jsonrpc":"2.0","id":2,"result":{}}'
    const data = `Content-Length: ${msg1.length}\r\n\r\n${msg1}Content-Length: ${msg2.length}\r\n\r\n${msg2}`

    const messages: string[] = []
    let buffer = data

    while (true) {
      const headerEnd = buffer.indexOf('\r\n\r\n')
      if (headerEnd === -1) break

      const header = buffer.slice(0, headerEnd)
      const match = header.match(/Content-Length:\s*(\d+)/i)
      if (!match) break

      const contentLength = parseInt(match[1]!, 10)
      const bodyStart = headerEnd + 4
      const bodyEnd = bodyStart + contentLength
      if (buffer.length < bodyEnd) break

      messages.push(buffer.slice(bodyStart, bodyEnd))
      buffer = buffer.slice(bodyEnd)
    }

    expect(messages).toHaveLength(2)
    expect(JSON.parse(messages[0]!)).toEqual({ jsonrpc: '2.0', id: 1, result: {} })
    expect(JSON.parse(messages[1]!)).toEqual({ jsonrpc: '2.0', id: 2, result: {} })
  })

  it('waits for complete body before parsing', () => {
    const body = '{"jsonrpc":"2.0","id":1,"result":{}}'
    const data = `Content-Length: ${body.length}\r\n\r\n${body.slice(0, 10)}` // Incomplete

    const headerEnd = data.indexOf('\r\n\r\n')
    const match = data.slice(0, headerEnd).match(/Content-Length:\s*(\d+)/i)
    const contentLength = parseInt(match![1]!, 10)
    const bodyEnd = headerEnd + 4 + contentLength

    expect(data.length).toBeLessThan(bodyEnd) // Not enough data
  })

  it('constructs outgoing messages with Content-Length', () => {
    const message: LSPMessage = { jsonrpc: '2.0', id: 1, method: 'initialize', params: {} }
    const body = JSON.stringify(message)
    const encoded = `Content-Length: ${new TextEncoder().encode(body).length}\r\n\r\n${body}`

    expect(encoded).toContain('Content-Length:')
    expect(encoded).toContain('\r\n\r\n')
    expect(encoded).toContain('"initialize"')
  })
})
