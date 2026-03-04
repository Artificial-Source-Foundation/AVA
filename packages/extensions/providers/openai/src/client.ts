/**
 * OpenAI Provider Client
 * Direct integration with OpenAI API.
 * Supports both API key (Chat Completions) and OAuth (Codex/Responses API).
 * https://platform.openai.com/docs/api-reference
 */

import type { ChatMessage, LLMClient, ProviderConfig, StreamDelta } from '@ava/core-v2/llm'
import { getAuth } from '@ava/core-v2/llm'
import { buildHttpError, parseRetryAfter } from '../../_shared/src/errors.js'
import {
  buildOpenAIRequestBody,
  type OpenAIStreamEvent,
  ToolCallBuffer,
} from '../../_shared/src/openai-compat.js'
import { readSSEStream } from '../../_shared/src/sse.js'
import { buildResponsesRequestBody } from './responses-body.js'

const BASE_URL = 'https://api.openai.com/v1'
const CODEX_ENDPOINT = 'https://chatgpt.com/backend-api/codex/responses'
const DEV_CODEX_PROXY = '/__chatgpt_proxy/backend-api/codex/responses'

// ─── Helpers ────────────────────────────────────────────────────────────────

function resolveOAuthEndpoint(): string {
  const location = (
    globalThis as { location?: { hostname?: string; port?: string; protocol?: string } }
  ).location

  if (
    location?.protocol?.startsWith('http') &&
    (location.hostname === 'localhost' || location.hostname === '127.0.0.1') &&
    location.port === '1420'
  ) {
    return DEV_CODEX_PROXY
  }

  return CODEX_ENDPOINT
}

function classifyError(status: number): 'rate_limit' | 'auth' | 'server' | 'unknown' {
  if (status === 401 || status === 403) return 'auth'
  if (status === 429) return 'rate_limit'
  if (status >= 500) return 'server'
  return 'unknown'
}

function extractErrorMessage(status: number, body: string): string {
  // Log full error body for debugging
  if (body) console.error(`[AVA:OpenAI] HTTP ${status} error body:`, body.slice(0, 1000))
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
    503: 'Service overloaded',
  }
  return messages[status] || `HTTP ${status}`
}

function parseRetryAfterHeader(header: string | null): number | undefined {
  if (!header) return undefined
  const seconds = parseInt(header, 10)
  return Number.isNaN(seconds) ? undefined : seconds
}

/** Models that support extended (24h) prompt cache retention. */
const EXTENDED_CACHE_PATTERNS = ['gpt-4.1', 'gpt-5', 'o3', 'o4']

function supportsExtendedCacheRetention(model: string): boolean {
  return EXTENDED_CACHE_PATTERNS.some((p) => model.toLowerCase().includes(p))
}

/** Models that should use the Responses API instead of Chat Completions. */
const RESPONSES_API_PATTERNS = ['gpt-5', 'o3-', 'o4-', 'codex']

/**
 * Check if a model should be routed to the Responses API.
 * Returns true for GPT-5+, o3, o4, and Codex models.
 */
export function shouldUseResponsesAPI(model: string): boolean {
  const lower = model.toLowerCase()
  return RESPONSES_API_PATTERNS.some((p) => lower.includes(p))
}

// ─── Client ─────────────────────────────────────────────────────────────────

export class OpenAIClient implements LLMClient {
  async *stream(
    messages: ChatMessage[],
    config: ProviderConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown> {
    const auth = await getAuth('openai')

    if (!auth) {
      yield {
        done: true,
        error: {
          type: 'auth',
          message:
            'No OpenAI authentication configured. Set AVA_OPENAI_API_KEY or use `ava auth openai` for OAuth.',
        },
      }
      return
    }

    if (auth.type === 'oauth') {
      yield* this.streamOAuth(messages, config, auth.token, auth.accountId, signal)
    } else {
      yield* this.streamApiKey(messages, config, auth.token, signal)
    }
  }

  // ─── API Key Path (Chat Completions or Responses API) ──────────────────

  private async *streamApiKey(
    messages: ChatMessage[],
    config: ProviderConfig,
    apiKey: string,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown> {
    const model = config.model ?? 'gpt-4o'
    const useResponses = shouldUseResponsesAPI(model)

    const headers: Record<string, string> = {
      'Content-Type': 'application/json',
      Authorization: `Bearer ${apiKey}`,
    }

    const endpoint = useResponses ? `${BASE_URL}/responses` : `${BASE_URL}/chat/completions`

    let bodyJson: string
    if (useResponses) {
      const respBody = buildResponsesRequestBody(messages, { ...config, model })
      console.debug(
        `[AVA:OpenAI] API Key Responses API — model=${model}, tools=${respBody.tools?.length ?? 0}, tool_choice=${respBody.tool_choice ?? 'default'}`
      )
      if (supportsExtendedCacheRetention(model)) {
        ;(respBody as unknown as Record<string, unknown>).prompt_cache_retention = '24h'
      }
      bodyJson = JSON.stringify(respBody)
    } else {
      const chatBody = buildOpenAIRequestBody(messages, config, { model: 'gpt-4o' })
      if (!chatBody.max_tokens) chatBody.max_tokens = 4096
      if (supportsExtendedCacheRetention(model)) {
        chatBody.prompt_cache_retention = '24h'
      }
      bodyJson = JSON.stringify(chatBody)
    }

    let response: Response
    try {
      response = await fetch(endpoint, {
        method: 'POST',
        headers,
        body: bodyJson,
        signal,
      })
    } catch (err) {
      if (err instanceof Error && err.name === 'AbortError') return
      yield {
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
      error.retryAfter = parseRetryAfter(response.headers.get('retry-after'))
      yield { done: true, error }
      return
    }

    // Route to Responses API SSE parser for newer models
    if (useResponses) {
      const reader = response.body?.getReader()
      if (!reader) {
        yield { done: true, error: { type: 'unknown', message: 'No response body' } }
        return
      }
      yield* this.parseResponsesStream(reader)
      return
    }

    const reader = response.body?.getReader()
    if (!reader) {
      yield { done: true, error: { type: 'unknown', message: 'No response body' } }
      return
    }

    const toolCallBuf = new ToolCallBuffer()

    for await (const dataLines of readSSEStream(reader)) {
      for (const data of dataLines) {
        try {
          const event = JSON.parse(data) as OpenAIStreamEvent
          const choice = event.choices?.[0]

          if (choice?.delta?.content) {
            yield { content: choice.delta.content }
          }

          if (choice?.delta?.tool_calls) {
            toolCallBuf.accumulate(choice.delta.tool_calls)
          }

          if (event.usage) {
            const cachedTokens = (
              event.usage as {
                prompt_tokens_details?: { cached_tokens?: number }
              }
            ).prompt_tokens_details?.cached_tokens
            yield {
              usage: {
                inputTokens: event.usage.prompt_tokens,
                outputTokens: event.usage.completion_tokens,
                cacheReadTokens: cachedTokens || undefined,
              },
            }
          }
        } catch {
          // Skip invalid JSON
        }
      }
    }

    yield* toolCallBuf.flush()
    yield { done: true }
  }

  // ─── OAuth Path (Codex / Responses API) ─────────────────────────────────

  private async *streamOAuth(
    messages: ChatMessage[],
    config: ProviderConfig,
    token: string,
    accountId: string | undefined,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown> {
    const endpoint = resolveOAuthEndpoint()
    const headers: Record<string, string> = {
      'Content-Type': 'application/json',
      Authorization: `Bearer ${token}`,
      originator: 'ava',
    }

    if (accountId) {
      headers['ChatGPT-Account-Id'] = accountId
    }

    // Use shared builder for consistent tool/message handling
    const body = buildResponsesRequestBody(messages, config) as unknown as Record<string, unknown>

    // Debug: log tool count so we can verify tools are being sent
    const toolCount = (body.tools as unknown[] | undefined)?.length ?? 0
    console.debug(
      `[AVA:OpenAI] OAuth Responses API — model=${config.model}, tools=${toolCount}, tool_choice=${body.tool_choice ?? 'default'}`
    )

    let response: Response
    try {
      response = await fetch(endpoint, {
        method: 'POST',
        headers,
        body: JSON.stringify(body),
        signal,
      })
    } catch (err) {
      if (err instanceof Error && err.name === 'AbortError') return
      yield {
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
        done: true,
        error: {
          type: classifyError(response.status),
          message: extractErrorMessage(response.status, errorBody),
          status: response.status,
          retryAfter: parseRetryAfterHeader(response.headers.get('retry-after')),
        },
      }
      return
    }

    const reader = response.body?.getReader()
    if (!reader) {
      yield { done: true, error: { type: 'unknown', message: 'No response body' } }
      return
    }

    yield* this.parseResponsesStream(reader)
  }

  // ─── Responses API SSE Parser ───────────────────────────────────────────

  private async *parseResponsesStream(
    reader: ReadableStreamDefaultReader<Uint8Array>
  ): AsyncGenerator<StreamDelta, void, unknown> {
    const decoder = new TextDecoder()
    let buffer = ''
    let inputTokens = 0
    let outputTokens = 0
    let cacheReadTokens = 0
    const fnCalls = new Map<string, { id: string; name: string; args: string }>()

    try {
      while (true) {
        const { done, value } = await reader.read()
        if (done) break

        buffer += decoder.decode(value, { stream: true })
        const lines = buffer.split('\n')
        buffer = lines.pop() || ''

        for (const line of lines) {
          if (!line.trim() || line.startsWith(':') || !line.startsWith('data: ')) continue

          const data = line.slice(6).trim()
          if (data === '[DONE]') {
            yield {
              done: true,
              usage: {
                inputTokens,
                outputTokens,
                totalTokens: inputTokens + outputTokens,
                cacheReadTokens: cacheReadTokens || undefined,
              },
            }
            return
          }

          try {
            const event = JSON.parse(data) as Record<string, unknown>
            const type = event.type as string

            // Text content delta
            if (type === 'response.output_text.delta' && event.delta) {
              yield { content: event.delta as string }
            }

            // Function call started
            if (type === 'response.output_item.added') {
              const item = event.item as Record<string, unknown> | undefined
              if (item?.type === 'function_call') {
                const id = (item.call_id as string) || (item.id as string) || `fn_${Date.now()}`
                fnCalls.set(id, { id, name: (item.name as string) ?? '', args: '' })
              }
            }

            // Function call arguments streaming
            if (type === 'response.function_call_arguments.delta' && event.delta) {
              const entry = fnCalls.get(event.item_id as string) || [...fnCalls.values()].pop()
              if (entry) {
                entry.args += event.delta as string
              }
            }

            // Function call arguments complete — yield as toolUse
            if (type === 'response.function_call_arguments.done') {
              const entry = fnCalls.get(event.item_id as string) || [...fnCalls.values()].pop()
              if (entry) {
                let parsed: Record<string, unknown> = {}
                try {
                  parsed = JSON.parse(entry.args || (event.arguments as string) || '{}')
                } catch {
                  parsed = { _raw: entry.args || (event.arguments as string) }
                }
                yield {
                  toolUse: {
                    type: 'tool_use',
                    id: entry.id,
                    name: entry.name || 'unknown',
                    input: parsed,
                  },
                }
              }
            }

            // Error
            if (type === 'error' || type === 'response.error') {
              const err = event.error as Record<string, unknown> | undefined
              yield {
                done: true,
                error: {
                  type: 'server',
                  message: (err?.message as string) || 'OpenAI Codex stream error',
                },
              }
              return
            }

            // Stream complete — extract usage
            if (type === 'response.completed') {
              const resp = event.response as Record<string, unknown> | undefined
              const usage = (resp?.usage ?? event.usage) as Record<string, unknown> | undefined
              if (usage) {
                inputTokens = (usage.input_tokens as number) ?? 0
                outputTokens = (usage.output_tokens as number) ?? outputTokens
                const details = usage.input_tokens_details as { cached_tokens?: number } | undefined
                cacheReadTokens = details?.cached_tokens ?? 0
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
      done: true,
      usage: {
        inputTokens,
        outputTokens,
        totalTokens: inputTokens + outputTokens,
        cacheReadTokens: cacheReadTokens || undefined,
      },
    }
  }
}
