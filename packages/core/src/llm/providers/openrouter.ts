/**
 * OpenRouter Provider Client
 * Gateway for 100+ LLM models via unified OpenAI-compatible API
 * https://openrouter.ai/docs
 */

import type { ChatMessage, LLMProvider, ProviderConfig, StreamDelta } from '../../types/llm.js'
import { getAuth, type LLMClient, registerClient } from '../client.js'
import { buildHttpError, parseRetryAfter } from '../utils/errors.js'
import {
  buildOpenAIRequestBody,
  type OpenAIStreamEvent,
  ToolCallBuffer,
} from '../utils/openai-compat.js'
import { readSSEStream } from '../utils/sse.js'

const BASE_URL = 'https://openrouter.ai/api/v1'
const PROVIDER: LLMProvider = 'openrouter'

/**
 * OpenRouter client implementation
 * Uses OpenAI-compatible chat completions API with custom auth (OAuth + API key)
 * and OpenRouter-specific headers.
 */
class OpenRouterClient implements LLMClient {
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
          message: 'No OpenRouter API key configured. Set AVA_OPENROUTER_API_KEY.',
        },
      }
      return
    }

    // Build request body using shared utility (includes tools automatically)
    const body = buildOpenAIRequestBody(messages, config, { model: config.model })

    // OpenRouter defaults
    if (!body.max_tokens) body.max_tokens = 4096
    if (body.temperature === undefined) body.temperature = 0.7

    // Make request
    let response: Response
    try {
      response = await fetch(`${BASE_URL}/chat/completions`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          Authorization: `Bearer ${auth.token}`,
          'HTTP-Referer': 'https://ava.app',
          'X-Title': 'AVA',
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
      const error = buildHttpError(response.status, errorBody, 'OpenRouter')
      error.retryAfter = parseRetryAfter(response.headers.get('retry-after'))
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
          const event = JSON.parse(data) as OpenAIStreamEvent
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

    // Emit accumulated tool calls
    yield* toolCallBuf.flush()
    yield { content: '', done: true }
  }
}

registerClient('openrouter', OpenRouterClient)
