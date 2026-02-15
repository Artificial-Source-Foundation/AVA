/**
 * Together AI Provider Client
 * Direct integration with Together AI API
 * https://docs.together.ai/reference/chat-completions
 */

import { createOpenAICompatClient } from '../utils/openai-compat.js'

createOpenAICompatClient({
  provider: 'together',
  displayName: 'Together AI',
  baseUrl: 'https://api.together.xyz/v1',
  defaultModel: 'meta-llama/Meta-Llama-3.1-70B-Instruct-Turbo',
  apiKeyHint: 'AVA_TOGETHER_API_KEY',
})
