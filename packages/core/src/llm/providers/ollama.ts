/**
 * Ollama Provider Client
 * Integration with local Ollama server
 * https://github.com/ollama/ollama/blob/main/docs/api.md
 */

import type { ChatMessage, LLMProvider, ProviderConfig, StreamDelta } from '../../types/llm.js'
import { type LLMClient, registerClient } from '../client.js'

// Default Ollama URL (can be overridden via env)
const DEFAULT_BASE_URL = 'http://localhost:11434'
const PROVIDER: LLMProvider = 'ollama'

/**
 * Get Ollama base URL from environment or default
 */
function getBaseUrl(): string {
  return process.env.ESTELA_OLLAMA_URL ?? process.env.OLLAMA_HOST ?? DEFAULT_BASE_URL
}

/**
 * Ollama stream event type
 */
interface OllamaStreamEvent {
  model: string
  created_at: string
  message?: {
    role: string
    content: string
    tool_calls?: Array<{
      function: {
        name: string
        arguments: Record<string, unknown>
      }
    }>
  }
  done: boolean
  done_reason?: string
  total_duration?: number
  load_duration?: number
  prompt_eval_count?: number
  prompt_eval_duration?: number
  eval_count?: number
  eval_duration?: number
}

/**
 * Ollama client implementation
 * Connects to local Ollama server for running local models
 */
class OllamaClient implements LLMClient {
  async *stream(
    messages: ChatMessage[],
    config: ProviderConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown> {
    const baseUrl = getBaseUrl()

    // Build request body (Ollama format)
    const body: Record<string, unknown> = {
      model: config.model ?? 'llama3.2',
      messages: messages.map((m) => ({
        role: m.role,
        content: m.content,
      })),
      stream: true,
    }

    // Ollama uses 'options' for model parameters
    const options: Record<string, unknown> = {}

    if (config.maxTokens) {
      options.num_predict = config.maxTokens
    }

    if (config.temperature !== undefined) {
      options.temperature = config.temperature
    }

    if (Object.keys(options).length > 0) {
      body.options = options
    }

    // Ollama supports tools via native format
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
      response = await fetch(`${baseUrl}/api/chat`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
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
          message: `Cannot connect to Ollama at ${baseUrl}. Make sure Ollama is running: ${err instanceof Error ? err.message : String(err)}`,
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
          message: `Ollama error (${response.status}): ${text}`,
        },
      }
      return
    }

    // Process NDJSON stream (Ollama uses newline-delimited JSON)
    const reader = response.body?.getReader()
    if (!reader) {
      yield { content: '', done: true, error: { type: 'api', message: 'No response body' } }
      return
    }

    const decoder = new TextDecoder()
    let buffer = ''
    let toolCallCounter = 0

    try {
      while (true) {
        const { done, value } = await reader.read()
        if (done) break

        buffer += decoder.decode(value, { stream: true })
        const lines = buffer.split('\n')
        buffer = lines.pop() ?? ''

        for (const line of lines) {
          if (!line.trim()) continue

          try {
            const event: OllamaStreamEvent = JSON.parse(line)

            // Emit content
            if (event.message?.content) {
              yield { content: event.message.content }
            }

            // Emit tool calls
            if (event.message?.tool_calls) {
              for (const tc of event.message.tool_calls) {
                yield {
                  content: '',
                  toolUse: {
                    type: 'tool_use',
                    id: `ollama-tc-${toolCallCounter++}`,
                    name: tc.function.name,
                    input: tc.function.arguments,
                  },
                }
              }
            }

            // Emit usage on completion
            if (event.done && event.prompt_eval_count !== undefined) {
              yield {
                content: '',
                usage: {
                  inputTokens: event.prompt_eval_count,
                  outputTokens: event.eval_count ?? 0,
                },
              }
            }
          } catch {
            // Skip invalid JSON lines
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
registerClient(PROVIDER, OllamaClient)

export { OllamaClient }

/**
 * Check if Ollama is available
 */
export async function isOllamaAvailable(): Promise<boolean> {
  try {
    const baseUrl = getBaseUrl()
    const response = await fetch(`${baseUrl}/api/tags`, {
      method: 'GET',
      signal: AbortSignal.timeout(2000),
    })
    return response.ok
  } catch {
    return false
  }
}

/**
 * List available Ollama models
 */
export async function listOllamaModels(): Promise<string[]> {
  try {
    const baseUrl = getBaseUrl()
    const response = await fetch(`${baseUrl}/api/tags`)
    if (!response.ok) return []

    const data = (await response.json()) as { models?: Array<{ name: string }> }
    return data.models?.map((m) => m.name) ?? []
  } catch {
    return []
  }
}
