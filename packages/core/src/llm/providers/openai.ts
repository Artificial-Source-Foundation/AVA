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
import { buildHttpError, parseRetryAfter as parseRetryAfterShared } from '../utils/errors.js'
import {
  buildOpenAIRequestBody,
  type OpenAIStreamEvent as SharedOpenAIStreamEvent,
  ToolCallBuffer,
} from '../utils/openai-compat.js'
import { readSSEStream } from '../utils/sse.js'

const BASE_URL = 'https://api.openai.com/v1'
const PROVIDER: LLMProvider = 'openai'
const DEV_CODEX_PROXY_ENDPOINT = '/__chatgpt_proxy/backend-api/codex/responses'

function resolveOAuthEndpoint(): string {
  const location = (
    globalThis as { location?: { hostname?: string; port?: string; protocol?: string } }
  ).location

  if (
    location?.protocol?.startsWith('http') &&
    (location.hostname === 'localhost' || location.hostname === '127.0.0.1') &&
    location.port === '1420'
  ) {
    return DEV_CODEX_PROXY_ENDPOINT
  }

  return OPENAI_OAUTH_CONFIG.apiEndpoint
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
            'No OpenAI authentication configured. Set AVA_OPENAI_API_KEY or use `ava auth openai` for OAuth.',
        },
      }
      return
    }

    // Determine endpoint based on auth type
    // OAuth uses Codex endpoint, API key uses standard chat completions endpoint
    const endpoint = auth.type === 'oauth' ? resolveOAuthEndpoint() : `${BASE_URL}/chat/completions`
    const headers: Record<string, string> = {
      'Content-Type': 'application/json',
    }

    if (auth.type === 'oauth') {
      // Codex endpoint for ChatGPT Plus/Pro subscribers
      headers.Authorization = `Bearer ${auth.token}`

      // Add account ID if available (required for Codex)
      const accountId = await getAccountId(PROVIDER)
      if (accountId) {
        headers['ChatGPT-Account-Id'] = accountId
      }

      headers.originator = 'ava'
    } else {
      // Standard API key auth
      headers.Authorization = `Bearer ${auth.token}`
    }

    // OAuth Codex endpoint is responses-style; API key uses chat completions streaming.
    if (auth.type === 'oauth') {
      const systemInstructions = messages
        .filter((m) => m.role === 'system')
        .map((m) => m.content)
        .join('\n\n')

      const oauthBody = {
        model: config.model,
        instructions: systemInstructions || 'You are AVA, a coding assistant.',
        input: messages
          .filter((m) => m.role !== 'system')
          .map((m) => ({
            role: m.role,
            content: [
              {
                type: m.role === 'assistant' ? 'output_text' : 'input_text',
                text: m.content,
              },
            ],
          })),
        store: false,
        stream: true,
      }

      try {
        const response = await fetch(endpoint, {
          method: 'POST',
          headers,
          body: JSON.stringify(oauthBody),
          signal,
        })

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
                const event = JSON.parse(data) as {
                  type?: string
                  delta?: string
                  response?: {
                    usage?: {
                      input_tokens?: number
                      output_tokens?: number
                      total_tokens?: number
                    }
                  }
                  usage?: {
                    input_tokens?: number
                    output_tokens?: number
                    total_tokens?: number
                  }
                  error?: {
                    message?: string
                  }
                }

                if (event.type === 'response.output_text.delta' && event.delta) {
                  outputTokens++
                  yield { content: event.delta, done: false }
                }

                if (event.type === 'error' || event.type === 'response.error') {
                  yield {
                    content: '',
                    done: true,
                    error: {
                      type: 'server',
                      message: event.error?.message || 'OpenAI Codex stream error',
                    },
                  }
                  return
                }

                if (event.type === 'response.completed') {
                  const usage = event.response?.usage || event.usage
                  if (usage) {
                    inputTokens = usage.input_tokens ?? 0
                    outputTokens = usage.output_tokens ?? outputTokens
                  }
                }
              } catch {
                // Ignore malformed SSE chunks
              }
            }
          }
        } finally {
          reader.releaseLock()
        }

        yield {
          content: '',
          done: true,
          usage: { inputTokens, outputTokens, totalTokens: inputTokens + outputTokens },
        }
        return
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
    }

    // Build request body using shared utility (includes tools automatically)
    const body = buildOpenAIRequestBody(messages, config, { model: config.model })
    if (!body.max_tokens) body.max_tokens = config.maxTokens || 4096
    if (body.temperature === undefined) body.temperature = config.temperature ?? 0.7
    // Request usage in final streaming chunk
    body.stream_options = { include_usage: true }

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
      const error = buildHttpError(response.status, errorBody, 'OpenAI')
      error.retryAfter = parseRetryAfterShared(response.headers.get('retry-after'))
      yield { content: '', done: true, error }
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

    const toolCallBuf = new ToolCallBuffer()

    for await (const dataLines of readSSEStream(reader)) {
      for (const data of dataLines) {
        try {
          const event = JSON.parse(data) as SharedOpenAIStreamEvent
          const choice = event.choices?.[0]

          if (choice?.delta?.content) {
            yield { content: choice.delta.content }
          }

          if (choice?.delta?.tool_calls) {
            toolCallBuf.accumulate(choice.delta.tool_calls)
          }

          // Extract usage from final event
          if (event.usage) {
            yield {
              content: '',
              usage: {
                inputTokens: event.usage.prompt_tokens,
                outputTokens: event.usage.completion_tokens,
              },
            }
          }
        } catch {
          // Skip invalid JSON
        }
      }
    }

    // Emit accumulated tool calls before done signal
    yield* toolCallBuf.flush()
    yield { content: '', done: true }
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

  if (status === 400 && body.trim().length > 0) {
    const bodySnippet = body.trim().slice(0, 240)
    return `Invalid request: ${bodySnippet}`
  }

  return messages[status] || `HTTP ${status}`
}

function parseRetryAfter(header: string | null): number | undefined {
  if (!header) return undefined
  const seconds = parseInt(header, 10)
  return Number.isNaN(seconds) ? undefined : seconds
}

registerClient('openai', OpenAIClient)
