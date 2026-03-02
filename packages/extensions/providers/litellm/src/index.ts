import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { LiteLLMClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('litellm', () => new LiteLLMClient())
}
