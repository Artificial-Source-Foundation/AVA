/**
 * Together AI Provider Client
 * Uses OpenAI-compatible Chat Completions API.
 */

import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

export const TogetherClient = createOpenAICompatClient({
  provider: 'together',
  displayName: 'Together AI',
  baseUrl: 'https://api.together.xyz/v1',
  defaultModel: 'meta-llama/Meta-Llama-3.1-70B-Instruct-Turbo',
  apiKeyHint: 'AVA_TOGETHER_API_KEY',
})
