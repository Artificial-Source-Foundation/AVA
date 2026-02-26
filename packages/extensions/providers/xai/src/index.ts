import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

const Client = createOpenAICompatClient({
  provider: 'xai',
  displayName: 'xAI',
  baseUrl: 'https://api.x.ai/v1',
  defaultModel: 'grok-2',
  apiKeyHint: 'AVA_XAI_API_KEY',
})

export function activate(api: ExtensionAPI) {
  return api.registerProvider('xai', () => new Client())
}
