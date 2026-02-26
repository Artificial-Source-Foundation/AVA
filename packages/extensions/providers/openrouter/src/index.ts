import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { OpenRouterClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('openrouter', () => new OpenRouterClient())
}
