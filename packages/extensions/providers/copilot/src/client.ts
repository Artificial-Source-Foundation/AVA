/**
 * GitHub Copilot Provider Client
 *
 * Uses the OpenAI-compatible Copilot API at https://api.githubcopilot.com.
 * Auth via OAuth device code flow (not API key), with a required
 * Copilot-Integration-Id header.
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

const BASE_URL = 'https://api.githubcopilot.com'

/** Strip provider prefix so the API receives the upstream model ID. */
function resolveModel(model: string): string {
  if (model.startsWith('copilot-')) {
    return model.slice('copilot-'.length)
  }
  return model
}

export class CopilotClient implements LLMClient {
  async *stream(
    messages: ChatMessage[],
    config: ProviderConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown> {
    const auth = await getAuth('copilot')

    if (!auth) {
      yield {
        done: true,
        error: {
          type: 'auth',
          message:
            'No GitHub Copilot authentication configured. Sign in via Settings \u2192 Providers \u2192 GitHub Copilot.',
        },
      }
      return
    }

    const body = buildOpenAIRequestBody(messages, config, { model: 'gpt-4o' })
    body.model = resolveModel(body.model as string)
    if (!body.max_tokens) body.max_tokens = 4096

    let response: Response
    try {
      response = await fetch(`${BASE_URL}/chat/completions`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          Authorization: `Bearer ${auth.token}`,
          'Copilot-Integration-Id': 'vscode-chat',
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
          message: `Network error: ${err instanceof Error ? err.message : String(err)}`,
        },
      }
      return
    }

    if (!response.ok) {
      const errorBody = await response.text().catch(() => '')
      const error = buildHttpError(response.status, errorBody, 'GitHub Copilot')
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
