/**
 * OpenRouter Provider Client
 * Gateway for 100+ LLM models via unified OpenAI-compatible API
 * https://openrouter.ai/docs
 */

import type {
  ChatMessage,
  LLMProvider,
  OpenAIStreamEvent,
  ProviderConfig,
  StreamDelta,
} from '../../../types/llm'
import { getApiKeyWithFallback, getCredentials } from '../../auth/credentials'
import { getOpenRouterModelId, type LLMClient, registerClient } from '../client'

const BASE_URL = 'https://openrouter.ai/api/v1'
const PROVIDER: LLMProvider = 'openrouter'

/**
 * OpenRouter client implementation
 * Uses OpenAI-compatible chat completions API
 */
class OpenRouterClient implements LLMClient {
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
          message: 'No OpenRouter API key configured',
        },
      }
      return
    }

    // Convert model ID to OpenRouter format
    const modelId = getOpenRouterModelId(config.model)

    // Build request
    const body = {
      model: modelId,
      messages: messages.map((m) => ({
        role: m.role,
        content: m.content,
      })),
      max_tokens: config.maxTokens || 4096,
      temperature: config.temperature ?? 0.7,
      stream: true,
    }

    // Make request
    let response: Response
    try {
      response = await fetch(`${BASE_URL}/chat/completions`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          Authorization: `Bearer ${cred.value}`,
          'HTTP-Referer': 'https://estela.app', // Required by OpenRouter
          'X-Title': 'Estela',
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
          if (!line.trim() || line.startsWith(':')) continue
          if (!line.startsWith('data: ')) continue

          const data = line.slice(6).trim()
          if (data === '[DONE]') {
            yield {
              content: '',
              done: true,
              usage: { inputTokens, outputTokens, totalTokens: inputTokens + outputTokens },
            }
            return
          }

          try {
            const event = JSON.parse(data) as OpenAIStreamEvent

            // Extract content delta
            const delta = event.choices?.[0]?.delta?.content
            if (delta) {
              outputTokens++
              yield { content: delta, done: false }
            }

            // Check for finish
            if (event.choices?.[0]?.finish_reason) {
              // Update usage if provided
              if (event.usage) {
                inputTokens = event.usage.prompt_tokens
                outputTokens = event.usage.completion_tokens
              }
            }
          } catch {
            // Skip malformed JSON
            console.warn('Failed to parse SSE data:', data)
          }
        }
      }

      // Stream ended without [DONE]
      yield {
        content: '',
        done: true,
        usage: { inputTokens, outputTokens, totalTokens: inputTokens + outputTokens },
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
    402: 'Insufficient credits',
    403: 'Access forbidden',
    404: 'Model not found',
    429: 'Rate limit exceeded',
    500: 'Server error',
    502: 'Gateway error',
    503: 'Service unavailable',
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
registerClient('openrouter', OpenRouterClient)
