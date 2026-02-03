/**
 * OpenAI Provider Client
 * Direct integration with OpenAI API (ChatGPT models)
 * Supports both API key and OAuth (Codex) authentication
 * https://platform.openai.com/docs/api-reference
 */

import { getAccountId } from '../../auth/manager.js'
import { OPENAI_OAUTH_CONFIG } from '../../auth/types.js'
import type { ChatMessage, LLMProvider, ProviderConfig, StreamDelta } from '../../types/llm.js'
import { getAuth, type LLMClient, registerClient } from '../client.js'

const BASE_URL = 'https://api.openai.com/v1'
const PROVIDER: LLMProvider = 'openai'

interface OpenAIStreamEvent {
  choices?: Array<{
    delta?: { content?: string; role?: string }
    finish_reason?: string | null
  }>
  usage?: {
    prompt_tokens: number
    completion_tokens: number
  }
}

/**
 * OpenAI client implementation
 * Uses Chat Completions API with streaming
 */
class OpenAIClient implements LLMClient {
  async *stream(
    messages: ChatMessage[],
    config: ProviderConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown> {
    const auth = await getAuth(PROVIDER)

    if (!auth) {
      yield {
        content: '',
        done: true,
        error: {
          type: 'auth',
          message:
            'No OpenAI authentication configured. Set ESTELA_OPENAI_API_KEY or use `estela auth openai` for OAuth.',
        },
      }
      return
    }

    // Determine endpoint based on auth type
    // OAuth uses Codex endpoint, API key uses standard endpoint
    let endpoint = `${BASE_URL}/chat/completions`
    const headers: Record<string, string> = {
      'Content-Type': 'application/json',
    }

    if (auth.type === 'oauth') {
      // Codex endpoint for ChatGPT Plus/Pro subscribers
      endpoint = OPENAI_OAUTH_CONFIG.apiEndpoint
      headers.Authorization = `Bearer ${auth.token}`

      // Add account ID if available (required for Codex)
      const accountId = await getAccountId(PROVIDER)
      if (accountId) {
        headers['X-ChatGPT-Account-ID'] = accountId
      }
    } else {
      // Standard API key auth
      headers.Authorization = `Bearer ${auth.token}`
    }

    // Build request body
    const body = {
      model: config.model,
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
      response = await fetch(endpoint, {
        method: 'POST',
        headers,
        body: JSON.stringify(body),
        signal,
      })
    } catch (err) {
      if (err instanceof Error && err.name === 'AbortError') {
        return
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

            const delta = event.choices?.[0]?.delta?.content
            if (delta) {
              outputTokens++
              yield { content: delta, done: false }
            }

            if (event.choices?.[0]?.finish_reason && event.usage) {
              inputTokens = event.usage.prompt_tokens
              outputTokens = event.usage.completion_tokens
            }
          } catch {
            console.warn('Failed to parse SSE data:', data)
          }
        }
      }

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

function getErrorType(status: number): 'rate_limit' | 'auth' | 'server' | 'unknown' {
  if (status === 401 || status === 403) return 'auth'
  if (status === 429) return 'rate_limit'
  if (status >= 500) return 'server'
  return 'unknown'
}

function getErrorMessage(status: number, body: string): string {
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
    429: 'Rate limit exceeded - try again later',
    500: 'Server error',
    503: 'Service overloaded',
  }

  return messages[status] || `HTTP ${status}`
}

function parseRetryAfter(header: string | null): number | undefined {
  if (!header) return undefined
  const seconds = parseInt(header, 10)
  return Number.isNaN(seconds) ? undefined : seconds
}

registerClient('openai', OpenAIClient)
