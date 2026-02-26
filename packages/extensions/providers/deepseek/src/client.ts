/**
 * DeepSeek Provider Client
 * Uses OpenAI-compatible Chat Completions API.
 */

import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

export const DeepSeekClient = createOpenAICompatClient({
  provider: 'deepseek',
  displayName: 'DeepSeek',
  baseUrl: 'https://api.deepseek.com/v1',
  defaultModel: 'deepseek-chat',
  apiKeyHint: 'AVA_DEEPSEEK_API_KEY',
})
