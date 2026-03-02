/**
 * LSP transport — Content-Length header framing over stdio.
 *
 * LSP uses "Content-Length: N\r\n\r\n" header framing (NOT newline-delimited JSON).
 * The transport reads N bytes after the header to get the complete message.
 */

import type { ChildProcess, IShell, SpawnOptions } from '@ava/core-v2/platform'

export interface LSPMessage {
  jsonrpc: '2.0'
  id?: number
  method?: string
  params?: unknown
  result?: unknown
  error?: { code: number; message: string; data?: unknown }
}

type MessageHandler = (message: LSPMessage) => void

export class LSPTransport {
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
      throw new Error('Failed to get stdio streams from LSP server')
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

  async send(message: LSPMessage): Promise<void> {
    if (!this.process?.stdin) {
      throw new Error('Transport not started')
    }
    const body = JSON.stringify(message)
    const bodyBytes =
      typeof Buffer !== 'undefined'
        ? Buffer.byteLength(body)
        : new TextEncoder().encode(body).byteLength
    const header = `Content-Length: ${bodyBytes}\r\n\r\n`
    const data = header + body

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

        // Parse Content-Length framed messages
        while (true) {
          const headerEnd = buffer.indexOf('\r\n\r\n')
          if (headerEnd === -1) break

          const header = buffer.slice(0, headerEnd)
          const match = header.match(/Content-Length:\s*(\d+)/i)
          if (!match) {
            // Skip malformed header
            buffer = buffer.slice(headerEnd + 4)
            continue
          }

          const contentLength = parseInt(match[1]!, 10)
          const bodyStart = headerEnd + 4
          const bodyEnd = bodyStart + contentLength

          // Wait for full body
          if (buffer.length < bodyEnd) break

          const body = buffer.slice(bodyStart, bodyEnd)
          buffer = buffer.slice(bodyEnd)

          try {
            const message = JSON.parse(body) as LSPMessage
            this.handler?.(message)
          } catch {
            // Skip malformed JSON
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
