/**
 * OpenRouter Provider Client
 * Gateway for 100+ LLM models via unified OpenAI-compatible API.
 * https://openrouter.ai/docs
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
import { addCacheControlMarkers } from './cache.js'

const BASE_URL = 'https://openrouter.ai/api/v1'

export class OpenRouterClient implements LLMClient {
  async *stream(
    messages: ChatMessage[],
    config: ProviderConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown> {
    const auth = await getAuth('openrouter')

    if (!auth) {
      yield {
        done: true,
        error: {
          type: 'auth',
          message: 'No OpenRouter API key configured. Set AVA_OPENROUTER_API_KEY.',
        },
      }
      return
    }

    const body = buildOpenAIRequestBody(messages, config, { model: config.model })

    // Apply prompt cache markers to the converted body messages.
    // OpenRouter passes cache_control through to Anthropic models.
    body.messages = addCacheControlMarkers(
      body.messages as Array<{
        role: string
        content: string | Array<Record<string, unknown>> | null
      }>
    )

    if (!body.max_tokens) body.max_tokens = 4096
    if (body.temperature === undefined) body.temperature = 0.7

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
      const error = buildHttpError(response.status, errorBody, 'OpenRouter')
      error.retryAfter = parseRetryAfter(response.headers.get('retry-after'))
      yield { done: true, error }
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
}
