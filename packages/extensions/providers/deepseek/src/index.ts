import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { DeepSeekClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('deepseek', () => new DeepSeekClient())
}
