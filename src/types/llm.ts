/**
 * LLM Provider Types
 * Provider-agnostic type definitions for multi-provider LLM integration
 */

// ============================================================================
// Provider Types
// ============================================================================

/** Supported LLM providers */
export type LLMProvider =
  | 'openrouter'
  | 'anthropic'
  | 'openai'
  | 'google'
  | 'copilot'
  | 'glm'
  | 'kimi'

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

// ============================================================================
// Tool Types
// ============================================================================

/** Tool use block in assistant message */
export interface ToolUseBlock {
  type: 'tool_use'
  id: string
  name: string
  input: Record<string, unknown>
}

/** Tool definition for LLM */
export interface ToolDefinition {
  name: string
  description: string
  input_schema: {
    type: 'object'
    properties: Record<string, unknown>
    required?: string[]
  }
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
  tools?: ToolDefinition[]
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
  toolUse?: ToolUseBlock
}

/** Token usage information */
export interface TokenUsage {
  inputTokens: number
  outputTokens: number
  totalTokens?: number
}

/** Stream error information */
export interface StreamError {
  type: 'rate_limit' | 'auth' | 'server' | 'network' | 'api' | 'unknown'
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
  | {
      type: 'content_block_start'
      index: number
      content_block: { type: 'tool_use'; id: string; name: string }
    }
  | { type: 'content_block_delta'; index: number; delta: { type: 'text_delta'; text: string } }
  | {
      type: 'content_block_delta'
      index: number
      delta: { type: 'input_json_delta'; partial_json: string }
    }
  | { type: 'content_block_stop'; index: number }
  | { type: 'message_delta'; delta: { stop_reason: string }; usage: { output_tokens: number } }
  | { type: 'message_stop' }
  | { type: 'error'; error: { type: string; message: string } }
