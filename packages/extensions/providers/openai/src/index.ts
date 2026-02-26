import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { OpenAIClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('openai', () => new OpenAIClient())
}
