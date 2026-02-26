import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

const Client = createOpenAICompatClient({
  provider: 'deepseek',
  displayName: 'DeepSeek',
  baseUrl: 'https://api.deepseek.com/v1',
  defaultModel: 'deepseek-chat',
  apiKeyHint: 'AVA_DEEPSEEK_API_KEY',
})

export function activate(api: ExtensionAPI) {
  return api.registerProvider('deepseek', () => new Client())
}
