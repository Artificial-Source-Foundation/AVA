import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

const Client = createOpenAICompatClient({
  provider: 'ollama',
  displayName: 'Ollama',
  baseUrl: 'http://localhost:11434/v1',
  defaultModel: 'llama3.1',
  apiKeyHint: 'AVA_OLLAMA_API_KEY',
})

export function activate(api: ExtensionAPI) {
  return api.registerProvider('ollama', () => new Client())
}
