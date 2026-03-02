/**
 * MCP client types.
 */

export interface MCPServer {
  name: string
  uri: string
  transport: 'stdio' | 'sse' | 'http-stream'
  command?: string
  args?: string[]
  env?: Record<string, string>
  oauth?: MCPOAuthConfig
  reconnect?: { maxAttempts?: number; baseDelayMs?: number }
}

export interface MCPTool {
  name: string
  description: string
  inputSchema: Record<string, unknown>
  serverName: string
}

export interface MCPResource {
  uri: string
  name: string
  description?: string
  mimeType?: string
}

export interface MCPResourceContents {
  uri: string
  mimeType?: string
  text?: string
  blob?: string
}

export interface MCPPrompt {
  name: string
  description?: string
  arguments?: Array<{ name: string; description?: string; required?: boolean }>
}

export interface MCPPromptMessage {
  role: 'user' | 'assistant'
  content: { type: string; text?: string; [key: string]: unknown }
}

export interface MCPSamplingRequest {
  messages: Array<{ role: string; content: { type: string; text?: string } }>
  modelPreferences?: {
    hints?: Array<{ name: string }>
    costPriority?: number
    speedPriority?: number
  }
  systemPrompt?: string
  maxTokens: number
}

export interface MCPSamplingResult {
  role: 'assistant'
  content: { type: string; text: string }
  model: string
}

export interface MCPOAuthConfig {
  authorizationUrl: string
  tokenUrl: string
  clientId: string
  clientSecret?: string
  scopes?: string[]
  redirectUri?: string
}

export interface MCPOAuthTokens {
  accessToken: string
  refreshToken?: string
  expiresAt?: number
  tokenType?: string
}

export interface MCPConnectionState {
  server: MCPServer
  status: 'connecting' | 'connected' | 'disconnected' | 'error'
  tools: MCPTool[]
  resources: MCPResource[]
  prompts: MCPPrompt[]
  error?: string
}
