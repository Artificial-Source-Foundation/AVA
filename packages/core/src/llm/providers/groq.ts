/**
 * Groq Provider Client
 * Direct integration with Groq API (fast inference)
 * https://console.groq.com/docs/api-reference
 */

import type { ChatMessage, LLMProvider, ProviderConfig, StreamDelta } from '../../types/llm.js'
import { getApiKey, type LLMClient, registerClient } from '../client.js'

const BASE_URL = 'https://api.groq.com/openai/v1'
const PROVIDER: LLMProvider = 'groq'

/**
 * Groq stream event types (OpenAI-compatible)
 */
interface GroqStreamEvent {
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
  x_groq?: {
    id: string
    usage: {
      queue_time: number
      prompt_tokens: number
      prompt_time: number
      completion_tokens: number
      completion_time: number
      total_tokens: number
      total_time: number
    }
  }
}

/**
 * Groq client implementation
 * Uses OpenAI-compatible Chat Completions API with ultra-fast inference
 */
class GroqClient implements LLMClient {
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
          message: 'No Groq API key configured. Set ESTELA_GROQ_API_KEY.',
        },
      }
      return
    }

    // Build request body
    const body: Record<string, unknown> = {
      model: config.model ?? 'llama-3.3-70b-versatile',
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
          message: `Groq API error (${response.status}): ${text}`,
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
    const toolCallBuffer: Record<number, { id: string; name: string; arguments: string }> = {}

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
            const event: GroqStreamEvent = JSON.parse(data)
            const choice = event.choices[0]

            if (choice?.delta?.content) {
              yield { content: choice.delta.content }
            }

            if (choice?.delta?.tool_calls) {
              for (const tc of choice.delta.tool_calls) {
                if (!toolCallBuffer[tc.index]) {
                  toolCallBuffer[tc.index] = {
                    id: tc.id ?? '',
                    name: tc.function?.name ?? '',
                    arguments: '',
                  }
                }
                if (tc.id) toolCallBuffer[tc.index].id = tc.id
                if (tc.function?.name) toolCallBuffer[tc.index].name = tc.function.name
                if (tc.function?.arguments)
                  toolCallBuffer[tc.index].arguments += tc.function.arguments
              }
            }

            if (event.x_groq?.usage) {
              yield {
                content: '',
                usage: {
                  inputTokens: event.x_groq.usage.prompt_tokens,
                  outputTokens: event.x_groq.usage.completion_tokens,
                },
              }
            }
          } catch {
            // Skip invalid JSON
          }
        }
      }

      // Emit accumulated tool calls
      for (const tc of Object.values(toolCallBuffer)) {
        if (tc.id && tc.name) {
          try {
            yield {
              content: '',
              toolUse: {
                type: 'tool_use',
                id: tc.id,
                name: tc.name,
                input: JSON.parse(tc.arguments || '{}'),
              },
            }
          } catch {
            // Skip invalid tool call JSON
          }
        }
      }

      yield { content: '', done: true }
    } finally {
      reader.releaseLock()
    }
  }
}

// Register provider
registerClient(PROVIDER, GroqClient)

export { GroqClient }
