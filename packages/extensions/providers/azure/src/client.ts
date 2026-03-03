/**
 * Azure OpenAI Provider Client
 *
 * Connects to Azure's hosted OpenAI models. Unlike standard OpenAI:
 * - Auth uses `api-key` header instead of `Authorization: Bearer`
 * - Endpoint includes resource name, deployment ID, and API version
 * - Model is implicit from the deployment — not sent in the request body
 *
 * https://learn.microsoft.com/en-us/azure/ai-services/openai/reference
 */

import type { ChatMessage, LLMClient, ProviderConfig, StreamDelta } from '@ava/core-v2/llm'
import { buildHttpError, parseRetryAfter } from '../../_shared/src/errors.js'
import {
  buildOpenAIRequestBody,
  type OpenAIStreamEvent,
  ToolCallBuffer,
} from '../../_shared/src/openai-compat.js'
import { readSSEStream } from '../../_shared/src/sse.js'

const DEFAULT_API_VERSION = '2024-10-21'

export interface AzureOpenAIConfig {
  /** Azure resource endpoint, e.g. https://my-resource.openai.azure.com */
  endpoint: string
  /** Azure API key */
  apiKey: string
  /** Deployment name (maps to a specific model) */
  deploymentId: string
  /** API version (defaults to 2024-10-21) */
  apiVersion?: string
}

function buildAzureUrl(config: AzureOpenAIConfig): string {
  const base = config.endpoint.replace(/\/+$/, '')
  const version = config.apiVersion ?? DEFAULT_API_VERSION
  return `${base}/openai/deployments/${config.deploymentId}/chat/completions?api-version=${version}`
}

export class AzureOpenAIClient implements LLMClient {
  private readonly config: AzureOpenAIConfig

  constructor(config: AzureOpenAIConfig) {
    this.config = config
  }

  async *stream(
    messages: ChatMessage[],
    config: ProviderConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown> {
    const { apiKey, endpoint, deploymentId } = this.config

    if (!apiKey || !endpoint || !deploymentId) {
      yield {
        done: true,
        error: {
          type: 'auth',
          message:
            'Azure OpenAI not configured. Set AZURE_OPENAI_ENDPOINT, AZURE_OPENAI_API_KEY, and AZURE_OPENAI_DEPLOYMENT_ID.',
        },
      }
      return
    }

    // Build body — Azure infers model from the deployment, but we still pass
    // it for providers that inspect the body (e.g. token counting).
    const body = buildOpenAIRequestBody(messages, config, {
      model: deploymentId,
    })
    if (!body.max_tokens) body.max_tokens = 4096

    const url = buildAzureUrl(this.config)

    let response: Response
    try {
      response = await fetch(url, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'api-key': apiKey,
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
      const error = buildHttpError(response.status, errorBody, 'Azure OpenAI')
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
                cacheReadTokens: event.usage.prompt_tokens_details?.cached_tokens || undefined,
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
