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

// OAuth (renamed to avoid conflict with auth/manager.ts exports)
export {
  areTokensExpired as areMCPTokensExpired,
  clearPendingStates as clearMCPPendingStates,
  completeOAuthFlow as completeMCPOAuthFlow,
  getAuthorizationHeader as getMCPAuthorizationHeader,
  getStoredTokens as getMCPStoredTokens,
  getValidTokens as getMCPValidTokens,
  hasStoredTokens as hasMCPStoredTokens,
  type MCPOAuthConfig,
  type MCPOAuthTokens,
  refreshTokens as refreshMCPTokens,
  removeTokens as removeMCPTokens,
  startOAuthFlow as startMCPOAuthFlow,
  storeTokens as storeMCPTokens,
} from './oauth.js'

// Types
export * from './types.js'
