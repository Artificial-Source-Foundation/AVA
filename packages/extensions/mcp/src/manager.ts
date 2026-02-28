/**
 * MCP connection manager.
 *
 * Manages connection lifecycle: connect → initialize → list tools → ready.
 * Wraps transport + client into a simple API for the extension.
 */

import type { IShell } from '@ava/core-v2/platform'
import { MCPClient } from './client.js'
import type { MCPTransport } from './transport.js'
import { SSETransport, StdioTransport } from './transport.js'
import type { MCPConnectionState, MCPServer, MCPTool } from './types.js'

interface ActiveConnection {
  state: MCPConnectionState
  client: MCPClient
  transport: MCPTransport
}

const connections = new Map<string, ActiveConnection>()

export function getConnections(): ReadonlyMap<string, MCPConnectionState> {
  const states = new Map<string, MCPConnectionState>()
  for (const [name, conn] of connections) {
    states.set(name, conn.state)
  }
  return states
}

export async function connectServer(server: MCPServer, shell: IShell): Promise<MCPTool[]> {
  // Disconnect existing connection with same name
  if (connections.has(server.name)) {
    await disconnectServer(server.name)
  }

  const state: MCPConnectionState = { server, status: 'connecting', tools: [] }
  let transport: MCPTransport

  if (server.transport === 'stdio') {
    if (!server.command) {
      throw new Error(`Stdio server "${server.name}" requires a command`)
    }
    transport = new StdioTransport(shell, server.command, server.args ?? [], { env: server.env })
  } else {
    transport = new SSETransport(server.uri)
  }

  const client = new MCPClient(transport)
  connections.set(server.name, { state, client, transport })

  try {
    await transport.start()
    await client.initialize()
    const toolDefs = await client.listTools()

    const tools: MCPTool[] = toolDefs.map((t) => ({
      name: t.name,
      description: t.description,
      inputSchema: t.inputSchema,
      serverName: server.name,
    }))

    state.status = 'connected'
    state.tools = tools
    return tools
  } catch (err) {
    state.status = 'error'
    state.error = err instanceof Error ? err.message : String(err)
    // Clean up failed connection
    try {
      await client.close()
    } catch {
      // Ignore cleanup errors
    }
    connections.delete(server.name)
    throw err
  }
}

export async function disconnectServer(name: string): Promise<void> {
  const conn = connections.get(name)
  if (!conn) return

  try {
    await conn.client.close()
  } catch {
    // Ignore close errors
  }
  connections.delete(name)
}

export async function callTool(
  serverName: string,
  toolName: string,
  args: Record<string, unknown>
): Promise<{ success: boolean; output: string }> {
  const conn = connections.get(serverName)
  if (!conn) {
    return { success: false, output: `MCP server "${serverName}" is not connected` }
  }

  try {
    const result = await conn.client.callTool(toolName, args)
    const textParts = result.content
      .filter((c) => c.type === 'text' && c.text)
      .map((c) => c.text as string)

    return {
      success: !result.isError,
      output:
        textParts.join('\n') ||
        (result.isError ? 'Tool returned an error' : 'Tool executed successfully'),
    }
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    return { success: false, output: `MCP tool call failed: ${message}` }
  }
}

export function getTools(): MCPTool[] {
  const tools: MCPTool[] = []
  for (const conn of connections.values()) {
    if (conn.state.status === 'connected') {
      tools.push(...conn.state.tools)
    }
  }
  return tools
}

export async function resetMCP(): Promise<void> {
  const names = [...connections.keys()]
  for (const name of names) {
    await disconnectServer(name)
  }
}
