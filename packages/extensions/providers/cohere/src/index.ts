import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

const Client = createOpenAICompatClient({
  provider: 'cohere',
  displayName: 'Cohere',
  baseUrl: 'https://api.cohere.ai/v1',
  defaultModel: 'command-r-plus',
  apiKeyHint: 'AVA_COHERE_API_KEY',
})

export function activate(api: ExtensionAPI) {
  return api.registerProvider('cohere', () => new Client())
}
