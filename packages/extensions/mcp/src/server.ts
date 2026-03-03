import { createServer, type Server, type Socket } from 'node:net'
import { onEvent } from '@ava/core-v2/extensions'
import { createLogger } from '@ava/core-v2/logger'
import { createToolsListChangedNotification, handleMCPServerRequest } from './server-protocol.js'
import type {
  JSONRPCMessage,
  JSONRPCNotification,
  JSONRPCRequest,
  JSONRPCResponse,
} from './transport.js'

const log = createLogger('MCPServer')

export interface MCPServerModeConfig {
  enabled?: boolean
  stdio?: boolean
  unixSocketPath?: string
}

interface LineEndpoint {
  send: (msg: JSONRPCMessage) => void
  close: () => void
}

function encodeMessage(message: JSONRPCMessage): string {
  return `${JSON.stringify(message)}\n`
}

function processLine(line: string, endpoint: LineEndpoint): void {
  const trimmed = line.trim()
  if (!trimmed) return
  try {
    const message = JSON.parse(trimmed) as JSONRPCMessage
    if (!('id' in message) || !('method' in message)) return
    const request = message as JSONRPCRequest
    void handleMCPServerRequest(request, {
      sessionId: 'mcp-server',
      workingDirectory: process.cwd(),
      signal: AbortSignal.timeout(60_000),
    }).then((response) => endpoint.send(response))
  } catch {
    // Ignore malformed input
  }
}

function attachLineReader(onLine: (line: string) => void): (chunk: string) => void {
  let buffer = ''
  return (chunk: string) => {
    buffer += chunk
    const lines = buffer.split('\n')
    buffer = lines.pop() ?? ''
    for (const line of lines) onLine(line)
  }
}

export class MCPToolServer {
  private unixServer: Server | null = null
  private sockets = new Set<Socket>()
  private stdioCleanup: (() => void) | null = null
  private toolsChangedSub: { dispose: () => void } | null = null

  constructor(private config: MCPServerModeConfig) {}

  async start(): Promise<void> {
    if (this.config.stdio) {
      this.startStdioMode()
    }
    if (this.config.unixSocketPath) {
      await this.startUnixSocketMode(this.config.unixSocketPath)
    }

    this.toolsChangedSub = onEvent('tools:registered', () => this.broadcastToolsChanged())
    const unsub = onEvent('tools:unregistered', () => this.broadcastToolsChanged())
    const prev = this.toolsChangedSub
    this.toolsChangedSub = {
      dispose() {
        prev?.dispose()
        unsub.dispose()
      },
    }

    log.debug('MCP server mode started')
  }

  async stop(): Promise<void> {
    this.toolsChangedSub?.dispose()
    this.toolsChangedSub = null

    this.stdioCleanup?.()
    this.stdioCleanup = null

    for (const socket of this.sockets) {
      socket.destroy()
    }
    this.sockets.clear()

    if (this.unixServer) {
      await new Promise<void>((resolve) => this.unixServer?.close(() => resolve()))
      this.unixServer = null
    }
  }

  private broadcastToolsChanged(): void {
    const notification = createToolsListChangedNotification()
    const data = encodeMessage(notification)
    for (const socket of this.sockets) {
      if (!socket.destroyed) socket.write(data)
    }
    if (this.config.stdio) {
      process.stdout.write(data)
    }
  }

  private startStdioMode(): void {
    const onChunk = attachLineReader((line) =>
      processLine(line, {
        send(msg: JSONRPCMessage) {
          process.stdout.write(encodeMessage(msg))
        },
        close() {},
      })
    )

    const onData = (chunk: Buffer | string) => onChunk(chunk.toString())
    process.stdin.on('data', onData)
    this.stdioCleanup = () => process.stdin.off('data', onData)
  }

  private async startUnixSocketMode(socketPath: string): Promise<void> {
    this.unixServer = createServer((socket) => {
      this.sockets.add(socket)
      const onChunk = attachLineReader((line) =>
        processLine(line, {
          send(msg: JSONRPCMessage) {
            socket.write(encodeMessage(msg))
          },
          close() {
            socket.end()
          },
        })
      )
      socket.on('data', (chunk) => onChunk(chunk.toString()))
      socket.on('close', () => {
        this.sockets.delete(socket)
      })
      socket.on('error', () => {
        this.sockets.delete(socket)
      })
    })

    await new Promise<void>((resolve, reject) => {
      this.unixServer?.once('error', reject)
      this.unixServer?.listen(socketPath, () => resolve())
    })
  }
}

export function isRequestMessage(message: JSONRPCMessage): message is JSONRPCRequest {
  return 'id' in message && 'method' in message
}

export function isResponseMessage(message: JSONRPCMessage): message is JSONRPCResponse {
  return 'id' in message && !('method' in message)
}

export function isNotificationMessage(message: JSONRPCMessage): message is JSONRPCNotification {
  return 'method' in message && !('id' in message)
}
