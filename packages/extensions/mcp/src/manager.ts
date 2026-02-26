/**
 * MCP connection manager.
 * Manages connections to MCP servers and exposes their tools.
 */

import type { MCPConnectionState, MCPServer, MCPTool } from './types.js'

const connections = new Map<string, MCPConnectionState>()

export function getConnections(): ReadonlyMap<string, MCPConnectionState> {
  return connections
}

export function addServer(server: MCPServer): void {
  connections.set(server.name, {
    server,
    status: 'disconnected',
    tools: [],
  })
}

export function removeServer(name: string): void {
  connections.delete(name)
}

export function getTools(): MCPTool[] {
  const tools: MCPTool[] = []
  for (const conn of connections.values()) {
    if (conn.status === 'connected') {
      tools.push(...conn.tools)
    }
  }
  return tools
}

export function resetMCP(): void {
  connections.clear()
}
