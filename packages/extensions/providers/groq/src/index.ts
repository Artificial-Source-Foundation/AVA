import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { GroqClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('groq', () => new GroqClient())
}
