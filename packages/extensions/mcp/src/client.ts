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
  JSONRPCResponse,
  MCPTransport,
} from './transport.js'
import type {
  MCPPrompt,
  MCPPromptMessage,
  MCPResource,
  MCPResourceContents,
  MCPSamplingRequest,
  MCPSamplingResult,
} from './types.js'

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

export type SamplingHandler = (request: MCPSamplingRequest) => Promise<MCPSamplingResult>

export class MCPClient {
  private nextId = 1
  private pending = new Map<number, PendingRequest>()
  private initialized = false
  private timeoutMs: number
  private samplingHandler: SamplingHandler | null = null
  private capabilities: ServerCapabilities = {}

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
      capabilities: { sampling: {} },
      clientInfo: { name: 'ava', version: '1.0.0' },
    })) as { capabilities: ServerCapabilities; protocolVersion: string }

    // Send initialized notification
    await this.notify('notifications/initialized')
    this.initialized = true
    this.capabilities = result.capabilities
    return result.capabilities
  }

  get serverCapabilities(): ServerCapabilities {
    return this.capabilities
  }

  // ─── Tools ────────────────────────────────────────────────────────────

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

  // ─── Resources ────────────────────────────────────────────────────────

  async listResources(): Promise<MCPResource[]> {
    this.assertInitialized()
    const result = (await this.request('resources/list')) as { resources: MCPResource[] }
    return result.resources ?? []
  }

  async readResource(uri: string): Promise<MCPResourceContents[]> {
    this.assertInitialized()
    const result = (await this.request('resources/read', { uri })) as {
      contents: MCPResourceContents[]
    }
    return result.contents ?? []
  }

  // ─── Prompts ──────────────────────────────────────────────────────────

  async listPrompts(): Promise<MCPPrompt[]> {
    this.assertInitialized()
    const result = (await this.request('prompts/list')) as { prompts: MCPPrompt[] }
    return result.prompts ?? []
  }

  async getPrompt(
    name: string,
    args?: Record<string, string>
  ): Promise<{ description?: string; messages: MCPPromptMessage[] }> {
    this.assertInitialized()
    const result = (await this.request('prompts/get', { name, arguments: args })) as {
      description?: string
      messages: MCPPromptMessage[]
    }
    return result
  }

  // ─── Sampling ─────────────────────────────────────────────────────────

  /** Set the handler for server-initiated sampling requests. */
  onSamplingRequest(handler: SamplingHandler): void {
    this.samplingHandler = handler
  }

  // ─── Lifecycle ────────────────────────────────────────────────────────

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
    // Server-initiated request (e.g. sampling/createMessage)
    if ('method' in message && 'id' in message) {
      void this.handleServerRequest(message as JSONRPCRequest)
      return
    }

    // Response to our request (has id, no method)
    if ('id' in message && !('method' in message)) {
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

    // Notifications (has method, no id) — ignore for now
  }

  private async handleServerRequest(request: JSONRPCRequest): Promise<void> {
    if (request.method === 'sampling/createMessage' && this.samplingHandler) {
      try {
        const result = await this.samplingHandler(request.params as unknown as MCPSamplingRequest)
        const response: JSONRPCResponse = {
          jsonrpc: '2.0',
          id: request.id,
          result: result as unknown,
        }
        await this.transport.send(response)
      } catch (err) {
        const response: JSONRPCResponse = {
          jsonrpc: '2.0',
          id: request.id,
          error: { code: -32000, message: err instanceof Error ? err.message : String(err) },
        }
        await this.transport.send(response)
      }
    } else {
      // Unsupported server request
      const response: JSONRPCResponse = {
        jsonrpc: '2.0',
        id: request.id,
        error: { code: -32601, message: `Method not found: ${request.method}` },
      }
      await this.transport.send(response)
    }
  }
}
