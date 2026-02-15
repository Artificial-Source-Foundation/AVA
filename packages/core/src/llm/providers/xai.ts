/**
 * xAI Provider Client
 * Direct integration with xAI (Grok) API
 * https://docs.x.ai/api
 */

import { createOpenAICompatClient } from '../utils/openai-compat.js'

createOpenAICompatClient({
  provider: 'xai',
  displayName: 'xAI',
  baseUrl: 'https://api.x.ai/v1',
  defaultModel: 'grok-beta',
  apiKeyHint: 'AVA_XAI_API_KEY',
})
