/**
 * MCP (Model Context Protocol) Module
 * Provides client support for MCP servers
 */

// Bridge (tool conversion and registration)
export {
  createToolFromMCP,
  registerDiscoveredMCPTools,
  registerMCPTools,
  setupMCPTools,
} from './bridge.js'

// Client
export { createMCPClient, MCPClientManager } from './client.js'

// Discovery
export {
  discoverMCPServers,
  getMCPConfigPaths,
  loadMCPConfig,
  parseMCPCommand,
} from './discovery.js'
// Types
export * from './types.js'
