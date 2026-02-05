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

// OAuth
export {
  areTokensExpired,
  clearPendingStates,
  completeOAuthFlow,
  getAuthorizationHeader,
  getStoredTokens,
  getValidTokens,
  hasStoredTokens,
  type MCPOAuthConfig,
  type MCPOAuthTokens,
  refreshTokens,
  removeTokens,
  startOAuthFlow,
  storeTokens,
} from './oauth.js'

// Types
export * from './types.js'
