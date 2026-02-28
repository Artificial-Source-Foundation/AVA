/**
 * MCP protocol client — handles the MCP handshake and tool operations.
 *
 * Implements initialize, tools/list, and tools/call over a transport.
 * Uses JSON-RPC 2.0 request/response correlation with timeouts.
 */

import type {
  JSONRPCMessage,
  JSONRPCNotification,
  JSONRPCRequest,
  MCPTransport,
} from './transport.js'

const PROTOCOL_VERSION = '2024-11-05'
const DEFAULT_TIMEOUT_MS = 30_000

interface PendingRequest {
  resolve: (result: unknown) => void
  reject: (error: Error) => void
  timer: ReturnType<typeof setTimeout>
}

export interface ServerCapabilities {
  tools?: Record<string, unknown>
  resources?: Record<string, unknown>
  prompts?: Record<string, unknown>
  [key: string]: unknown
}

export interface MCPToolDefinition {
  name: string
  description: string
  inputSchema: Record<string, unknown>
}

export interface MCPToolResult {
  content: Array<{ type: string; text?: string; [key: string]: unknown }>
  isError?: boolean
}

export class MCPClient {
  private nextId = 1
  private pending = new Map<number, PendingRequest>()
  private initialized = false
  private timeoutMs: number

  constructor(
    private transport: MCPTransport,
    options?: { timeoutMs?: number }
  ) {
    this.timeoutMs = options?.timeoutMs ?? DEFAULT_TIMEOUT_MS
    this.transport.onMessage((msg) => this.handleMessage(msg))
  }

  async initialize(): Promise<ServerCapabilities> {
    const result = (await this.request('initialize', {
      protocolVersion: PROTOCOL_VERSION,
      capabilities: {},
      clientInfo: { name: 'ava', version: '1.0.0' },
    })) as { capabilities: ServerCapabilities; protocolVersion: string }

    // Send initialized notification
    await this.notify('notifications/initialized')
    this.initialized = true
    return result.capabilities
  }

  async listTools(): Promise<MCPToolDefinition[]> {
    this.assertInitialized()
    const result = (await this.request('tools/list')) as { tools: MCPToolDefinition[] }
    return result.tools ?? []
  }

  async callTool(name: string, args: Record<string, unknown>): Promise<MCPToolResult> {
    this.assertInitialized()
    const result = (await this.request('tools/call', { name, arguments: args })) as MCPToolResult
    return result
  }

  async close(): Promise<void> {
    // Reject all pending requests
    for (const [id, pending] of this.pending) {
      clearTimeout(pending.timer)
      pending.reject(new Error('Client closed'))
      this.pending.delete(id)
    }
    this.initialized = false
    await this.transport.close()
  }

  private assertInitialized(): void {
    if (!this.initialized) {
      throw new Error('Client not initialized. Call initialize() first.')
    }
  }

  private async request(method: string, params?: Record<string, unknown>): Promise<unknown> {
    const id = this.nextId++
    const message: JSONRPCRequest = { jsonrpc: '2.0', id, method, params }

    return new Promise<unknown>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id)
        reject(new Error(`Request timed out: ${method} (${this.timeoutMs}ms)`))
      }, this.timeoutMs)

      this.pending.set(id, { resolve, reject, timer })
      this.transport.send(message).catch((err) => {
        clearTimeout(timer)
        this.pending.delete(id)
        reject(err)
      })
    })
  }

  private async notify(method: string, params?: Record<string, unknown>): Promise<void> {
    const message: JSONRPCNotification = { jsonrpc: '2.0', method, params }
    await this.transport.send(message)
  }

  private handleMessage(message: JSONRPCMessage): void {
    // Only handle responses (messages with id and no method)
    if (!('id' in message) || 'method' in message) return

    const pending = this.pending.get(message.id)
    if (!pending) return

    clearTimeout(pending.timer)
    this.pending.delete(message.id)

    if (message.error) {
      pending.reject(new Error(`MCP error (${message.error.code}): ${message.error.message}`))
    } else {
      pending.resolve(message.result)
    }
  }
}
