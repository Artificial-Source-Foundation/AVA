/**
 * ACP (Agent Client Protocol) Types
 *
 * Type definitions for the ACP integration layer.
 * These types bridge between the ACP protocol and Estela's internal systems.
 */

// ============================================================================
// Session Types
// ============================================================================

/** ACP session info for persistence */
export interface AcpSessionInfo {
  /** Session ID (ACP protocol) */
  sessionId: string
  /** Mapped Estela session ID (from SessionManager) */
  estelaSessionId: string
  /** Working directory for this session */
  workingDirectory: string
  /** When the session was created */
  createdAt: number
  /** When the session was last active */
  lastActiveAt: number
  /** Current mode */
  mode: AcpMode
}

/** ACP mode (agent or plan) */
export type AcpMode = 'agent' | 'plan'

// ============================================================================
// Terminal Types
// ============================================================================

/** ACP terminal capabilities reported by the editor */
export interface AcpTerminalCapabilities {
  /** Editor supports creating terminals */
  createTerminal: boolean
  /** Editor supports writing to terminals */
  writeTerminal: boolean
  /** Editor supports waiting for exit */
  waitForExit: boolean
  /** Editor supports killing terminals */
  killTerminal: boolean
}

/** Result from running a command in the editor's terminal */
export interface AcpTerminalResult {
  /** Exit code (0 = success) */
  exitCode: number
  /** Combined stdout/stderr output */
  output: string
  /** Whether the command was killed */
  killed: boolean
}

/** ACP terminal request sent to editor */
export interface AcpTerminalCreateRequest {
  name: string
  cwd: string
}

/** ACP terminal write request */
export interface AcpTerminalWriteRequest {
  terminalId: string
  data: string
}

// ============================================================================
// MCP Bridge Types
// ============================================================================

/** MCP server config received from the editor */
export interface AcpMCPServerConfig {
  /** Server name */
  name: string
  /** Transport type */
  transport: 'stdio' | 'sse' | 'http'
  /** Command for stdio transport */
  command?: string
  /** Command arguments */
  args?: string[]
  /** URL for SSE/HTTP transport */
  url?: string
  /** Environment variables */
  env?: Record<string, string>
}

// ============================================================================
// Error Types
// ============================================================================

/** ACP error codes aligned with JSON-RPC 2.0 */
export enum AcpErrorCode {
  /** Generic internal error */
  INTERNAL = -32603,
  /** Session not found */
  SESSION_NOT_FOUND = -32001,
  /** Session already exists */
  SESSION_EXISTS = -32002,
  /** Operation cancelled */
  CANCELLED = -32003,
  /** Editor disconnected */
  DISCONNECTED = -32004,
  /** Invalid mode */
  INVALID_MODE = -32005,
  /** Terminal unavailable */
  TERMINAL_UNAVAILABLE = -32006,
  /** MCP connection failed */
  MCP_FAILED = -32007,
}

/** Structured ACP error */
export class AcpError extends Error {
  readonly code: AcpErrorCode
  readonly data?: unknown

  constructor(code: AcpErrorCode, message: string, data?: unknown) {
    super(message)
    this.name = 'AcpError'
    this.code = code
    this.data = data
  }
}

// ============================================================================
// Transport Abstraction
// ============================================================================

/**
 * Abstraction over the ACP transport layer.
 * Allows session-store, terminal, etc. to send requests
 * without depending on the concrete AgentSideConnection.
 */
export interface AcpTransport {
  /** Send a JSON-RPC request and wait for response */
  request<T = unknown>(method: string, params?: unknown): Promise<T>
  /** Send a JSON-RPC notification (no response expected) */
  notify(method: string, params?: unknown): void
}

// ============================================================================
// Client Capabilities
// ============================================================================

/** Capabilities reported by the editor during initialize */
export interface AcpClientCapabilities {
  /** File system capabilities */
  fs?: {
    readTextFile?: boolean
    writeTextFile?: boolean
    listDirectory?: boolean
  }
  /** Terminal capabilities */
  terminal?: AcpTerminalCapabilities
  /** Whether the editor can load previous sessions */
  sessionLoad?: boolean
  /** Whether the editor supports MCP server forwarding */
  mcpServers?: boolean
  /** Supported modes */
  modes?: AcpMode[]
}
