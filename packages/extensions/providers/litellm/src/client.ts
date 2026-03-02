/**
 * LiteLLM Provider Client
 *
 * Uses OpenAI-compatible Chat Completions API via a LiteLLM proxy server.
 * Default base URL is http://localhost:4000/v1 (standard LiteLLM proxy port).
 */

import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

export const LiteLLMClient = createOpenAICompatClient({
  provider: 'litellm',
  displayName: 'LiteLLM',
  baseUrl: 'http://localhost:4000/v1',
  defaultModel: 'gpt-4o',
  apiKeyHint: 'AVA_LITELLM_API_KEY or LITELLM_API_KEY',
})
