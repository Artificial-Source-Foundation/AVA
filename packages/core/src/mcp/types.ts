/**
 * MCP (Model Context Protocol) Types
 * Based on @modelcontextprotocol/sdk and Gemini CLI patterns
 */

import type { Tool as MCPSDKTool } from '@modelcontextprotocol/sdk/types.js'

// ============================================================================
// Server Configuration
// ============================================================================

/**
 * Transport type for MCP server
 */
export type MCPTransportType = 'stdio' | 'sse' | 'http'

/**
 * MCP Server configuration
 */
export interface MCPServerConfig {
  /** Human-readable name for the server */
  name: string

  /** Transport type */
  type: MCPTransportType

  /**
   * For stdio transport: command to run
   * For HTTP/SSE: URL to connect to
   */
  command?: string

  /** Command arguments (for stdio) */
  args?: string[]

  /** URL for HTTP/SSE transports */
  url?: string

  /** Working directory (for stdio) */
  cwd?: string

  /** Environment variables (for stdio) */
  env?: Record<string, string>

  /** Custom headers (for HTTP/SSE) */
  headers?: Record<string, string>

  /** Connection timeout in ms (default: 30000) */
  timeout?: number

  /** Tools to include (whitelist) */
  includeTools?: string[]

  /** Tools to exclude (blacklist) */
  excludeTools?: string[]

  /** Trust level for this server */
  trust?: 'full' | 'sandbox' | 'none'
}

// ============================================================================
// Server Status
// ============================================================================

/**
 * Connection status of an MCP server
 */
export enum MCPServerStatus {
  /** Server is disconnected */
  DISCONNECTED = 'disconnected',
  /** Server is disconnecting */
  DISCONNECTING = 'disconnecting',
  /** Server is connecting */
  CONNECTING = 'connecting',
  /** Server is connected and ready */
  CONNECTED = 'connected',
}

/**
 * Overall MCP discovery state
 */
export enum MCPDiscoveryState {
  /** Discovery has not started */
  NOT_STARTED = 'not_started',
  /** Discovery is in progress */
  IN_PROGRESS = 'in_progress',
  /** Discovery completed */
  COMPLETED = 'completed',
}

// ============================================================================
// Tool Types
// ============================================================================

/**
 * MCP tool definition (from SDK)
 */
export type MCPTool = MCPSDKTool

/**
 * Discovered MCP tool with server context
 */
export interface DiscoveredMCPTool {
  /** Server this tool belongs to */
  serverName: string

  /** Original tool name from MCP */
  originalName: string

  /** Normalized name (mcp_{server}_{tool}) */
  name: string

  /** Tool description */
  description: string

  /** JSON Schema for input parameters */
  inputSchema: Record<string, unknown>

  /** Execute the tool */
  execute: (params: Record<string, unknown>) => Promise<MCPToolResult>
}

/**
 * Result from MCP tool execution
 */
export interface MCPToolResult {
  /** Content returned by the tool */
  content: MCPToolContent[]

  /** Whether this is an error result */
  isError?: boolean
}

/**
 * Content item in MCP tool result
 */
export interface MCPToolContent {
  type: 'text' | 'image' | 'resource'
  text?: string
  data?: string
  mimeType?: string
  uri?: string
}

// ============================================================================
// Events
// ============================================================================

/**
 * MCP event types
 */
export type MCPEventType =
  | 'server:connecting'
  | 'server:connected'
  | 'server:disconnected'
  | 'server:error'
  | 'tools:updated'
  | 'discovery:started'
  | 'discovery:completed'

/**
 * MCP event payload
 */
export interface MCPEvent {
  type: MCPEventType
  serverName?: string
  error?: Error
  tools?: DiscoveredMCPTool[]
}

/**
 * MCP event listener
 */
export type MCPEventListener = (event: MCPEvent) => void
