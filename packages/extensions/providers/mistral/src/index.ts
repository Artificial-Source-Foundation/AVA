import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { MistralClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('mistral', () => new MistralClient())
}
