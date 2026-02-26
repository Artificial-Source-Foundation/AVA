/**
 * OpenAI-Compatible Provider Utilities
 * Shared helpers for providers using the OpenAI Chat Completions API format.
 */

import type {
  ChatMessage,
  LLMClient,
  LLMProvider,
  ProviderConfig,
  StreamDelta,
  ToolUseBlock,
} from '@ava/core-v2/llm'
import { getApiKey } from '@ava/core-v2/llm'
import { buildHttpError, parseRetryAfter } from './errors.js'
import { readSSEStream } from './sse.js'

// ─── OpenAI Stream Event ────────────────────────────────────────────────────

export interface OpenAIStreamEvent {
  id: string
  object: string
  created: number
  model: string
  choices: Array<{
    index: number
    delta: {
      role?: string
      content?: string
      tool_calls?: Array<{
        index: number
        id?: string
        type?: string
        function?: {
          name?: string
          arguments?: string
        }
      }>
    }
    finish_reason: string | null
  }>
  usage?: {
    prompt_tokens: number
    completion_tokens: number
    total_tokens: number
  }
}

// ─── Tool Conversion ────────────────────────────────────────────────────────

export function convertToolsToOpenAIFormat(tools: ProviderConfig['tools']):
  | Array<{
      type: 'function'
      function: { name: string; description: string; parameters: unknown }
    }>
  | undefined {
  if (!tools || tools.length === 0) return undefined
  return tools.map((t) => ({
    type: 'function' as const,
    function: {
      name: t.name,
      description: t.description,
      parameters: t.input_schema,
    },
  }))
}

// ─── Tool Call Buffer ───────────────────────────────────────────────────────

/**
 * Accumulates streaming tool call deltas into complete tool calls.
 * OpenAI-compatible APIs stream tool calls in fragments across multiple events.
 */
export class ToolCallBuffer {
  private buffer: Record<number, { id: string; name: string; arguments: string }> = {}

  accumulate(
    toolCalls: Array<{
      index: number
      id?: string
      type?: string
      function?: { name?: string; arguments?: string }
    }>
  ): void {
    for (const tc of toolCalls) {
      if (!this.buffer[tc.index]) {
        this.buffer[tc.index] = { id: tc.id ?? '', name: tc.function?.name ?? '', arguments: '' }
      }
      const entry = this.buffer[tc.index]!
      if (tc.id) entry.id = tc.id
      if (tc.function?.name) entry.name = tc.function.name
      if (tc.function?.arguments) entry.arguments += tc.function.arguments
    }
  }

  *flush(): Generator<StreamDelta> {
    for (const tc of Object.values(this.buffer)) {
      if (tc.id && tc.name) {
        try {
          const toolUse: ToolUseBlock = {
            type: 'tool_use',
            id: tc.id,
            name: tc.name,
            input: JSON.parse(tc.arguments || '{}'),
          }
          yield { toolUse }
        } catch {
          // Skip invalid tool call JSON
        }
      }
    }
  }
}

// ─── Request Body Builder ───────────────────────────────────────────────────

export function buildOpenAIRequestBody(
  messages: ChatMessage[],
  config: ProviderConfig,
  defaults: { model: string }
): Record<string, unknown> {
  const body: Record<string, unknown> = {
    model: config.model ?? defaults.model,
    messages: messages.map((m) => ({ role: m.role, content: m.content })),
    stream: true,
  }

  if (config.maxTokens) body.max_tokens = config.maxTokens
  if (config.temperature !== undefined) body.temperature = config.temperature

  const tools = convertToolsToOpenAIFormat(config.tools)
  if (tools) body.tools = tools

  return body
}

// ─── Provider Config ────────────────────────────────────────────────────────

export interface OpenAICompatProviderConfig {
  provider: LLMProvider
  displayName: string
  baseUrl: string
  defaultModel: string
  apiKeyHint: string
  endpoint?: string
  extractUsage?: (
    event: Record<string, unknown>
  ) => { inputTokens: number; outputTokens: number } | null
}

// ─── Client Factory ─────────────────────────────────────────────────────────

/**
 * Create an OpenAI-compatible provider client class.
 * Returns the class (not registered) — caller registers via ExtensionAPI.
 */
export function createOpenAICompatClient(
  providerConfig: OpenAICompatProviderConfig
): new () => LLMClient {
  const {
    provider,
    displayName,
    baseUrl,
    defaultModel,
    apiKeyHint,
    endpoint = '/chat/completions',
    extractUsage,
  } = providerConfig

  class OpenAICompatClient implements LLMClient {
    async *stream(
      messages: ChatMessage[],
      config: ProviderConfig,
      signal?: AbortSignal
    ): AsyncGenerator<StreamDelta, void, unknown> {
      const apiKey = await getApiKey(provider)

      if (!apiKey) {
        yield {
          done: true,
          error: {
            type: 'auth',
            message: `No ${displayName} API key configured. Set ${apiKeyHint}.`,
          },
        }
        return
      }

      const body = buildOpenAIRequestBody(messages, config, { model: defaultModel })

      let response: Response
      try {
        response = await fetch(`${baseUrl}${endpoint}`, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            Authorization: `Bearer ${apiKey}`,
          },
          body: JSON.stringify(body),
          signal,
        })
      } catch (err) {
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
        const text = await response.text()
        const error = buildHttpError(response.status, text, displayName)
        error.retryAfter = parseRetryAfter(response.headers.get('retry-after'))
        yield { done: true, error }
        return
      }

      const reader = response.body?.getReader()
      if (!reader) {
        yield { done: true, error: { type: 'api', message: 'No response body' } }
        return
      }

      const toolCallBuf = new ToolCallBuffer()

      for await (const dataLines of readSSEStream(reader)) {
        for (const data of dataLines) {
          try {
            const event = JSON.parse(data) as OpenAIStreamEvent
            const choice = event.choices[0]

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

            if (extractUsage) {
              const usage = extractUsage(event as unknown as Record<string, unknown>)
              if (usage) yield { usage }
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

  return OpenAICompatClient
}
