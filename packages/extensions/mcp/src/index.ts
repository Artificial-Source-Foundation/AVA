/**
 * MCP extension — Model Context Protocol client.
 *
 * Connects to MCP servers, discovers their tools, and registers them
 * as AVA tools. Supports resources, prompts, sampling, and reconnection.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { callTool, connectServer, disconnectServer, resetMCP } from './manager.js'
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
  const toolDisposables: Disposable[] = []

  // Connect initial servers from settings
  const config = api.getSettings<{ servers?: MCPServer[] }>('mcp')
  if (config?.servers) {
    for (const server of config.servers) {
      connectServer(server, {
        shell: api.platform.shell,
        onReconnecting: (name, attempt) => {
          api.log.debug(`MCP reconnecting to ${name} (attempt ${attempt})`)
        },
      })
        .then((tools) => {
          const registered = registerMCPTools(api, server.name, tools)
          toolDisposables.push(...registered)
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
      connectServer(server, { shell: api.platform.shell })
        .then((tools) => {
          const registered = registerMCPTools(api, server.name, tools)
          toolDisposables.push(...registered)
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
      for (const d of toolDisposables) d.dispose()
      for (const d of disposables) d.dispose()
      resetMCP().catch(() => {})
      api.log.debug('MCP extension deactivated')
    },
  }
}

export { callTool, getPrompts, getResources, getTools, resetMCP } from './manager.js'
