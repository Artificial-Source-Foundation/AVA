/**
 * MCP Streamable HTTP transport.
 *
 * Implements the MCP streamable HTTP spec:
 * - HTTP POST for client → server messages (JSON-RPC 2.0)
 * - SSE for server → client responses and notifications
 * - Session management via Mcp-Session-Id header
 * - Supports both single JSON responses and SSE streaming responses
 */

import type { JSONRPCMessage, MCPTransport } from './transport.js'

type MessageHandler = (message: JSONRPCMessage) => void

export interface HttpStreamConfig {
  url: string
  headers?: Record<string, string>
}

/**
 * Parse SSE text into individual events.
 * Each event is separated by a blank line (\n\n).
 * Returns [parsed events data[], remaining buffer].
 */
function parseSSEEvents(buffer: string): [JSONRPCMessage[], string] {
  const messages: JSONRPCMessage[] = []
  const events = buffer.split('\n\n')
  const remaining = events.pop() ?? ''

  for (const event of events) {
    const dataMatch = event.match(/^data:\s*(.+)$/m)
    if (!dataMatch) continue

    try {
      const message = JSON.parse(dataMatch[1]!) as JSONRPCMessage
      messages.push(message)
    } catch {
      // Skip non-JSON data
    }
  }

  return [messages, remaining]
}

export class HttpStreamTransport implements MCPTransport {
  private baseUrl: string
  private customHeaders: Record<string, string>
  private sessionId: string | null = null
  private handler: MessageHandler | null = null
  private errorHandler: ((error: Error) => void) | null = null
  private closeHandler: (() => void) | null = null
  private abortController: AbortController | null = null
  private connected = false

  constructor(config: HttpStreamConfig) {
    this.baseUrl = config.url
    this.customHeaders = config.headers ?? {}
  }

  async start(): Promise<void> {
    this.abortController = new AbortController()
    this.connected = true
    // Open an initial SSE GET for server-initiated notifications
    this.listenForNotifications()
  }

  onMessage(handler: MessageHandler): void {
    this.handler = handler
  }

  onError(handler: (error: Error) => void): void {
    this.errorHandler = handler
  }

  onClose(handler: () => void): void {
    this.closeHandler = handler
  }

  async send(message: JSONRPCMessage): Promise<void> {
    if (!this.connected) {
      throw new Error('Transport not started')
    }

    const headers: Record<string, string> = {
      'Content-Type': 'application/json',
      Accept: 'application/json, text/event-stream',
      ...this.customHeaders,
    }

    if (this.sessionId) {
      headers['Mcp-Session-Id'] = this.sessionId
    }

    const response = await fetch(this.baseUrl, {
      method: 'POST',
      headers,
      body: JSON.stringify(message),
      signal: this.abortController?.signal,
    })

    if (!response.ok) {
      throw new Error(`HTTP POST failed (${response.status}): ${response.statusText}`)
    }

    // Capture session ID from response headers
    const newSessionId = response.headers.get('Mcp-Session-Id')
    if (newSessionId) {
      this.sessionId = newSessionId
    }

    const contentType = response.headers.get('Content-Type') ?? ''

    if (contentType.includes('text/event-stream')) {
      // Response is an SSE stream — pipe to message handler
      await this.readSSEResponse(response)
    } else if (contentType.includes('application/json')) {
      // Single JSON response
      const body = (await response.json()) as JSONRPCMessage
      this.handler?.(body)
    }
    // 202 Accepted or empty body — notification was accepted, no response
  }

  async close(): Promise<void> {
    this.connected = false
    this.abortController?.abort()
    this.abortController = null
    this.sessionId = null
  }

  /** The current session ID, if any. */
  getSessionId(): string | null {
    return this.sessionId
  }

  /**
   * Open a long-lived GET request with Accept: text/event-stream
   * for server-initiated notifications.
   */
  private async listenForNotifications(): Promise<void> {
    try {
      const headers: Record<string, string> = {
        Accept: 'text/event-stream',
        ...this.customHeaders,
      }
      if (this.sessionId) {
        headers['Mcp-Session-Id'] = this.sessionId
      }

      const response = await fetch(this.baseUrl, {
        method: 'GET',
        headers,
        signal: this.abortController?.signal,
      })

      if (!response.ok || !response.body) return

      // Capture session ID
      const newSessionId = response.headers.get('Mcp-Session-Id')
      if (newSessionId) {
        this.sessionId = newSessionId
      }

      await this.readSSEStream(response)
    } catch (err) {
      if (err instanceof DOMException && err.name === 'AbortError') return
      this.errorHandler?.(err instanceof Error ? err : new Error(String(err)))
    } finally {
      if (this.connected) {
        this.closeHandler?.()
      }
    }
  }

  /** Read an SSE response from a POST (contains responses to our request). */
  private async readSSEResponse(response: Response): Promise<void> {
    if (!response.body) return

    const reader = response.body.getReader()
    const decoder = new TextDecoder()
    let buffer = ''

    try {
      while (true) {
        const { done, value } = await reader.read()
        if (done) break

        buffer += decoder.decode(value, { stream: true })
        const [messages, remaining] = parseSSEEvents(buffer)
        buffer = remaining

        for (const msg of messages) {
          this.handler?.(msg)
        }
      }

      // Process any remaining data in buffer
      if (buffer.trim()) {
        const [messages] = parseSSEEvents(`${buffer}\n\n`)
        for (const msg of messages) {
          this.handler?.(msg)
        }
      }
    } finally {
      reader.releaseLock()
    }
  }

  /** Read a long-lived SSE stream (GET for notifications). */
  private async readSSEStream(response: Response): Promise<void> {
    if (!response.body) return

    const reader = response.body.getReader()
    const decoder = new TextDecoder()
    let buffer = ''

    try {
      while (true) {
        const { done, value } = await reader.read()
        if (done) break

        buffer += decoder.decode(value, { stream: true })
        const [messages, remaining] = parseSSEEvents(buffer)
        buffer = remaining

        for (const msg of messages) {
          this.handler?.(msg)
        }
      }
    } catch (err) {
      if (err instanceof DOMException && err.name === 'AbortError') return
      throw err
    } finally {
      reader.releaseLock()
    }
  }
}
