/**
 * MCP client types.
 */

export interface MCPServer {
  name: string
  uri: string
  transport: 'stdio' | 'sse'
  command?: string
  args?: string[]
  env?: Record<string, string>
}

export interface MCPTool {
  name: string
  description: string
  inputSchema: Record<string, unknown>
  serverName: string
}

export interface MCPConnectionState {
  server: MCPServer
  status: 'connecting' | 'connected' | 'disconnected' | 'error'
  tools: MCPTool[]
  error?: string
}
