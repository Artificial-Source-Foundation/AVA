import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

const Client = createOpenAICompatClient({
  provider: 'groq',
  displayName: 'Groq',
  baseUrl: 'https://api.groq.com/openai/v1',
  defaultModel: 'llama-3.3-70b-versatile',
  apiKeyHint: 'AVA_GROQ_API_KEY',
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

export function activate(api: ExtensionAPI) {
  return api.registerProvider('groq', () => new Client())
}
