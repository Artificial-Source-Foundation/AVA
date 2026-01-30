/**
 * LLM Provider Types
 * Provider-agnostic type definitions for multi-provider LLM integration
 */

// ============================================================================
// Provider Types
// ============================================================================

/** Supported LLM providers */
export type LLMProvider = 'openrouter' | 'anthropic' | 'openai' | 'glm'

/** Authentication methods */
export type AuthMethod = 'api-key' | 'oauth' | 'gateway'

// ============================================================================
// Message Types (OpenAI-compatible format)
// ============================================================================

/** Unified message format used across all providers */
export interface ChatMessage {
  role: 'user' | 'assistant' | 'system'
  content: string
}

/** Content block for structured responses (future: tool use) */
export interface ContentBlock {
  type: 'text' | 'tool_use' | 'tool_result'
  text?: string
  id?: string
  name?: string
  input?: Record<string, unknown>
}

// ============================================================================
// Configuration Types
// ============================================================================

/** Configuration for a specific provider/model combination */
export interface ProviderConfig {
  provider: LLMProvider
  model: string
  authMethod: AuthMethod
  maxTokens?: number
  temperature?: number
  systemPrompt?: string
}

/** Model definition with provider mapping */
export interface ModelDefinition {
  id: string // Display ID (e.g., 'claude-sonnet-4')
  name: string // Display name
  provider: LLMProvider // Primary provider
  providerId: string // Provider-specific model ID
  openrouterId?: string // OpenRouter model ID (if different)
  maxTokens: number // Default max tokens
  contextWindow: number // Context window size
}

// ============================================================================
// Credential Types
// ============================================================================

/** Stored credential for a provider */
export interface Credentials {
  provider: LLMProvider
  type: 'api-key' | 'oauth-token'
  value: string
  expiresAt?: number // Timestamp for OAuth token expiry
  refreshToken?: string // For OAuth token refresh
}

/** All stored credentials indexed by provider */
export interface StoredCredentials {
  [provider: string]: Credentials
}

// ============================================================================
// Streaming Types
// ============================================================================

/** Unified stream delta across providers */
export interface StreamDelta {
  content: string
  done: boolean
  usage?: TokenUsage
  error?: StreamError
}

/** Token usage information */
export interface TokenUsage {
  inputTokens: number
  outputTokens: number
  totalTokens?: number
}

/** Stream error information */
export interface StreamError {
  type: 'rate_limit' | 'auth' | 'server' | 'network' | 'unknown'
  message: string
  status?: number
  retryAfter?: number // Seconds until retry (for rate limits)
}

// ============================================================================
// API Response Types
// ============================================================================

/** OpenAI/OpenRouter SSE event types */
export interface OpenAIStreamEvent {
  id: string
  object: 'chat.completion.chunk'
  created: number
  model: string
  choices: Array<{
    index: number
    delta: {
      role?: 'assistant'
      content?: string
    }
    finish_reason: string | null
  }>
  usage?: {
    prompt_tokens: number
    completion_tokens: number
    total_tokens: number
  }
}

/** Anthropic SSE event types */
export type AnthropicStreamEvent =
  | {
      type: 'message_start'
      message: { id: string; model: string; usage: { input_tokens: number } }
    }
  | { type: 'content_block_start'; index: number; content_block: { type: 'text'; text: string } }
  | { type: 'content_block_delta'; index: number; delta: { type: 'text_delta'; text: string } }
  | { type: 'content_block_stop'; index: number }
  | { type: 'message_delta'; delta: { stop_reason: string }; usage: { output_tokens: number } }
  | { type: 'message_stop' }
  | { type: 'error'; error: { type: string; message: string } }

// ============================================================================
// Error Types
// ============================================================================

/** LLM-specific error with provider context */
export class LLMError extends Error {
  constructor(
    message: string,
    public provider: LLMProvider,
    public status?: number,
    public retryAfter?: number
  ) {
    super(message)
    this.name = 'LLMError'
  }
}

/** Error messages by status code */
export const ERROR_MESSAGES: Record<number, string> = {
  400: 'Invalid request',
  401: 'Invalid API key',
  403: 'Access forbidden',
  404: 'Model not found',
  429: 'Rate limit exceeded',
  500: 'Server error',
  529: 'API overloaded',
}
