/**
 * ACP MCP Bridge
 *
 * Forwards MCP server configurations from the editor to AVA's
 * MCPClientManager. When an editor sends MCP server configs during
 * session creation, this bridge connects them automatically.
 */

import { createMCPClient, type MCPClientManager } from '../mcp/client.js'
import type { MCPServerConfig } from '../mcp/types.js'
import type { AcpMCPServerConfig } from './types.js'
import { AcpError, AcpErrorCode } from './types.js'

// ============================================================================
// ACP MCP Bridge
// ============================================================================

/**
 * Bridges editor MCP server configs to AVA's MCP client.
 *
 * When editors send MCP configs in `session/new`, this bridge:
 * 1. Converts ACP config format → AVA MCPServerConfig
 * 2. Connects to each server via MCPClientManager
 * 3. Discovers tools from connected servers
 * 4. Makes tools available in the ACP session
 */
export class AcpMCPBridge {
  private mcpClient: MCPClientManager
  private connectedServers = new Set<string>()
  private disposed = false

  constructor(mcpClient?: MCPClientManager) {
    this.mcpClient = mcpClient ?? createMCPClient('ava-acp', '0.1.0')
  }

  // ==========================================================================
  // Server Management
  // ==========================================================================

  /**
   * Connect MCP servers from editor configs.
   * Called when editor sends mcpServers in session/new params.
   *
   * @param configs - MCP server configs from the editor
   * @returns Names of successfully connected servers
   */
  async connectServers(configs: AcpMCPServerConfig[]): Promise<string[]> {
    this.ensureNotDisposed()

    const connected: string[] = []

    for (const config of configs) {
      try {
        const normalizedConfig = this.normalizeConfig(config)
        await this.mcpClient.connect(normalizedConfig)
        await this.mcpClient.discoverTools(config.name)
        this.connectedServers.add(config.name)
        connected.push(config.name)
      } catch (error) {
        // Log but don't fail the whole batch
        console.warn(
          `[ACP MCP Bridge] Failed to connect to '${config.name}':`,
          error instanceof Error ? error.message : String(error)
        )
      }
    }

    return connected
  }

  /**
   * Connect a single MCP server
   */
  async connectServer(config: AcpMCPServerConfig): Promise<void> {
    this.ensureNotDisposed()

    const normalizedConfig = this.normalizeConfig(config)

    try {
      await this.mcpClient.connect(normalizedConfig)
      await this.mcpClient.discoverTools(config.name)
      this.connectedServers.add(config.name)
    } catch (error) {
      throw new AcpError(
        AcpErrorCode.MCP_FAILED,
        `Failed to connect MCP server '${config.name}': ${
          error instanceof Error ? error.message : String(error)
        }`
      )
    }
  }

  /**
   * Disconnect a specific MCP server
   */
  async disconnectServer(name: string): Promise<void> {
    this.ensureNotDisposed()

    await this.mcpClient.disconnect(name)
    this.connectedServers.delete(name)
  }

  /**
   * Disconnect all MCP servers managed by this bridge
   */
  async disconnectAll(): Promise<void> {
    for (const name of this.connectedServers) {
      await this.mcpClient.disconnect(name)
    }
    this.connectedServers.clear()
  }

  // ==========================================================================
  // Queries
  // ==========================================================================

  /**
   * Get list of connected server names
   */
  getConnectedServers(): string[] {
    return Array.from(this.connectedServers)
  }

  /**
   * Check if a server is connected
   */
  isConnected(name: string): boolean {
    return this.connectedServers.has(name)
  }

  /**
   * Get the underlying MCP client manager
   */
  getMCPClient(): MCPClientManager {
    return this.mcpClient
  }

  /**
   * Get all discovered tools from connected servers
   */
  getTools(): ReturnType<MCPClientManager['getAllTools']> {
    return this.mcpClient.getAllTools()
  }

  // ==========================================================================
  // Cleanup
  // ==========================================================================

  /**
   * Dispose of the bridge - disconnects all servers
   */
  async dispose(): Promise<void> {
    if (this.disposed) return
    this.disposed = true

    await this.disconnectAll()
  }

  private ensureNotDisposed(): void {
    if (this.disposed) {
      throw new Error('AcpMCPBridge has been disposed')
    }
  }

  // ==========================================================================
  // Private Helpers
  // ==========================================================================

  /**
   * Convert ACP MCP config to AVA MCPServerConfig
   */
  private normalizeConfig(config: AcpMCPServerConfig): MCPServerConfig {
    return {
      name: config.name,
      type: config.transport,
      command: config.command,
      args: config.args,
      url: config.url,
      env: config.env,
    }
  }
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create an ACP MCP bridge
 */
export function createAcpMCPBridge(mcpClient?: MCPClientManager): AcpMCPBridge {
  return new AcpMCPBridge(mcpClient)
}
