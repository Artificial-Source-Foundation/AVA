/**
 * OpenAI Provider Client
 * Direct integration with OpenAI Chat Completions API.
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

const BASE_URL = 'https://api.openai.com/v1'

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

    const headers: Record<string, string> = {
      'Content-Type': 'application/json',
      Authorization: `Bearer ${auth.token}`,
    }

    const body = buildOpenAIRequestBody(messages, config, { model: 'gpt-4o' })
    if (!body.max_tokens) body.max_tokens = 4096

    let response: Response
    try {
      response = await fetch(`${BASE_URL}/chat/completions`, {
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
      const error = buildHttpError(response.status, errorBody, 'OpenAI')
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
            yield {
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

    yield* toolCallBuf.flush()
    yield { done: true }
  }
}
