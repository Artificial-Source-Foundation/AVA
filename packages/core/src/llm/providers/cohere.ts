/**
 * Cohere Provider Client
 * Direct integration with Cohere API
 * https://docs.cohere.com/reference/chat
 */

import type { ChatMessage, LLMProvider, ProviderConfig, StreamDelta } from '../../types/llm.js'
import { getApiKey, type LLMClient, registerClient } from '../client.js'

const BASE_URL = 'https://api.cohere.com/v2'
const PROVIDER: LLMProvider = 'cohere'

/**
 * Cohere stream event types
 */
interface CohereStreamEvent {
  type:
    | 'message-start'
    | 'content-start'
    | 'content-delta'
    | 'content-end'
    | 'tool-plan-delta'
    | 'tool-call-start'
    | 'tool-call-delta'
    | 'tool-call-end'
    | 'message-end'
  delta?: {
    message?: {
      content?: {
        text?: string
      }
      tool_calls?: Array<{
        id: string
        type: string
        function: {
          name: string
          arguments: string
        }
      }>
      usage?: {
        billed_units: {
          input_tokens: number
          output_tokens: number
        }
      }
    }
  }
  index?: number
}

/**
 * Cohere client implementation
 * Uses Cohere Chat API with streaming
 */
class CohereClient implements LLMClient {
  async *stream(
    messages: ChatMessage[],
    config: ProviderConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown> {
    const apiKey = await getApiKey(PROVIDER)

    if (!apiKey) {
      yield {
        content: '',
        done: true,
        error: {
          type: 'auth',
          message: 'No Cohere API key configured. Set ESTELA_COHERE_API_KEY.',
        },
      }
      return
    }

    // Separate system message for preamble
    const systemMessage = messages.find((m) => m.role === 'system')
    const conversationMessages = messages.filter((m) => m.role !== 'system')

    // Build request body (Cohere format)
    const body: Record<string, unknown> = {
      model: config.model ?? 'command-r-plus',
      messages: conversationMessages.map((m) => ({
        role: m.role === 'assistant' ? 'assistant' : 'user',
        content: m.content,
      })),
      stream: true,
    }

    if (systemMessage) {
      body.preamble = systemMessage.content
    }

    if (config.maxTokens) {
      body.max_tokens = config.maxTokens
    }

    if (config.temperature !== undefined) {
      body.temperature = config.temperature
    }

    if (config.tools && config.tools.length > 0) {
      body.tools = config.tools.map((t) => ({
        type: 'function',
        function: {
          name: t.name,
          description: t.description,
          parameters: t.input_schema,
        },
      }))
    }

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
      yield {
        content: '',
        done: true,
        error: {
          type: 'api',
          message: `Cohere API error (${response.status}): ${text}`,
        },
      }
      return
    }

    // Process SSE stream
    const reader = response.body?.getReader()
    if (!reader) {
      yield { content: '', done: true, error: { type: 'api', message: 'No response body' } }
      return
    }

    const decoder = new TextDecoder()
    let buffer = ''
    const toolCallBuffer: Record<string, { name: string; arguments: string }> = {}

    try {
      while (true) {
        const { done, value } = await reader.read()
        if (done) break

        buffer += decoder.decode(value, { stream: true })
        const lines = buffer.split('\n')
        buffer = lines.pop() ?? ''

        for (const line of lines) {
          if (!line.startsWith('data: ')) continue
          const data = line.slice(6).trim()
          if (!data) continue

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
            toolUse: {
              type: 'tool_use',
              id,
              name: tc.name,
              input: JSON.parse(tc.arguments || '{}'),
            },
          }
        } catch {
          // Skip invalid tool call JSON
        }
      }

      yield { content: '', done: true }
    } finally {
      reader.releaseLock()
    }
  }
}

// Register provider
registerClient(PROVIDER, CohereClient)

export { CohereClient }
