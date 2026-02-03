/**
 * MCP Server Discovery
 * Find MCP server configurations from various config files
 */

import { getPlatform } from '../platform.js'
import type { MCPServerConfig, MCPTransportType } from './types.js'

// ============================================================================
// Types
// ============================================================================

/**
 * Raw MCP config file format (Claude Code style)
 */
interface MCPConfigFile {
  mcpServers?: Record<string, RawMCPServerConfig>
}

/**
 * Raw server config from file
 */
interface RawMCPServerConfig {
  command?: string
  args?: string[]
  url?: string
  httpUrl?: string
  type?: MCPTransportType
  cwd?: string
  env?: Record<string, string>
  headers?: Record<string, string>
  timeout?: number
  includeTools?: string[]
  excludeTools?: string[]
  trust?: 'full' | 'sandbox' | 'none'
}

// ============================================================================
// Config Paths
// ============================================================================

/**
 * Get possible MCP config file paths
 */
export function getMCPConfigPaths(): string[] {
  const home = process.env.HOME ?? process.env.USERPROFILE ?? ''

  return [
    // Estela config
    `${home}/.estela/mcp.json`,

    // Claude Code config (for compatibility)
    `${home}/.claude/claude_desktop_config.json`,

    // Project-local config
    '.estela/mcp.json',
    '.mcp.json',
  ]
}

// ============================================================================
// Discovery
// ============================================================================

/**
 * Discover MCP servers from config files
 */
export async function discoverMCPServers(): Promise<MCPServerConfig[]> {
  const fs = getPlatform().fs
  const servers: MCPServerConfig[] = []
  const seenNames = new Set<string>()

  for (const configPath of getMCPConfigPaths()) {
    try {
      const exists = await fs.exists(configPath)
      if (!exists) continue

      const content = await fs.readFile(configPath)
      const config = JSON.parse(content) as MCPConfigFile

      if (!config.mcpServers) continue

      for (const [name, rawConfig] of Object.entries(config.mcpServers)) {
        // Skip duplicates (first config wins)
        if (seenNames.has(name)) continue
        seenNames.add(name)

        const server = normalizeConfig(name, rawConfig)
        if (server) {
          servers.push(server)
        }
      }
    } catch {
      // Skip invalid config files
    }
  }

  return servers
}

/**
 * Load MCP servers from a specific config file
 */
export async function loadMCPConfig(configPath: string): Promise<MCPServerConfig[]> {
  const fs = getPlatform().fs

  const content = await fs.readFile(configPath)
  const config = JSON.parse(content) as MCPConfigFile

  if (!config.mcpServers) {
    return []
  }

  const servers: MCPServerConfig[] = []

  for (const [name, rawConfig] of Object.entries(config.mcpServers)) {
    const server = normalizeConfig(name, rawConfig)
    if (server) {
      servers.push(server)
    }
  }

  return servers
}

// ============================================================================
// Normalization
// ============================================================================

/**
 * Normalize raw config to MCPServerConfig
 */
function normalizeConfig(name: string, raw: RawMCPServerConfig): MCPServerConfig | null {
  // Determine transport type
  let type: MCPTransportType

  if (raw.type) {
    type = raw.type
  } else if (raw.command) {
    type = 'stdio'
  } else if (raw.httpUrl || raw.url) {
    // Default URL-based to HTTP (can override with type)
    type = 'http'
  } else {
    // Invalid config
    return null
  }

  return {
    name,
    type,
    command: raw.command,
    args: raw.args,
    url: raw.httpUrl ?? raw.url,
    cwd: raw.cwd,
    env: raw.env,
    headers: raw.headers,
    timeout: raw.timeout,
    includeTools: raw.includeTools,
    excludeTools: raw.excludeTools,
    trust: raw.trust,
  }
}

/**
 * Parse an inline MCP server command
 * Format: "command arg1 arg2" or "npx @example/mcp-server"
 */
export function parseMCPCommand(commandStr: string): MCPServerConfig | null {
  const parts = commandStr.trim().split(/\s+/)
  if (parts.length === 0 || !parts[0]) return null

  const command = parts[0]
  const args = parts.slice(1)

  // Generate name from command
  const name = command.split('/').pop()?.replace(/^@/, '') ?? 'inline'

  return {
    name,
    type: 'stdio',
    command,
    args,
  }
}
