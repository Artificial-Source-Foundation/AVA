/**
 * GLM (Zhipu AI) Provider Client
 * Uses OpenAI-compatible Chat Completions API.
 */

import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

export const GLMClient = createOpenAICompatClient({
  provider: 'glm',
  displayName: 'GLM',
  baseUrl: 'https://open.bigmodel.cn/api/paas/v4',
  defaultModel: 'glm-4-flash',
  apiKeyHint: 'AVA_GLM_API_KEY',
})
