/**
 * Mistral AI Provider Client
 * Direct integration with Mistral API
 * https://docs.mistral.ai/api/
 */

import { createOpenAICompatClient } from '../utils/openai-compat.js'

createOpenAICompatClient({
  provider: 'mistral',
  displayName: 'Mistral',
  baseUrl: 'https://api.mistral.ai/v1',
  defaultModel: 'mistral-large-latest',
  apiKeyHint: 'AVA_MISTRAL_API_KEY',
})
