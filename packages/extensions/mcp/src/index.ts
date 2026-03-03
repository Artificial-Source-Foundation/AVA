/**
 * MCP extension — Model Context Protocol client.
 *
 * Connects to MCP servers, discovers their tools, and registers them
 * as AVA tools. Supports resources, prompts, sampling, and reconnection.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { MCPHealthMonitor } from './health.js'
import {
  callTool,
  connectServer,
  disconnectServer,
  getConnectedServers,
  ping,
  resetMCP,
  restart,
} from './manager.js'
import { MCPToolServer } from './server.js'
import type { MCPServer, MCPTool } from './types.js'

function registerMCPTools(api: ExtensionAPI, serverName: string, tools: MCPTool[]): Disposable[] {
  return tools.map((mcpTool) =>
    api.registerTool({
      definition: {
        name: `mcp_${serverName}_${mcpTool.name}`,
        description: `[MCP: ${serverName}] ${mcpTool.description}`,
        input_schema: {
          type: 'object' as const,
          properties: (mcpTool.inputSchema.properties ?? mcpTool.inputSchema) as Record<
            string,
            unknown
          >,
        },
      },
      async execute(params) {
        return callTool(serverName, mcpTool.name, params as Record<string, unknown>)
      },
    })
  )
}

export function activate(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []
  const toolDisposablesByServer = new Map<string, Disposable[]>()

  function clearServerTools(serverName: string): void {
    const items = toolDisposablesByServer.get(serverName)
    if (!items) return
    for (const item of items) item.dispose()
    toolDisposablesByServer.delete(serverName)
  }

  function setServerTools(serverName: string, tools: MCPTool[]): void {
    clearServerTools(serverName)
    const registered = registerMCPTools(api, serverName, tools)
    toolDisposablesByServer.set(serverName, registered)
  }

  const healthMonitor = new MCPHealthMonitor(
    {
      getConnectedServers,
      ping,
      restart,
    },
    {
      intervalMs: 30_000,
      timeoutMs: 10_000,
      maxFailures: 3,
    }
  )

  const settings = api.getSettings<{
    servers?: MCPServer[]
    serverMode?: { enabled?: boolean; stdio?: boolean; unixSocketPath?: string }
  }>('mcp')
  const serverMode = settings?.serverMode
  const mcpToolServer =
    serverMode?.enabled === true
      ? new MCPToolServer({
          enabled: true,
          stdio: serverMode.stdio,
          unixSocketPath: serverMode.unixSocketPath,
        })
      : null

  healthMonitor.start()
  if (mcpToolServer) {
    void mcpToolServer.start().catch((err) => {
      const message = err instanceof Error ? err.message : String(err)
      api.log.error(`Failed to start MCP server mode: ${message}`)
      api.emit('mcp:error', { serverName: 'local-server', error: message })
    })
  }

  // Connect initial servers from settings
  const config = settings
  if (config?.servers) {
    for (const server of config.servers) {
      connectServer(server, {
        shell: api.platform.shell,
        onReconnecting: (name, attempt) => {
          api.log.debug(`MCP reconnecting to ${name} (attempt ${attempt})`)
        },
        onToolsListChanged: (serverName, tools) => {
          setServerTools(serverName, tools)
          api.emit('mcp:tools-updated', { serverName, toolCount: tools.length })
        },
      })
        .then((tools) => {
          setServerTools(server.name, tools)
          api.log.debug(`MCP server connected: ${server.name} (${tools.length} tools)`)
          api.emit('mcp:connected', { serverName: server.name, toolCount: tools.length })
        })
        .catch((err) => {
          const message = err instanceof Error ? err.message : String(err)
          api.log.error(`MCP server "${server.name}" failed to connect: ${message}`)
          api.emit('mcp:error', { serverName: server.name, error: message })
        })
    }
  }

  // Dynamic server add
  disposables.push(
    api.on('mcp:add-server', (data) => {
      const server = data as MCPServer
      connectServer(server, {
        shell: api.platform.shell,
        onToolsListChanged: (serverName, tools) => {
          setServerTools(serverName, tools)
          api.emit('mcp:tools-updated', { serverName, toolCount: tools.length })
        },
      })
        .then((tools) => {
          setServerTools(server.name, tools)
          api.emit('mcp:connected', { serverName: server.name, toolCount: tools.length })
        })
        .catch((err) => {
          const message = err instanceof Error ? err.message : String(err)
          api.log.error(`MCP server "${server.name}" failed to connect: ${message}`)
          api.emit('mcp:error', { serverName: server.name, error: message })
        })
    })
  )

  // Dynamic server remove
  disposables.push(
    api.on('mcp:remove-server', (data) => {
      const { name } = data as { name: string }
      disconnectServer(name)
        .then(() => {
          clearServerTools(name)
          api.emit('mcp:server-removed', { name })
        })
        .catch((err) => {
          const message = err instanceof Error ? err.message : String(err)
          api.log.error(`Failed to disconnect MCP server "${name}": ${message}`)
        })
    })
  )

  api.emit('mcp:ready', { servers: [] })
  api.log.debug('MCP extension activated')

  return {
    dispose() {
      for (const items of toolDisposablesByServer.values()) {
        for (const d of items) d.dispose()
      }
      toolDisposablesByServer.clear()
      for (const d of disposables) d.dispose()
      healthMonitor.stop()
      void mcpToolServer?.stop()
      resetMCP().catch(() => {})
      api.log.debug('MCP extension deactivated')
    },
  }
}

export type { HealthCheckConfig, HealthStatus, MCPManager } from './health.js'
export { MCPHealthMonitor } from './health.js'
export {
  callTool,
  getConnectedServers,
  getPrompts,
  getResources,
  getTools,
  ping,
  resetMCP,
  restart,
} from './manager.js'
