/**
 * Anthropic Provider Client
 * Direct integration with Claude API
 * https://docs.anthropic.com/en/api/messages
 */

import type {
  AnthropicStreamEvent,
  ChatMessage,
  LLMProvider,
  ProviderConfig,
  StreamDelta,
} from '../../../types/llm'
import { getApiKeyWithFallback, getCredentials } from '../../auth/credentials'
import { type LLMClient, registerClient } from '../client'

const BASE_URL = 'https://api.anthropic.com/v1'
const API_VERSION = '2023-06-01'
const PROVIDER: LLMProvider = 'anthropic'

/**
 * Anthropic client implementation
 * Uses Messages API with streaming
 */
export class AnthropicClient implements LLMClient {
  async *stream(
    messages: ChatMessage[],
    config: ProviderConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown> {
    // Get credentials
    const cred = getCredentials(PROVIDER) || {
      provider: PROVIDER,
      type: 'api-key' as const,
      value: getApiKeyWithFallback(PROVIDER) || '',
    }

    if (!cred.value) {
      yield {
        content: '',
        done: true,
        error: {
          type: 'auth',
          message: 'No Anthropic API key configured',
        },
      }
      return
    }

    // Separate system message from conversation
    const systemMessage = messages.find((m) => m.role === 'system')
    const conversationMessages = messages
      .filter((m) => m.role !== 'system')
      .map((m) => ({
        role: m.role as 'user' | 'assistant',
        content: m.content,
      }))

    // Build request
    const body: Record<string, unknown> = {
      model: config.model,
      messages: conversationMessages,
      max_tokens: config.maxTokens || 4096,
      stream: true,
    }

    // Add system prompt if present
    if (systemMessage) {
      body.system = systemMessage.content
    }

    // Add optional parameters
    if (config.temperature !== undefined) {
      body.temperature = config.temperature
    }

    // Make request
    let response: Response
    try {
      response = await fetch(`${BASE_URL}/messages`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'x-api-key': cred.value,
          'anthropic-version': API_VERSION,
          // Required for browser access
          'anthropic-dangerous-direct-browser-access': 'true',
        },
        body: JSON.stringify(body),
        signal,
      })
    } catch (err) {
      if (err instanceof Error && err.name === 'AbortError') {
        return // Silently exit on abort
      }
      yield {
        content: '',
        done: true,
        error: {
          type: 'network',
          message: `Network error: ${err instanceof Error ? err.message : 'Unknown'}`,
        },
      }
      return
    }

    // Handle HTTP errors
    if (!response.ok) {
      const errorBody = await response.text().catch(() => '')
      yield {
        content: '',
        done: true,
        error: {
          type: getErrorType(response.status),
          message: getErrorMessage(response.status, errorBody),
          status: response.status,
          retryAfter: parseRetryAfter(response.headers.get('retry-after')),
        },
      }
      return
    }

    // Stream response
    const reader = response.body?.getReader()
    if (!reader) {
      yield {
        content: '',
        done: true,
        error: { type: 'unknown', message: 'No response body' },
      }
      return
    }

    const decoder = new TextDecoder()
    let buffer = ''
    let inputTokens = 0
    let outputTokens = 0

    try {
      while (true) {
        const { done, value } = await reader.read()
        if (done) break

        buffer += decoder.decode(value, { stream: true })
        const lines = buffer.split('\n')
        buffer = lines.pop() || ''

        for (const line of lines) {
          if (!line.trim()) continue

          // Anthropic uses 'event:' and 'data:' lines
          if (line.startsWith('event:')) continue
          if (!line.startsWith('data: ')) continue

          const data = line.slice(6).trim()

          try {
            const event = JSON.parse(data) as AnthropicStreamEvent

            switch (event.type) {
              case 'message_start':
                inputTokens = event.message.usage.input_tokens
                break

              case 'content_block_delta':
                if (event.delta.type === 'text_delta' && event.delta.text) {
                  yield { content: event.delta.text, done: false }
                }
                break

              case 'message_delta':
                outputTokens = event.usage.output_tokens
                break

              case 'message_stop':
                yield {
                  content: '',
                  done: true,
                  usage: {
                    inputTokens,
                    outputTokens,
                    totalTokens: inputTokens + outputTokens,
                  },
                }
                return

              case 'error':
                yield {
                  content: '',
                  done: true,
                  error: {
                    type: 'server',
                    message: event.error.message,
                  },
                }
                return
            }
          } catch {
            // Skip malformed JSON
            console.warn('Failed to parse SSE data:', data)
          }
        }
      }

      // Stream ended without message_stop
      yield {
        content: '',
        done: true,
        usage: {
          inputTokens,
          outputTokens,
          totalTokens: inputTokens + outputTokens,
        },
      }
    } finally {
      reader.releaseLock()
    }
  }
}

// ============================================================================
// Helper Functions
// ============================================================================

function getErrorType(status: number): 'rate_limit' | 'auth' | 'server' | 'unknown' {
  if (status === 401 || status === 403) return 'auth'
  if (status === 429) return 'rate_limit'
  if (status === 529) return 'server' // Anthropic overloaded
  if (status >= 500) return 'server'
  return 'unknown'
}

function getErrorMessage(status: number, body: string): string {
  // Try to parse error from body
  try {
    const parsed = JSON.parse(body)
    if (parsed.error?.message) return parsed.error.message
  } catch {
    // Use default messages
  }

  const messages: Record<number, string> = {
    400: 'Invalid request',
    401: 'Invalid API key',
    403: 'Access forbidden',
    404: 'Model not found',
    429: 'Rate limit exceeded',
    500: 'Server error',
    529: 'API overloaded - please retry',
  }

  return messages[status] || `HTTP ${status}`
}

function parseRetryAfter(header: string | null): number | undefined {
  if (!header) return undefined
  const seconds = parseInt(header, 10)
  return Number.isNaN(seconds) ? undefined : seconds
}

// ============================================================================
// Registration
// ============================================================================

// Register this client with the factory
registerClient('anthropic', AnthropicClient)
