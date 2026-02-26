/**
 * Groq Provider Client
 * Uses OpenAI-compatible Chat Completions API.
 */

import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

export const GroqClient = createOpenAICompatClient({
  provider: 'groq',
  displayName: 'Groq',
  baseUrl: 'https://api.groq.com/openai/v1',
  defaultModel: 'llama-3.3-70b-versatile',
  apiKeyHint: 'AVA_GROQ_API_KEY',
})
