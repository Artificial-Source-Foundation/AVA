/**
 * LLM types — messages, streaming, tool definitions, providers.
 *
 * No provider implementations here. Extensions register providers
 * via `api.registerProvider()`.
 */

// ─── Providers ───────────────────────────────────────────────────────────────

export type LLMProvider =
  | 'anthropic'
  | 'openai'
  | 'openrouter'
  | 'google'
  | 'copilot'
  | 'glm'
  | 'kimi'
  | 'mistral'
  | 'groq'
  | 'deepseek'
  | 'xai'
  | 'cohere'
  | 'together'
  | 'ollama'

export type AuthMethod = 'api-key' | 'oauth' | 'gateway'

// ─── Messages ────────────────────────────────────────────────────────────────

export interface ChatMessage {
  role: 'user' | 'assistant' | 'system'
  content: string
}

export interface ToolUseBlock {
  type: 'tool_use'
  id: string
  name: string
  input: Record<string, unknown>
}

// ─── Tool Definitions ────────────────────────────────────────────────────────

export interface ToolDefinition {
  name: string
  description: string
  input_schema: {
    type: 'object'
    properties: Record<string, unknown>
    required?: string[]
  }
}

// ─── Provider Config ─────────────────────────────────────────────────────────

export interface ProviderConfig {
  provider: LLMProvider
  model: string
  authMethod?: AuthMethod
  maxTokens?: number
  temperature?: number
  systemPrompt?: string
  tools?: ToolDefinition[]
  thinking?: { enabled: boolean }
}

export interface Credentials {
  provider: LLMProvider
  type: 'api-key' | 'oauth-token'
  value: string
  expiresAt?: number
  refreshToken?: string
}

// ─── Streaming ───────────────────────────────────────────────────────────────

export interface StreamDelta {
  content?: string
  done?: boolean
  usage?: TokenUsage
  error?: StreamError
  toolUse?: ToolUseBlock
}

export interface TokenUsage {
  inputTokens: number
  outputTokens: number
  totalTokens?: number
}

export interface StreamError {
  type: 'rate_limit' | 'auth' | 'server' | 'network' | 'api' | 'unknown'
  message: string
  status?: number
  retryAfter?: number
}

// ─── LLM Client Interface ───────────────────────────────────────────────────

export interface LLMClient {
  stream(
    messages: ChatMessage[],
    config: ProviderConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown>
}

// ─── Auth ────────────────────────────────────────────────────────────────────

export interface AuthInfo {
  type: 'api-key' | 'oauth'
  token: string
  accountId?: string
}
