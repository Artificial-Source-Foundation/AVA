/**
 * OpenAI-Compatible Provider Utilities
 * Shared helpers for providers using the OpenAI Chat Completions API format.
 */

import type {
  ChatMessage,
  ImageBlock,
  LLMClient,
  LLMProvider,
  MessageContent,
  ProviderConfig,
  StreamDelta,
  TextBlock,
  ToolResultBlock,
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
    prompt_tokens_details?: { cached_tokens?: number }
  }
}

// ─── Tool Conversion ────────────────────────────────────────────────────────

/**
 * Convert tool definitions to OpenAI function-calling format.
 * Adds `cache_control: { type: 'ephemeral' }` on the last tool so providers
 * that support prompt caching (Anthropic via OpenRouter, OpenAI, Azure, etc.)
 * can cache the full tool prefix across turns. Providers that don't recognize
 * the field simply ignore it.
 */
export function convertToolsToOpenAIFormat(tools: ProviderConfig['tools']):
  | Array<{
      type: 'function'
      function: { name: string; description: string; parameters: unknown }
      cache_control?: { type: 'ephemeral' }
    }>
  | undefined {
  if (!tools || tools.length === 0) return undefined
  const lastIndex = tools.length - 1
  return tools.map((t, i) => ({
    type: 'function' as const,
    function: {
      name: t.name,
      description: t.description,
      parameters: t.input_schema,
    },
    ...(i === lastIndex ? { cache_control: { type: 'ephemeral' as const } } : {}),
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

// ─── Message Conversion ─────────────────────────────────────────────────────

interface OpenAIImageContent {
  type: 'image_url'
  image_url: { url: string }
}

interface OpenAITextContent {
  type: 'text'
  text: string
}

type OpenAIContentPart = OpenAITextContent | OpenAIImageContent

interface OpenAIMessage {
  role: string
  content: string | OpenAIContentPart[] | null
  tool_calls?: Array<{
    id: string
    type: 'function'
    function: { name: string; arguments: string }
  }>
  tool_call_id?: string
}

const VISION_MODEL_PATTERNS = [
  'gpt-4o',
  'gpt-4.1',
  'o1',
  'o3',
  'claude-3',
  'gemini-1.5',
  'gemini-2',
  'llava',
] as const

const NON_VISION_MODEL_PATTERNS = ['embedding', 'whisper', 'tts', 'gpt-3.5'] as const

export function isVisionCapable(model: string): boolean {
  const normalized = model.toLowerCase()

  if (NON_VISION_MODEL_PATTERNS.some((pattern) => normalized.includes(pattern))) {
    return false
  }

  return VISION_MODEL_PATTERNS.some((pattern) => normalized.includes(pattern))
}

/** Convert an ImageBlock to OpenAI image_url content part. */
function convertImageBlock(block: ImageBlock): OpenAIImageContent {
  const url =
    block.source.type === 'url'
      ? block.source.data
      : `data:${block.source.media_type};base64,${block.source.data}`
  return { type: 'image_url', image_url: { url } }
}

/** Extract plain text from MessageContent. */
function extractText(content: MessageContent): string {
  if (typeof content === 'string') return content
  return content
    .filter((b): b is TextBlock => b.type === 'text')
    .map((b) => b.text)
    .join('\n')
}

/**
 * Convert structured ChatMessages to OpenAI API format.
 * - Assistant with tool_use blocks → { role: 'assistant', content, tool_calls }
 * - User with tool_result blocks → separate { role: 'tool', tool_call_id, content } messages
 * - Plain string messages → pass through unchanged
 */
export function convertMessagesToOpenAI(messages: ChatMessage[]): OpenAIMessage[] {
  const result: OpenAIMessage[] = []

  for (const msg of messages) {
    // Plain string content — pass through
    if (typeof msg.content === 'string') {
      result.push({ role: msg.role, content: msg.content })
      continue
    }

    const blocks = msg.content

    // Assistant with tool_use blocks
    if (msg.role === 'assistant') {
      const textParts = blocks.filter((b): b is TextBlock => b.type === 'text').map((b) => b.text)
      const toolUseBlocks = blocks.filter((b): b is ToolUseBlock => b.type === 'tool_use')

      const assistantMsg: OpenAIMessage = {
        role: 'assistant',
        content: textParts.length > 0 ? textParts.join('\n') : null,
      }

      if (toolUseBlocks.length > 0) {
        assistantMsg.tool_calls = toolUseBlocks.map((tc) => ({
          id: tc.id,
          type: 'function' as const,
          function: {
            name: tc.name,
            arguments: JSON.stringify(tc.input),
          },
        }))
      }

      result.push(assistantMsg)
      continue
    }

    // User with tool_result, text, and/or image blocks
    if (msg.role === 'user') {
      const toolResults = blocks.filter((b): b is ToolResultBlock => b.type === 'tool_result')
      const textParts = blocks.filter((b): b is TextBlock => b.type === 'text')
      const imageBlocks = blocks.filter((b): b is ImageBlock => b.type === 'image')

      // Emit tool result messages first
      for (const tr of toolResults) {
        result.push({
          role: 'tool',
          content: tr.content,
          tool_call_id: tr.tool_use_id,
        })
      }

      // Build user message with text and/or images
      const contentParts: OpenAIContentPart[] = []
      for (const tp of textParts) {
        contentParts.push({ type: 'text', text: tp.text })
      }
      for (const img of imageBlocks) {
        contentParts.push(convertImageBlock(img))
      }

      if (contentParts.length > 0) {
        // Use simple string when only text parts, array when images present
        if (imageBlocks.length === 0 && textParts.length > 0) {
          result.push({ role: 'user', content: textParts.map((b) => b.text).join('\n') })
        } else {
          result.push({ role: 'user', content: contentParts })
        }
      }
      continue
    }

    // System or other — extract text
    result.push({ role: msg.role, content: extractText(msg.content) })
  }

  return result
}

// ─── Message Transforms (re-exported from transforms.ts) ────────────────────

export { enforceAlternatingRoles, filterEmptyContentBlocks } from './transforms.js'

// ─── Request Body Builder ───────────────────────────────────────────────────

export function buildOpenAIRequestBody(
  messages: ChatMessage[],
  config: ProviderConfig,
  defaults: { model: string }
): Record<string, unknown> {
  const body: Record<string, unknown> = {
    model: config.model ?? defaults.model,
    messages: convertMessagesToOpenAI(messages),
    stream: true,
  }

  if (config.maxTokens) body.max_tokens = config.maxTokens
  if (config.temperature !== undefined) body.temperature = config.temperature

  const tools = convertToolsToOpenAIFormat(config.tools)
  if (tools) body.tools = tools

  // Pass tool_choice through — some models/providers require explicit "auto" to activate tool calling
  if (config.toolChoice && tools) {
    if (config.toolChoice.type === 'tool') {
      body.tool_choice = { type: 'function', function: { name: config.toolChoice.name } }
    } else {
      body.tool_choice = config.toolChoice.type // "auto" | "none"
    }
  }

  // Reasoning effort for o-series, DeepSeek-R1, and other reasoning models
  if (config.thinking?.enabled && config.thinking.effort) {
    body.reasoning_effort = config.thinking.effort
  }

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
  /** Transform messages before they are converted to OpenAI format. */
  transformMessages?: (messages: ChatMessage[]) => ChatMessage[]
  /** Transform the request body before it is sent to the API. */
  transformRequestBody?: (body: Record<string, unknown>) => Record<string, unknown>
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
    transformMessages,
    transformRequestBody,
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

      const transformedMessages = transformMessages ? transformMessages(messages) : messages
      let body = buildOpenAIRequestBody(transformedMessages, config, { model: defaultModel })
      if (transformRequestBody) body = transformRequestBody(body)

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
                  cacheReadTokens: event.usage.prompt_tokens_details?.cached_tokens || undefined,
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
