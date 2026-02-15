/**
 * Cohere Provider Client
 * Direct integration with Cohere API (non-OpenAI-compatible format)
 * https://docs.cohere.com/reference/chat
 */

import type { ChatMessage, ProviderConfig, StreamDelta } from '../../types/llm.js'
import { getApiKey, type LLMClient, registerClient } from '../client.js'
import { buildHttpError, parseRetryAfter } from '../utils/errors.js'
import { convertToolsToOpenAIFormat } from '../utils/openai-compat.js'
import { readSSEStream } from '../utils/sse.js'

const BASE_URL = 'https://api.cohere.com/v2'

interface CohereStreamEvent {
  type: string
  delta?: {
    message?: {
      content?: { text?: string }
      tool_calls?: Array<{
        id: string
        type: string
        function: { name: string; arguments: string }
      }>
      usage?: {
        billed_units: { input_tokens: number; output_tokens: number }
      }
    }
  }
}

class CohereClient implements LLMClient {
  async *stream(
    messages: ChatMessage[],
    config: ProviderConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown> {
    const apiKey = await getApiKey('cohere')

    if (!apiKey) {
      yield {
        content: '',
        done: true,
        error: { type: 'auth', message: 'No Cohere API key configured. Set AVA_COHERE_API_KEY.' },
      }
      return
    }

    // Cohere uses preamble for system messages
    const systemMessage = messages.find((m) => m.role === 'system')
    const conversationMessages = messages.filter((m) => m.role !== 'system')

    const body: Record<string, unknown> = {
      model: config.model ?? 'command-r-plus',
      messages: conversationMessages.map((m) => ({
        role: m.role === 'assistant' ? 'assistant' : 'user',
        content: m.content,
      })),
      stream: true,
    }

    if (systemMessage) body.preamble = systemMessage.content
    if (config.maxTokens) body.max_tokens = config.maxTokens
    if (config.temperature !== undefined) body.temperature = config.temperature

    const tools = convertToolsToOpenAIFormat(config.tools)
    if (tools) body.tools = tools

    let response: Response
    try {
      response = await fetch(`${BASE_URL}/chat`, {
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
      const error = buildHttpError(response.status, text, 'Cohere')
      error.retryAfter = parseRetryAfter(response.headers.get('retry-after'))
      yield { content: '', done: true, error }
      return
    }

    const reader = response.body?.getReader()
    if (!reader) {
      yield { content: '', done: true, error: { type: 'api', message: 'No response body' } }
      return
    }

    const toolCallBuffer: Record<string, { name: string; arguments: string }> = {}

    for await (const dataLines of readSSEStream(reader)) {
      for (const data of dataLines) {
        try {
          const event: CohereStreamEvent = JSON.parse(data)

          if (event.type === 'content-delta' && event.delta?.message?.content?.text) {
            yield { content: event.delta.message.content.text }
          }

          if (event.type === 'tool-call-start' && event.delta?.message?.tool_calls) {
            for (const tc of event.delta.message.tool_calls) {
              toolCallBuffer[tc.id] = { name: tc.function.name, arguments: '' }
            }
          }

          if (event.type === 'tool-call-delta' && event.delta?.message?.tool_calls) {
            for (const tc of event.delta.message.tool_calls) {
              if (toolCallBuffer[tc.id]) {
                toolCallBuffer[tc.id].arguments += tc.function.arguments
              }
            }
          }

          if (event.type === 'message-end' && event.delta?.message?.usage) {
            yield {
              content: '',
              usage: {
                inputTokens: event.delta.message.usage.billed_units.input_tokens,
                outputTokens: event.delta.message.usage.billed_units.output_tokens,
              },
            }
          }
        } catch {
          // Skip invalid JSON
        }
      }
    }

    // Emit accumulated tool calls
    for (const [id, tc] of Object.entries(toolCallBuffer)) {
      try {
        yield {
          content: '',
          toolUse: { type: 'tool_use', id, name: tc.name, input: JSON.parse(tc.arguments || '{}') },
        }
      } catch {
        // Skip invalid tool call JSON
      }
    }

    yield { content: '', done: true }
  }
}

registerClient('cohere', CohereClient)

export { CohereClient }
