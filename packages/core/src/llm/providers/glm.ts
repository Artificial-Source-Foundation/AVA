/**
 * GLM Provider Client (Zhipu AI)
 * Direct integration with Zhipu AI's GLM models
 * Uses OpenAI-compatible API format
 * https://docs.z.ai/
 */

import type { ChatMessage, LLMProvider, ProviderConfig, StreamDelta } from '../../types/llm.js'
import { getAuth, type LLMClient, registerClient } from '../client.js'

const BASE_URL = 'https://open.bigmodel.cn/api/paas/v4'
const PROVIDER: LLMProvider = 'glm'

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
 * GLM client implementation
 * Uses OpenAI-compatible chat completions API
 */
class GLMClient implements LLMClient {
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
          message: 'No Zhipu AI API key configured. Set AVA_GLM_API_KEY.',
        },
      }
      return
    }

    // Build request body
    const body = {
      model: config.model || 'glm-4',
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
          Authorization: `Bearer ${auth.token}`,
        },
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
    429: 'Rate limit exceeded',
    500: 'Server error',
  }

  return messages[status] || `HTTP ${status}`
}

registerClient('glm', GLMClient)
