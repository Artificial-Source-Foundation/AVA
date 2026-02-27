/**
 * MCP extension — Model Context Protocol client.
 *
 * Connects to MCP servers and registers their tools.
 * Wires the existing manager.ts to the ExtensionAPI.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { addServer, getConnections, removeServer, resetMCP } from './manager.js'
import type { MCPServer } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []
  const toolDisposables: Disposable[] = []

  // Load initial servers from settings
  const servers = api.getSettings<{ servers?: MCPServer[] }>('mcp')
  if (servers?.servers) {
    for (const server of servers.servers) {
      addServer(server)
      api.log.debug(`MCP server registered: ${server.name}`)
    }
  }

  // Listen for MCP connection events to register tools
  disposables.push(
    api.on('mcp:connected', (data) => {
      const { serverName } = data as { serverName: string }
      const connection = getConnections().get(serverName)
      if (!connection) return

      // Register tools from this server
      for (const mcpTool of connection.tools) {
        const toolDisposable = api.registerTool({
          definition: {
            name: `mcp_${mcpTool.serverName}_${mcpTool.name}`,
            description: mcpTool.description,
            input_schema: {
              type: 'object' as const,
              properties: mcpTool.inputSchema,
            },
          },
          async execute(_params, _ctx) {
            // MCP tool execution would be handled by the MCP client
            api.emit('mcp:tool-call', {
              serverName: mcpTool.serverName,
              toolName: mcpTool.name,
              params: _params,
            })
            return { success: true, output: 'MCP tool call dispatched' }
          },
        })
        toolDisposables.push(toolDisposable)
      }

      api.log.debug(`Registered ${connection.tools.length} tools from MCP server: ${serverName}`)
    })
  )

  // Listen for server add/remove events
  disposables.push(
    api.on('mcp:add-server', (data) => {
      const server = data as MCPServer
      addServer(server)
      api.emit('mcp:server-added', { name: server.name })
    })
  )

  disposables.push(
    api.on('mcp:remove-server', (data) => {
      const { name } = data as { name: string }
      removeServer(name)
      api.emit('mcp:server-removed', { name })
    })
  )

  api.emit('mcp:ready', { servers: [...getConnections().keys()] })
  api.log.debug('MCP extension activated')

  return {
    dispose() {
      for (const d of toolDisposables) d.dispose()
      for (const d of disposables) d.dispose()
      resetMCP()
      api.log.debug('MCP extension deactivated')
    },
  }
}

export { getConnections, getTools, resetMCP } from './manager.js'
