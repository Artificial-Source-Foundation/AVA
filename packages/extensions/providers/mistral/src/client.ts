/**
 * Mistral Provider Client
 * Uses OpenAI-compatible Chat Completions API.
 */

import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

export const MistralClient = createOpenAICompatClient({
  provider: 'mistral',
  displayName: 'Mistral',
  baseUrl: 'https://api.mistral.ai/v1',
  defaultModel: 'mistral-large-latest',
  apiKeyHint: 'AVA_MISTRAL_API_KEY',
})
