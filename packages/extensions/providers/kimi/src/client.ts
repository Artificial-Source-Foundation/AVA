/**
 * Kimi (Moonshot AI) Provider Client
 * Uses OpenAI-compatible Chat Completions API.
 */

import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

export const KimiClient = createOpenAICompatClient({
  provider: 'kimi',
  displayName: 'Kimi',
  baseUrl: 'https://api.moonshot.cn/v1',
  defaultModel: 'moonshot-v1-8k',
  apiKeyHint: 'AVA_KIMI_API_KEY',
})
