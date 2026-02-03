/**
 * MCP Client
 * Manages connections to MCP servers using the official SDK
 *
 * Based on Gemini CLI's mcp-client.ts pattern
 */

import { Client } from '@modelcontextprotocol/sdk/client/index.js'
import { SSEClientTransport } from '@modelcontextprotocol/sdk/client/sse.js'
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js'
import { StreamableHTTPClientTransport } from '@modelcontextprotocol/sdk/client/streamableHttp.js'
import type { Transport } from '@modelcontextprotocol/sdk/shared/transport.js'

import type {
  DiscoveredMCPTool,
  MCPEvent,
  MCPEventListener,
  MCPServerConfig,
  MCPToolContent,
  MCPToolResult,
} from './types.js'
import { MCPDiscoveryState, MCPServerStatus } from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Default timeout for MCP operations (10 minutes like Gemini CLI) */
const DEFAULT_TIMEOUT_MS = 10 * 60 * 1000

// ============================================================================
// Server State
// ============================================================================

interface ServerState {
  config: MCPServerConfig
  client: Client
  transport: Transport
  status: MCPServerStatus
  tools: DiscoveredMCPTool[]
}

// ============================================================================
// MCP Client Manager
// ============================================================================

/**
 * Manages connections to multiple MCP servers
 */
export class MCPClientManager {
  private servers = new Map<string, ServerState>()
  private listeners: MCPEventListener[] = []
  private discoveryState: MCPDiscoveryState = MCPDiscoveryState.NOT_STARTED

  /** Client name and version for MCP protocol */
  private readonly clientName: string
  private readonly clientVersion: string

  constructor(clientName = 'estela', clientVersion = '0.1.0') {
    this.clientName = clientName
    this.clientVersion = clientVersion
  }

  // ==========================================================================
  // Connection Management
  // ==========================================================================

  /**
   * Connect to an MCP server
   */
  async connect(config: MCPServerConfig): Promise<void> {
    const serverName = config.name

    // Check if already connected
    if (this.servers.has(serverName)) {
      const existing = this.servers.get(serverName)!
      if (existing.status === 'connected') {
        return
      }
      // Clean up existing connection
      await this.disconnect(serverName)
    }

    this.emit({ type: 'server:connecting', serverName })

    try {
      // Create MCP client
      const client = new Client(
        { name: this.clientName, version: this.clientVersion },
        { capabilities: {} }
      )

      // Create transport
      const transport = await this.createTransport(config)

      // Connect
      await client.connect(transport, {
        timeout: config.timeout ?? DEFAULT_TIMEOUT_MS,
      })

      // Store server state
      this.servers.set(serverName, {
        config,
        client,
        transport,
        status: MCPServerStatus.CONNECTED,
        tools: [],
      })

      this.emit({ type: 'server:connected', serverName })
    } catch (error) {
      this.emit({
        type: 'server:error',
        serverName,
        error: error instanceof Error ? error : new Error(String(error)),
      })
      throw error
    }
  }

  /**
   * Disconnect from an MCP server
   */
  async disconnect(serverName: string): Promise<void> {
    const server = this.servers.get(serverName)
    if (!server) return

    try {
      await server.transport.close()
      await server.client.close()
    } catch {
      // Ignore errors during cleanup
    }

    this.servers.delete(serverName)
    this.emit({ type: 'server:disconnected', serverName })
  }

  /**
   * Disconnect from all servers
   */
  async disconnectAll(): Promise<void> {
    const names = Array.from(this.servers.keys())
    await Promise.all(names.map((name) => this.disconnect(name)))
  }

  // ==========================================================================
  // Tool Discovery
  // ==========================================================================

  /**
   * Discover tools from a connected server
   */
  async discoverTools(serverName: string): Promise<DiscoveredMCPTool[]> {
    const server = this.servers.get(serverName)
    if (!server) {
      throw new Error(`Server '${serverName}' is not connected`)
    }

    const { client, config } = server

    // Check if server supports tools
    const capabilities = client.getServerCapabilities()
    if (!capabilities?.tools) {
      return []
    }

    // List tools from server
    const response = await client.listTools()
    const tools: DiscoveredMCPTool[] = []

    for (const toolDef of response.tools) {
      // Apply include/exclude filters
      if (!this.isToolEnabled(toolDef.name, config)) {
        continue
      }

      // Create discovered tool
      const tool: DiscoveredMCPTool = {
        serverName,
        originalName: toolDef.name,
        name: `mcp_${serverName}_${toolDef.name}`,
        description: toolDef.description ?? '',
        inputSchema: (toolDef.inputSchema as Record<string, unknown>) ?? {
          type: 'object',
          properties: {},
        },
        execute: async (params) => this.callTool(serverName, toolDef.name, params),
      }

      tools.push(tool)
    }

    // Update server state
    server.tools = tools

    this.emit({ type: 'tools:updated', serverName, tools })
    return tools
  }

  /**
   * Discover tools from all connected servers
   */
  async discoverAllTools(): Promise<DiscoveredMCPTool[]> {
    this.discoveryState = MCPDiscoveryState.IN_PROGRESS
    this.emit({ type: 'discovery:started' })

    const allTools: DiscoveredMCPTool[] = []

    for (const [serverName] of this.servers) {
      try {
        const tools = await this.discoverTools(serverName)
        allTools.push(...tools)
      } catch {
        // Continue with other servers on error
      }
    }

    this.discoveryState = MCPDiscoveryState.COMPLETED
    this.emit({ type: 'discovery:completed', tools: allTools })

    return allTools
  }

  // ==========================================================================
  // Tool Execution
  // ==========================================================================

  /**
   * Call a tool on an MCP server
   */
  async callTool(
    serverName: string,
    toolName: string,
    params: Record<string, unknown>
  ): Promise<MCPToolResult> {
    const server = this.servers.get(serverName)
    if (!server) {
      throw new Error(`Server '${serverName}' is not connected`)
    }

    const result = await server.client.callTool({ name: toolName, arguments: params }, undefined, {
      timeout: server.config.timeout ?? DEFAULT_TIMEOUT_MS,
    })

    // Convert result to our format
    const content: MCPToolContent[] = []

    if (Array.isArray(result.content)) {
      for (const item of result.content) {
        if (item.type === 'text') {
          content.push({ type: 'text', text: item.text })
        } else if (item.type === 'image') {
          content.push({
            type: 'image',
            data: item.data,
            mimeType: item.mimeType,
          })
        } else if (item.type === 'resource') {
          content.push({
            type: 'resource',
            uri: (item as { uri?: string }).uri,
            text: (item as { text?: string }).text,
          })
        }
      }
    }

    return {
      content,
      isError: Boolean(result.isError),
    }
  }

  // ==========================================================================
  // State Queries
  // ==========================================================================

  /**
   * Get list of connected server names
   */
  getConnectedServers(): string[] {
    return Array.from(this.servers.keys())
  }

  /**
   * Get server status
   */
  getServerStatus(serverName: string): MCPServerStatus | undefined {
    return this.servers.get(serverName)?.status
  }

  /**
   * Get all discovered tools
   */
  getAllTools(): DiscoveredMCPTool[] {
    const tools: DiscoveredMCPTool[] = []
    for (const server of this.servers.values()) {
      tools.push(...server.tools)
    }
    return tools
  }

  /**
   * Get discovery state
   */
  getDiscoveryState(): MCPDiscoveryState {
    return this.discoveryState
  }

  // ==========================================================================
  // Events
  // ==========================================================================

  /**
   * Add event listener
   */
  addEventListener(listener: MCPEventListener): void {
    this.listeners.push(listener)
  }

  /**
   * Remove event listener
   */
  removeEventListener(listener: MCPEventListener): void {
    const index = this.listeners.indexOf(listener)
    if (index !== -1) {
      this.listeners.splice(index, 1)
    }
  }

  private emit(event: MCPEvent): void {
    for (const listener of this.listeners) {
      try {
        listener(event)
      } catch {
        // Ignore listener errors
      }
    }
  }

  // ==========================================================================
  // Private Helpers
  // ==========================================================================

  private async createTransport(config: MCPServerConfig): Promise<Transport> {
    switch (config.type) {
      case 'stdio': {
        if (!config.command) {
          throw new Error(`Stdio transport requires 'command' in config`)
        }
        // Build env, filtering out undefined values from process.env
        let env: Record<string, string> | undefined
        if (config.env) {
          env = {}
          for (const [key, value] of Object.entries(process.env)) {
            if (value !== undefined) {
              env[key] = value
            }
          }
          Object.assign(env, config.env)
        }
        return new StdioClientTransport({
          command: config.command,
          args: config.args ?? [],
          env,
          cwd: config.cwd,
        })
      }

      case 'sse':
        if (!config.url) {
          throw new Error(`SSE transport requires 'url' in config`)
        }
        return new SSEClientTransport(new URL(config.url), {
          requestInit: config.headers ? { headers: config.headers } : undefined,
        })

      case 'http':
        if (!config.url) {
          throw new Error(`HTTP transport requires 'url' in config`)
        }
        return new StreamableHTTPClientTransport(new URL(config.url), {
          requestInit: config.headers ? { headers: config.headers } : undefined,
        })

      default:
        throw new Error(`Unknown transport type: ${config.type}`)
    }
  }

  private isToolEnabled(toolName: string, config: MCPServerConfig): boolean {
    // Exclude takes precedence
    if (config.excludeTools?.includes(toolName)) {
      return false
    }

    // If include list exists, tool must be in it
    if (config.includeTools && !config.includeTools.includes(toolName)) {
      return false
    }

    return true
  }
}

/**
 * Create a new MCP client manager
 */
export function createMCPClient(clientName?: string, clientVersion?: string): MCPClientManager {
  return new MCPClientManager(clientName, clientVersion)
}
