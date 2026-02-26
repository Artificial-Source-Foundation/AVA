import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { OllamaClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('ollama', () => new OllamaClient())
}
