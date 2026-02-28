/**
 * MCP transport layer — stdio and SSE transports.
 *
 * Handles JSON-RPC 2.0 message framing over stdio (newline-delimited JSON)
 * and SSE (Server-Sent Events with POST for sending).
 */

import type { ChildProcess, IShell, SpawnOptions } from '@ava/core-v2/platform'

// ---- JSON-RPC 2.0 types ----

export interface JSONRPCRequest {
  jsonrpc: '2.0'
  id: number
  method: string
  params?: Record<string, unknown>
}

export interface JSONRPCResponse {
  jsonrpc: '2.0'
  id: number
  result?: unknown
  error?: { code: number; message: string; data?: unknown }
}

export interface JSONRPCNotification {
  jsonrpc: '2.0'
  method: string
  params?: Record<string, unknown>
}

export type JSONRPCMessage = JSONRPCRequest | JSONRPCResponse | JSONRPCNotification

type MessageHandler = (message: JSONRPCMessage) => void

// ---- Transport interface ----

export interface MCPTransport {
  start(): Promise<void>
  send(message: JSONRPCMessage): Promise<void>
  onMessage(handler: MessageHandler): void
  onError?(handler: (error: Error) => void): void
  onClose?(handler: () => void): void
  close(): Promise<void>
}

// ---- Stdio transport ----

export class StdioTransport implements MCPTransport {
  private process: ChildProcess | null = null
  private handler: MessageHandler | null = null
  private errorHandler: ((error: Error) => void) | null = null
  private closeHandler: (() => void) | null = null
  private reading = false

  constructor(
    private shell: IShell,
    private command: string,
    private args: string[],
    private options?: SpawnOptions
  ) {}

  async start(): Promise<void> {
    this.process = this.shell.spawn(this.command, this.args, this.options)
    if (!this.process.stdout || !this.process.stdin) {
      throw new Error('Failed to get stdio streams from spawned process')
    }
    this.reading = true
    this.readLoop()
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
    if (!this.process?.stdin) {
      throw new Error('Transport not started')
    }
    const data = `${JSON.stringify(message)}\n`
    const writer = this.process.stdin.getWriter()
    try {
      await writer.write(new TextEncoder().encode(data))
    } finally {
      writer.releaseLock()
    }
  }

  async close(): Promise<void> {
    this.reading = false
    if (this.process) {
      this.process.kill()
      this.process = null
    }
  }

  private async readLoop(): Promise<void> {
    if (!this.process?.stdout) return
    const reader = this.process.stdout.getReader()
    const decoder = new TextDecoder()
    let buffer = ''

    try {
      while (this.reading) {
        const { done, value } = await reader.read()
        if (done) break

        buffer += decoder.decode(value, { stream: true })
        const lines = buffer.split('\n')
        buffer = lines.pop() ?? ''

        for (const line of lines) {
          const trimmed = line.trim()
          if (!trimmed) continue
          try {
            const message = JSON.parse(trimmed) as JSONRPCMessage
            this.handler?.(message)
          } catch {
            // Skip non-JSON lines (e.g. server startup output)
          }
        }
      }
    } catch (err) {
      this.errorHandler?.(err instanceof Error ? err : new Error(String(err)))
    } finally {
      reader.releaseLock()
      this.closeHandler?.()
    }
  }
}

// ---- SSE transport ----

export class SSETransport implements MCPTransport {
  private handler: MessageHandler | null = null
  private errorHandler: ((error: Error) => void) | null = null
  private closeHandler: (() => void) | null = null
  private abortController: AbortController | null = null
  private messageEndpoint: string

  constructor(private url: string) {
    this.messageEndpoint = url
  }

  async start(): Promise<void> {
    this.abortController = new AbortController()
    this.readSSE()
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
    const response = await fetch(this.messageEndpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(message),
      signal: this.abortController?.signal,
    })
    if (!response.ok) {
      throw new Error(`SSE POST failed (${response.status})`)
    }
  }

  async close(): Promise<void> {
    this.abortController?.abort()
    this.abortController = null
  }

  private async readSSE(): Promise<void> {
    try {
      const response = await fetch(this.url, {
        headers: { Accept: 'text/event-stream' },
        signal: this.abortController?.signal,
      })

      if (!response.ok || !response.body) return

      const reader = response.body.getReader()
      const decoder = new TextDecoder()
      let buffer = ''

      while (true) {
        const { done, value } = await reader.read()
        if (done) break

        buffer += decoder.decode(value, { stream: true })
        const events = buffer.split('\n\n')
        buffer = events.pop() ?? ''

        for (const event of events) {
          // Check for endpoint event (SSE spec: server tells client where to POST)
          const endpointMatch = event.match(/^event:\s*endpoint\ndata:\s*(.+)$/m)
          if (endpointMatch?.[1]) {
            const endpoint = endpointMatch[1].trim()
            // Resolve relative URLs against the base
            this.messageEndpoint = new URL(endpoint, this.url).href
            continue
          }

          // Parse data lines
          const dataMatch = event.match(/^data:\s*(.+)$/m)
          if (!dataMatch) continue

          try {
            const message = JSON.parse(dataMatch[1]!) as JSONRPCMessage
            this.handler?.(message)
          } catch {
            // Skip non-JSON data
          }
        }
      }
    } catch (err) {
      if (err instanceof DOMException && err.name === 'AbortError') return
      this.errorHandler?.(err instanceof Error ? err : new Error(String(err)))
    } finally {
      this.closeHandler?.()
    }
  }
}
