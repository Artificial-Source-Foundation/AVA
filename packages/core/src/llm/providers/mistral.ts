/**
 * Mistral AI Provider Client
 * Direct integration with Mistral API
 * https://docs.mistral.ai/api/
 */

import type { ChatMessage, LLMProvider, ProviderConfig, StreamDelta } from '../../types/llm.js'
import { getApiKey, type LLMClient, registerClient } from '../client.js'

const BASE_URL = 'https://api.mistral.ai/v1'
const PROVIDER: LLMProvider = 'mistral'

/**
 * Mistral stream event types
 */
interface MistralStreamEvent {
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
        id: string
        type: string
        function: {
          name: string
          arguments: string
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

/**
 * Mistral client implementation
 * Uses OpenAI-compatible Chat Completions API
 */
class MistralClient implements LLMClient {
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
          message: 'No Mistral API key configured. Set ESTELA_MISTRAL_API_KEY.',
        },
      }
      return
    }

    // Build request body (OpenAI-compatible format)
    const body: Record<string, unknown> = {
      model: config.model ?? 'mistral-large-latest',
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
      response = await fetch(`${BASE_URL}/chat/completions`, {
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
          message: `Mistral API error (${response.status}): ${text}`,
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
          if (data === '[DONE]') continue
          if (!data) continue

          try {
            const event: MistralStreamEvent = JSON.parse(data)
            const choice = event.choices[0]

            if (choice?.delta?.content) {
              yield { content: choice.delta.content }
            }

            if (choice?.delta?.tool_calls) {
              for (const tc of choice.delta.tool_calls) {
                if (!toolCallBuffer[tc.id]) {
                  toolCallBuffer[tc.id] = { name: tc.function.name, arguments: '' }
                }
                toolCallBuffer[tc.id].arguments += tc.function.arguments
              }
            }

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
      for (const [id, tc] of Object.entries(toolCallBuffer)) {
        try {
          yield {
            content: '',
            toolUse: {
              type: 'tool_use',
              id,
              name: tc.name,
              input: JSON.parse(tc.arguments),
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
registerClient(PROVIDER, MistralClient)

export { MistralClient }
