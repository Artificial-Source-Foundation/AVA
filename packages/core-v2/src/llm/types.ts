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
  | 'azure'
  | 'glm'
  | 'kimi'
  | 'mistral'
  | 'groq'
  | 'deepseek'
  | 'xai'
  | 'cohere'
  | 'together'
  | 'ollama'
  | 'litellm'

export type AuthMethod = 'api-key' | 'oauth' | 'gateway'

// ─── Messages ────────────────────────────────────────────────────────────────

export interface TextBlock {
  type: 'text'
  text: string
}

export interface ToolUseBlock {
  type: 'tool_use'
  id: string
  name: string
  input: Record<string, unknown>
}

export interface ToolResultBlock {
  type: 'tool_result'
  tool_use_id: string
  content: string
  is_error?: boolean
}

export interface ImageBlock {
  type: 'image'
  source: {
    type: 'base64' | 'url'
    media_type: 'image/jpeg' | 'image/png' | 'image/gif' | 'image/webp'
    data: string // base64 data or URL
  }
}

export type ContentBlock = TextBlock | ToolUseBlock | ToolResultBlock | ImageBlock
export type MessageContent = string | ContentBlock[]

export interface ChatMessage {
  role: 'user' | 'assistant' | 'system'
  content: MessageContent
  /** Per-message system prompt override (prepended to system prompt for this turn). */
  _system?: string
  /** Per-message response format override ('json' to switch to JSON mode). */
  _format?: string
  /** Per-message model variant hint (e.g. 'fast', 'thinking', 'cheap'). */
  _variant?: string
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
  /** Force the model to call a specific tool. */
  toolChoice?: { type: 'tool'; name: string } | { type: 'auto' } | { type: 'none' }
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
  thinking?: string
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
