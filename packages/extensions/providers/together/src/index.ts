import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

const Client = createOpenAICompatClient({
  provider: 'together',
  displayName: 'Together',
  baseUrl: 'https://api.together.xyz/v1',
  defaultModel: 'meta-llama/Meta-Llama-3.1-70B-Instruct-Turbo',
  apiKeyHint: 'AVA_TOGETHER_API_KEY',
})

export function activate(api: ExtensionAPI) {
  return api.registerProvider('together', () => new Client())
}
