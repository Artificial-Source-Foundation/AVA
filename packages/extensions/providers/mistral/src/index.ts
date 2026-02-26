import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

const Client = createOpenAICompatClient({
  provider: 'mistral',
  displayName: 'Mistral',
  baseUrl: 'https://api.mistral.ai/v1',
  defaultModel: 'mistral-large-latest',
  apiKeyHint: 'AVA_MISTRAL_API_KEY',
})

export function activate(api: ExtensionAPI) {
  return api.registerProvider('mistral', () => new Client())
}
