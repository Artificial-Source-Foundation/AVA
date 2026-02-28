/**
 * Anthropic Provider Client
 * Direct integration with Claude API.
 * https://docs.anthropic.com/en/api/messages
 */

import type {
  ChatMessage,
  LLMClient,
  MessageContent,
  ProviderConfig,
  StreamDelta,
  TextBlock,
  ToolUseBlock,
} from '@ava/core-v2/llm'
import { getAuth } from '@ava/core-v2/llm'

const BASE_URL = 'https://api.anthropic.com/v1'
const API_VERSION = '2023-06-01'

// ─── Anthropic SSE Event Types ──────────────────────────────────────────────

interface AnthropicStreamEvent {
  type: string
  message: { usage: { input_tokens: number } }
  content_block: { type: string; id: string; name: string }
  delta: { type: string; text?: string; partial_json?: string; thinking?: string }
  usage: { output_tokens: number }
  error: { message: string }
}

// ─── Helpers ────────────────────────────────────────────────────────────────

/** Extract plain text from MessageContent (string or ContentBlock[]). */
function extractTextContent(content: MessageContent): string {
  if (typeof content === 'string') return content
  return content
    .filter((b): b is TextBlock => b.type === 'text')
    .map((b) => b.text)
    .join('\n')
}

// ─── Client Implementation ──────────────────────────────────────────────────

export class AnthropicClient implements LLMClient {
  async *stream(
    messages: ChatMessage[],
    config: ProviderConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown> {
    const auth = await getAuth('anthropic')

    if (!auth) {
      yield {
        done: true,
        error: {
          type: 'auth',
          message:
            'No Anthropic authentication configured. Set AVA_ANTHROPIC_API_KEY or use `ava auth anthropic` for OAuth.',
        },
      }
      return
    }

    // Separate system message from conversation
    const systemMessage = messages.find((m) => m.role === 'system')
    const conversationMessages = messages
      .filter((m) => m.role !== 'system')
      .map((m) => ({ role: m.role as 'user' | 'assistant', content: m.content }))

    const body: Record<string, unknown> = {
      model: config.model,
      messages: conversationMessages,
      max_tokens: config.maxTokens || 4096,
      stream: true,
    }

    if (systemMessage) body.system = extractTextContent(systemMessage.content)

    // Extended thinking (must come before temperature — thinking disables temperature)
    if (config.thinking?.enabled) {
      body.thinking = { type: 'enabled', budget_tokens: 10000 }
    }

    // Anthropic requires omitting temperature when thinking is enabled
    if (config.temperature !== undefined && !config.thinking?.enabled) {
      body.temperature = config.temperature
    }

    if (config.tools && config.tools.length > 0) body.tools = config.tools

    // Build headers based on auth type
    const headers: Record<string, string> = {
      'Content-Type': 'application/json',
      'anthropic-version': API_VERSION,
      'anthropic-dangerous-direct-browser-access': 'true',
    }

    if (auth.type === 'oauth') {
      headers.Authorization = `Bearer ${auth.token}`
      headers['anthropic-beta'] = 'claude-pro-2025-01-01'
    } else {
      headers['x-api-key'] = auth.token
    }

    let response: Response
    try {
      response = await fetch(`${BASE_URL}/messages`, {
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
          message: extractError(response.status, errorBody),
          status: response.status,
          retryAfter: parseRetry(response.headers.get('retry-after')),
        },
      }
      return
    }

    const reader = response.body?.getReader()
    if (!reader) {
      yield { done: true, error: { type: 'unknown', message: 'No response body' } }
      return
    }

    yield* this.parseStream(reader)
  }

  private async *parseStream(
    reader: ReadableStreamDefaultReader<Uint8Array>
  ): AsyncGenerator<StreamDelta, void, unknown> {
    const decoder = new TextDecoder()
    let buffer = ''
    let inputTokens = 0
    let outputTokens = 0
    let currentToolUse: Partial<ToolUseBlock> | null = null
    let toolInputBuffer = ''

    try {
      while (true) {
        const { done, value } = await reader.read()
        if (done) break

        buffer += decoder.decode(value, { stream: true })
        const lines = buffer.split('\n')
        buffer = lines.pop() || ''

        for (const line of lines) {
          if (!line.trim() || line.startsWith('event:') || !line.startsWith('data: ')) continue
          const data = line.slice(6).trim()

          try {
            const event = JSON.parse(data) as AnthropicStreamEvent

            switch (event.type) {
              case 'message_start':
                inputTokens = event.message.usage.input_tokens
                break

              case 'content_block_start':
                if (event.content_block.type === 'tool_use') {
                  currentToolUse = {
                    type: 'tool_use',
                    id: event.content_block.id,
                    name: event.content_block.name,
                  }
                  toolInputBuffer = ''
                }
                // 'thinking' block start — deltas handled in content_block_delta
                break

              case 'content_block_delta':
                if (event.delta.type === 'text_delta' && event.delta.text) {
                  yield { content: event.delta.text }
                } else if (event.delta.type === 'input_json_delta' && event.delta.partial_json) {
                  toolInputBuffer += event.delta.partial_json
                } else if (event.delta.type === 'thinking_delta' && event.delta.thinking) {
                  yield { thinking: event.delta.thinking }
                }
                break

              case 'content_block_stop':
                if (currentToolUse) {
                  try {
                    const input = toolInputBuffer ? JSON.parse(toolInputBuffer) : {}
                    yield {
                      toolUse: {
                        type: 'tool_use',
                        id: currentToolUse.id!,
                        name: currentToolUse.name!,
                        input,
                      },
                    }
                  } catch {
                    // Skip invalid tool input JSON
                  }
                  currentToolUse = null
                  toolInputBuffer = ''
                }
                break

              case 'message_delta':
                outputTokens = event.usage.output_tokens
                break

              case 'message_stop':
                yield {
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
                  done: true,
                  error: { type: 'server', message: event.error.message },
                }
                return
            }
          } catch {
            // Skip malformed JSON
          }
        }
      }

      yield {
        done: true,
        usage: { inputTokens, outputTokens, totalTokens: inputTokens + outputTokens },
      }
    } finally {
      reader.releaseLock()
    }
  }
}

// ─── Helpers ────────────────────────────────────────────────────────────────

function classifyError(status: number): 'rate_limit' | 'auth' | 'server' | 'unknown' {
  if (status === 401 || status === 403) return 'auth'
  if (status === 429) return 'rate_limit'
  if (status === 529) return 'server'
  if (status >= 500) return 'server'
  return 'unknown'
}

function extractError(status: number, body: string): string {
  try {
    const parsed = JSON.parse(body)
    if (parsed.error?.message) return parsed.error.message
  } catch {
    // Use defaults
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

function parseRetry(header: string | null): number | undefined {
  if (!header) return undefined
  const seconds = parseInt(header, 10)
  return Number.isNaN(seconds) ? undefined : seconds
}
