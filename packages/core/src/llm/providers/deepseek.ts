/**
 * DeepSeek Provider Client
 * Direct integration with DeepSeek API
 * https://platform.deepseek.com/api-docs
 */

import { createOpenAICompatClient } from '../utils/openai-compat.js'

createOpenAICompatClient({
  provider: 'deepseek',
  displayName: 'DeepSeek',
  baseUrl: 'https://api.deepseek.com/v1',
  defaultModel: 'deepseek-chat',
  apiKeyHint: 'AVA_DEEPSEEK_API_KEY',
})
