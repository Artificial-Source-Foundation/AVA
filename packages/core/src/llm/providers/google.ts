/**
 * Google Gemini Provider Client
 * Direct integration with Google AI (Gemini models)
 * Supports both API key and OAuth (Antigravity) authentication
 * https://ai.google.dev/gemini-api/docs
 */

import type { ChatMessage, LLMProvider, ProviderConfig, StreamDelta } from '../../types/llm.js'
import { getAuth, type LLMClient, registerClient } from '../client.js'

const BASE_URL = 'https://generativelanguage.googleapis.com/v1beta'
const PROVIDER: LLMProvider = 'google'

interface GeminiStreamEvent {
  candidates?: Array<{
    content?: {
      parts?: Array<{ text?: string }>
    }
    finishReason?: string
  }>
  usageMetadata?: {
    promptTokenCount: number
    candidatesTokenCount: number
    totalTokenCount: number
  }
}

/**
 * Google Gemini client implementation
 * Uses Gemini API with streaming
 */
class GoogleClient implements LLMClient {
  async *stream(
    messages: ChatMessage[],
    config: ProviderConfig,
    signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown> {
    const auth = await getAuth(PROVIDER)

    if (!auth) {
      yield {
        content: '',
        done: true,
        error: {
          type: 'auth',
          message:
            'No Google authentication configured. Set ESTELA_GOOGLE_API_KEY or use `estela auth google` for OAuth.',
        },
      }
      return
    }

    // Convert messages to Gemini format
    const systemMessage = messages.find((m) => m.role === 'system')
    const conversationMessages = messages.filter((m) => m.role !== 'system')

    const contents = conversationMessages.map((m) => ({
      role: m.role === 'assistant' ? 'model' : 'user',
      parts: [{ text: m.content }],
    }))

    // Build request body
    const body: Record<string, unknown> = {
      contents,
      generationConfig: {
        maxOutputTokens: config.maxTokens || 4096,
        temperature: config.temperature ?? 0.7,
      },
    }

    // Add system instruction if present
    if (systemMessage) {
      body.systemInstruction = {
        parts: [{ text: systemMessage.content }],
      }
    }

    // Build URL with model
    const model = config.model || 'gemini-1.5-flash'
    let url: string

    if (auth.type === 'oauth') {
      // OAuth uses Bearer token
      url = `${BASE_URL}/models/${model}:streamGenerateContent?alt=sse`
    } else {
      // API key in URL
      url = `${BASE_URL}/models/${model}:streamGenerateContent?key=${auth.token}&alt=sse`
    }

    const headers: Record<string, string> = {
      'Content-Type': 'application/json',
    }

    if (auth.type === 'oauth') {
      headers.Authorization = `Bearer ${auth.token}`
    }

    // Make request
    let response: Response
    try {
      response = await fetch(url, {
        method: 'POST',
        headers,
        body: JSON.stringify(body),
        signal,
      })
    } catch (err) {
      if (err instanceof Error && err.name === 'AbortError') {
        return
      }
      yield {
        content: '',
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
      yield {
        content: '',
        done: true,
        error: {
          type: getErrorType(response.status),
          message: getErrorMessage(response.status, errorBody),
          status: response.status,
        },
      }
      return
    }

    const reader = response.body?.getReader()
    if (!reader) {
      yield {
        content: '',
        done: true,
        error: { type: 'unknown', message: 'No response body' },
      }
      return
    }

    const decoder = new TextDecoder()
    let buffer = ''
    let inputTokens = 0
    let outputTokens = 0

    try {
      while (true) {
        const { done, value } = await reader.read()
        if (done) break

        buffer += decoder.decode(value, { stream: true })
        const lines = buffer.split('\n')
        buffer = lines.pop() || ''

        for (const line of lines) {
          if (!line.trim()) continue
          if (!line.startsWith('data: ')) continue

          const data = line.slice(6).trim()
          if (!data) continue

          try {
            const event = JSON.parse(data) as GeminiStreamEvent

            // Extract text from candidates
            const text = event.candidates?.[0]?.content?.parts?.[0]?.text
            if (text) {
              yield { content: text, done: false }
            }

            // Update usage if provided
            if (event.usageMetadata) {
              inputTokens = event.usageMetadata.promptTokenCount
              outputTokens = event.usageMetadata.candidatesTokenCount
            }

            // Check for finish
            if (event.candidates?.[0]?.finishReason) {
              yield {
                content: '',
                done: true,
                usage: { inputTokens, outputTokens, totalTokens: inputTokens + outputTokens },
              }
              return
            }
          } catch {
            console.warn('Failed to parse SSE data:', data)
          }
        }
      }

      yield {
        content: '',
        done: true,
        usage: { inputTokens, outputTokens, totalTokens: inputTokens + outputTokens },
      }
    } finally {
      reader.releaseLock()
    }
  }
}

function getErrorType(status: number): 'rate_limit' | 'auth' | 'server' | 'unknown' {
  if (status === 401 || status === 403) return 'auth'
  if (status === 429) return 'rate_limit'
  if (status >= 500) return 'server'
  return 'unknown'
}

function getErrorMessage(status: number, body: string): string {
  try {
    const parsed = JSON.parse(body)
    if (parsed.error?.message) return parsed.error.message
  } catch {
    // Use default messages
  }

  const messages: Record<number, string> = {
    400: 'Invalid request',
    401: 'Invalid API key',
    403: 'Access forbidden',
    404: 'Model not found',
    429: 'Rate limit exceeded',
    500: 'Server error',
    503: 'Service overloaded',
  }

  return messages[status] || `HTTP ${status}`
}

registerClient('google', GoogleClient)
