/**
 * OpenAI-Compatible Provider Utilities
 * Shared helpers for providers using the OpenAI Chat Completions API format
 */

import type {
  ChatMessage,
  LLMProvider,
  ProviderConfig,
  StreamDelta,
  ToolUseBlock,
} from '../../types/llm.js'
import { getApiKey, type LLMClient, registerClient } from '../client.js'
import { buildHttpError, parseRetryAfter } from './errors.js'
import { readSSEStream } from './sse.js'

// ============================================================================
// OpenAI-Compatible Stream Event Type
// ============================================================================

/**
 * Standard OpenAI-compatible streaming event shape
 * Used by DeepSeek, xAI, Together, Groq, Mistral, and others
 */
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

// ============================================================================
// Tool Conversion
// ============================================================================

/**
 * Convert AVA tool definitions to OpenAI function calling format
 */
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

// ============================================================================
// Tool Call Buffer
// ============================================================================

/**
 * Accumulates streaming tool call deltas into complete tool calls.
 * OpenAI-compatible APIs stream tool calls in fragments across multiple events.
 */
export class ToolCallBuffer {
  private buffer: Record<number, { id: string; name: string; arguments: string }> = {}

  /**
   * Process a tool call delta from a streaming event
   */
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
        this.buffer[tc.index] = {
          id: tc.id ?? '',
          name: tc.function?.name ?? '',
          arguments: '',
        }
      }
      if (tc.id) this.buffer[tc.index].id = tc.id
      if (tc.function?.name) this.buffer[tc.index].name = tc.function.name
      if (tc.function?.arguments) this.buffer[tc.index].arguments += tc.function.arguments
    }
  }

  /**
   * Emit complete tool calls as StreamDelta yields
   */
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
          yield { content: '', toolUse }
        } catch {
          // Skip invalid tool call JSON
        }
      }
    }
  }
}

// ============================================================================
// Request Body Builder
// ============================================================================

/**
 * Build an OpenAI-compatible chat completions request body
 */
export function buildOpenAIRequestBody(
  messages: ChatMessage[],
  config: ProviderConfig,
  defaults: { model: string }
): Record<string, unknown> {
  const body: Record<string, unknown> = {
    model: config.model ?? defaults.model,
    messages: messages.map((m) => ({
      role: m.role,
      content: m.content,
    })),
    stream: true,
  }

  if (config.maxTokens) {
    body.max_tokens = config.maxTokens
  }

  if (config.temperature !== undefined) {
    body.temperature = config.temperature
  }

  const tools = convertToolsToOpenAIFormat(config.tools)
  if (tools) {
    body.tools = tools
  }

  return body
}

// ============================================================================
// Provider Configuration
// ============================================================================

/**
 * Configuration for creating an OpenAI-compatible provider client
 */
export interface OpenAICompatProviderConfig {
  /** Provider identifier */
  provider: LLMProvider
  /** Display name for error messages */
  displayName: string
  /** Base URL for the API */
  baseUrl: string
  /** Default model if none specified */
  defaultModel: string
  /** API key environment variable hint */
  apiKeyHint: string
  /** Custom endpoint path (default: '/chat/completions') */
  endpoint?: string
  /** Custom usage extractor for providers with non-standard usage reporting */
  extractUsage?: (
    event: Record<string, unknown>
  ) => { inputTokens: number; outputTokens: number } | null
}

// ============================================================================
// OpenAI-Compatible Client Factory
// ============================================================================

/**
 * Create and register an OpenAI-compatible provider client.
 * Eliminates ~200 lines of duplicated code per provider.
 */
export function createOpenAICompatClient(providerConfig: OpenAICompatProviderConfig): void {
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
          content: '',
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
          content: '',
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
        yield { content: '', done: true, error }
        return
      }

      const reader = response.body?.getReader()
      if (!reader) {
        yield { content: '', done: true, error: { type: 'api', message: 'No response body' } }
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

            // Standard usage field
            if (event.usage) {
              yield {
                content: '',
                usage: {
                  inputTokens: event.usage.prompt_tokens,
                  outputTokens: event.usage.completion_tokens,
                },
              }
            }

            // Provider-specific usage (e.g., Groq's x_groq.usage)
            if (extractUsage) {
              const usage = extractUsage(event as unknown as Record<string, unknown>)
              if (usage) {
                yield { content: '', usage }
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

  registerClient(provider, OpenAICompatClient)
}
