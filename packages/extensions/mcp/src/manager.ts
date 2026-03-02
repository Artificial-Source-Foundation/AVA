/**
 * MCP connection manager.
 *
 * Manages connection lifecycle: connect → initialize → list tools → ready.
 * Supports reconnection, resources, and prompts.
 */

import type { IShell } from '@ava/core-v2/platform'
import type { SamplingHandler } from './client.js'
import { MCPClient } from './client.js'
import { HttpStreamTransport } from './http-stream-transport.js'
import { ReconnectStrategy } from './reconnect.js'
import type { MCPTransport } from './transport.js'
import { SSETransport, StdioTransport } from './transport.js'
import type { MCPConnectionState, MCPPrompt, MCPResource, MCPServer, MCPTool } from './types.js'

interface ActiveConnection {
  state: MCPConnectionState
  client: MCPClient
  transport: MCPTransport
  reconnect?: ReconnectStrategy
  connectOptions?: ConnectOptions
}

const connections = new Map<string, ActiveConnection>()

export function getConnections(): ReadonlyMap<string, MCPConnectionState> {
  const states = new Map<string, MCPConnectionState>()
  for (const [name, conn] of connections) {
    states.set(name, conn.state)
  }
  return states
}

export interface ConnectOptions {
  shell: IShell
  onReconnecting?: (serverName: string, attempt: number) => void
  onSampling?: SamplingHandler
}

export async function connectServer(
  server: MCPServer,
  shellOrOptions: IShell | ConnectOptions
): Promise<MCPTool[]> {
  const options: ConnectOptions =
    'exec' in shellOrOptions ? { shell: shellOrOptions } : shellOrOptions
  // Disconnect existing connection with same name
  if (connections.has(server.name)) {
    await disconnectServer(server.name)
  }

  const state: MCPConnectionState = {
    server,
    status: 'connecting',
    tools: [],
    resources: [],
    prompts: [],
  }
  let transport: MCPTransport

  if (server.transport === 'stdio') {
    if (!server.command) {
      throw new Error(`Stdio server "${server.name}" requires a command`)
    }
    transport = new StdioTransport(options.shell, server.command, server.args ?? [], {
      env: server.env,
    })
  } else if (server.transport === 'http-stream') {
    transport = new HttpStreamTransport({ url: server.uri, headers: server.env })
  } else {
    transport = new SSETransport(server.uri)
  }

  const client = new MCPClient(transport)

  // Set up sampling handler
  if (options.onSampling) {
    client.onSamplingRequest(options.onSampling)
  }

  const reconnect = server.reconnect
    ? new ReconnectStrategy({
        maxAttempts: server.reconnect.maxAttempts,
        baseDelayMs: server.reconnect.baseDelayMs,
      })
    : undefined

  connections.set(server.name, { state, client, transport, reconnect, connectOptions: options })

  // Set up reconnection on transport close
  if (reconnect) {
    transport.onClose?.(() => {
      if (state.status === 'connected') {
        state.status = 'disconnected'
        void attemptReconnect(server, options, reconnect)
      }
    })
  }

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

    // Fetch resources if server supports them
    if (client.serverCapabilities.resources) {
      try {
        state.resources = await client.listResources()
      } catch {
        // Resources not available
      }
    }

    // Fetch prompts if server supports them
    if (client.serverCapabilities.prompts) {
      try {
        state.prompts = await client.listPrompts()
      } catch {
        // Prompts not available
      }
    }

    reconnect?.reset()
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

async function attemptReconnect(
  server: MCPServer,
  options: ConnectOptions,
  strategy: ReconnectStrategy
): Promise<void> {
  const delay = strategy.nextDelay()
  if (delay === null) return // Max attempts exceeded

  options.onReconnecting?.(server.name, strategy.attemptCount)
  await new Promise((r) => setTimeout(r, delay))

  try {
    await connectServer(server, options)
  } catch {
    // Will retry on next close callback
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

export function getResources(): MCPResource[] {
  const resources: MCPResource[] = []
  for (const conn of connections.values()) {
    if (conn.state.status === 'connected') {
      resources.push(...conn.state.resources)
    }
  }
  return resources
}

export function getPrompts(): MCPPrompt[] {
  const prompts: MCPPrompt[] = []
  for (const conn of connections.values()) {
    if (conn.state.status === 'connected') {
      prompts.push(...conn.state.prompts)
    }
  }
  return prompts
}

export async function readResource(
  serverName: string,
  uri: string
): Promise<{ success: boolean; output: string }> {
  const conn = connections.get(serverName)
  if (!conn) {
    return { success: false, output: `MCP server "${serverName}" is not connected` }
  }

  try {
    const contents = await conn.client.readResource(uri)
    const text = contents.map((c) => c.text ?? '').join('\n')
    return { success: true, output: text || 'Resource read successfully' }
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    return { success: false, output: `Failed to read resource: ${message}` }
  }
}

export async function getPrompt(
  serverName: string,
  promptName: string,
  args?: Record<string, string>
): Promise<{ success: boolean; output: string }> {
  const conn = connections.get(serverName)
  if (!conn) {
    return { success: false, output: `MCP server "${serverName}" is not connected` }
  }

  try {
    const result = await conn.client.getPrompt(promptName, args)
    const text = result.messages
      .map((m) => `[${m.role}] ${m.content.text ?? JSON.stringify(m.content)}`)
      .join('\n')
    return { success: true, output: text }
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    return { success: false, output: `Failed to get prompt: ${message}` }
  }
}

/** Get the names of all currently connected servers. */
export function getConnectedServers(): string[] {
  const result: string[] = []
  for (const [name, conn] of connections) {
    if (conn.state.status === 'connected') {
      result.push(name)
    }
  }
  return result
}

/** Ping a server by listing its tools. Throws if not connected or on failure. */
export async function ping(serverId: string): Promise<void> {
  const conn = connections.get(serverId)
  if (!conn || conn.state.status !== 'connected') {
    throw new Error(`Server "${serverId}" is not connected`)
  }
  await conn.client.listTools()
}

/** Restart a server by disconnecting and reconnecting. */
export async function restart(serverId: string): Promise<MCPTool[]> {
  const conn = connections.get(serverId)
  if (!conn) {
    throw new Error(`Server "${serverId}" not found`)
  }
  const server = conn.state.server
  const options = conn.connectOptions
  if (!options) {
    throw new Error(`No connect options stored for "${serverId}"`)
  }
  await disconnectServer(serverId)
  return connectServer(server, options)
}

export async function resetMCP(): Promise<void> {
  const names = [...connections.keys()]
  for (const name of names) {
    await disconnectServer(name)
  }
}
