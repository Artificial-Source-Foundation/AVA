/**
 * xAI Provider Client
 * Uses OpenAI-compatible Chat Completions API.
 */

import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

export const XAIClient = createOpenAICompatClient({
  provider: 'xai',
  displayName: 'xAI',
  baseUrl: 'https://api.x.ai/v1',
  defaultModel: 'grok-2',
  apiKeyHint: 'AVA_XAI_API_KEY',
})
