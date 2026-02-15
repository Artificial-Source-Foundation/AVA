/**
 * Groq Provider Client
 * Direct integration with Groq API (fast inference)
 * https://console.groq.com/docs/api-reference
 */

import { createOpenAICompatClient } from '../utils/openai-compat.js'

createOpenAICompatClient({
  provider: 'groq',
  displayName: 'Groq',
  baseUrl: 'https://api.groq.com/openai/v1',
  defaultModel: 'llama-3.3-70b-versatile',
  apiKeyHint: 'AVA_GROQ_API_KEY',
  // Groq reports usage in x_groq.usage instead of standard usage field
  extractUsage: (event: Record<string, unknown>) => {
    const xGroq = event.x_groq as
      | { usage?: { prompt_tokens: number; completion_tokens: number } }
      | undefined
    if (xGroq?.usage) {
      return {
        inputTokens: xGroq.usage.prompt_tokens,
        outputTokens: xGroq.usage.completion_tokens,
      }
    }
    return null
  },
})
