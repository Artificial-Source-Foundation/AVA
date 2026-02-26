/**
 * Cohere Provider Client
 * Uses OpenAI-compatible Chat Completions API.
 */

import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

export const CohereClient = createOpenAICompatClient({
  provider: 'cohere',
  displayName: 'Cohere',
  baseUrl: 'https://api.cohere.com/v2',
  defaultModel: 'command-r-plus',
  apiKeyHint: 'AVA_COHERE_API_KEY',
})
