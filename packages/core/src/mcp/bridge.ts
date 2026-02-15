/**
 * MCP Tool Bridge
 * Converts MCP tools to AVA tool format and registers them
 */

import { ToolErrorType } from '../tools/errors.js'
import { registerTool } from '../tools/registry.js'
import type { Tool, ToolContext, ToolDefinition, ToolResult } from '../tools/types.js'
import type { MCPClientManager } from './client.js'
import type { DiscoveredMCPTool, MCPToolContent } from './types.js'

// ============================================================================
// Bridge
// ============================================================================

/**
 * Convert MCP tool to AVA tool format
 */
export function createToolFromMCP(mcpTool: DiscoveredMCPTool): Tool {
  const definition: ToolDefinition = {
    name: mcpTool.name,
    description: `[MCP:${mcpTool.serverName}] ${mcpTool.description}`,
    input_schema: mcpTool.inputSchema as ToolDefinition['input_schema'],
  }

  return {
    definition,

    validate(params: unknown): Record<string, unknown> {
      // Basic validation - ensure params is an object
      if (typeof params !== 'object' || params === null) {
        throw new Error('Invalid params: expected object')
      }
      return params as Record<string, unknown>
    },

    async execute(params: Record<string, unknown>, ctx: ToolContext): Promise<ToolResult> {
      // Check abort signal
      if (ctx.signal.aborted) {
        return {
          success: false,
          output: 'Operation was cancelled',
          error: ToolErrorType.EXECUTION_ABORTED,
        }
      }

      try {
        // Execute MCP tool
        const result = await mcpTool.execute(params)

        // Convert MCP result to AVA format
        const output = formatMCPContent(result.content)

        return {
          success: !result.isError,
          output,
          error: result.isError ? ToolErrorType.UNKNOWN : undefined,
          metadata: {
            mcpServer: mcpTool.serverName,
            mcpTool: mcpTool.originalName,
          },
        }
      } catch (err) {
        const message = err instanceof Error ? err.message : String(err)
        return {
          success: false,
          output: `MCP tool error: ${message}`,
          error: ToolErrorType.UNKNOWN,
        }
      }
    },
  }
}

/**
 * Format MCP content array to string output
 */
function formatMCPContent(content: MCPToolContent[]): string {
  const parts: string[] = []

  for (const item of content) {
    switch (item.type) {
      case 'text':
        if (item.text) {
          parts.push(item.text)
        }
        break

      case 'image':
        if (item.mimeType && item.data) {
          parts.push(`[Image: ${item.mimeType}]`)
          // Could optionally include base64 data reference
        }
        break

      case 'resource':
        if (item.uri) {
          parts.push(`[Resource: ${item.uri}]`)
          if (item.text) {
            parts.push(item.text)
          }
        }
        break
    }
  }

  return parts.join('\n') || '(no output)'
}

// ============================================================================
// Registration
// ============================================================================

/**
 * Register all tools from an MCP client manager
 */
export function registerMCPTools(manager: MCPClientManager): void {
  const tools = manager.getAllTools()
  for (const mcpTool of tools) {
    const tool = createToolFromMCP(mcpTool)
    registerTool(tool)
  }
}

/**
 * Register tools from discovered MCP tools array
 */
export function registerDiscoveredMCPTools(mcpTools: DiscoveredMCPTool[]): void {
  for (const mcpTool of mcpTools) {
    const tool = createToolFromMCP(mcpTool)
    registerTool(tool)
  }
}

// ============================================================================
// Integration Helper
// ============================================================================

/**
 * Connect to MCP servers and register their tools
 */
export async function setupMCPTools(
  manager: MCPClientManager,
  configs: Array<{
    name: string
    type: 'stdio' | 'sse' | 'http'
    command?: string
    url?: string
    args?: string[]
  }>
): Promise<DiscoveredMCPTool[]> {
  const allTools: DiscoveredMCPTool[] = []

  for (const config of configs) {
    try {
      // Connect to server
      await manager.connect({
        name: config.name,
        type: config.type,
        command: config.command,
        url: config.url,
        args: config.args,
      })

      // Discover and register tools
      const tools = await manager.discoverTools(config.name)
      allTools.push(...tools)
    } catch (err) {
      // Log error but continue with other servers
      console.warn(`Failed to setup MCP server '${config.name}':`, err)
    }
  }

  // Register all discovered tools
  registerDiscoveredMCPTools(allTools)

  return allTools
}
