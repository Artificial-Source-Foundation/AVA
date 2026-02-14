/**
 * ACP (Agent Client Protocol) Module
 *
 * Production-quality ACP integration for AVA.
 * Provides session persistence, terminal bridging, MCP forwarding,
 * mode switching, and error handling for editor integration.
 */

// Error Handler
export type { ErrorEntry, FormattedError } from './error-handler.js'
export { AcpErrorHandler, createAcpErrorHandler } from './error-handler.js'
// MCP Bridge
export { AcpMCPBridge, createAcpMCPBridge } from './mcp-bridge.js'
// Mode Manager
export type { ModeChangeEvent, ModeChangeListener } from './mode.js'
export { AcpModeManager, createAcpModeManager } from './mode.js'
// Session Store
export { AcpSessionStore, createAcpSessionStore } from './session-store.js'
// Terminal Bridge
export { AcpTerminalBridge, createAcpTerminalBridge } from './terminal.js'
// Types
export type {
  AcpClientCapabilities,
  AcpMCPServerConfig,
  AcpMode,
  AcpSessionInfo,
  AcpTerminalCapabilities,
  AcpTerminalCreateRequest,
  AcpTerminalResult,
  AcpTerminalWriteRequest,
  AcpTransport,
} from './types.js'
export { AcpError, AcpErrorCode } from './types.js'
