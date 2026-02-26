import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { TogetherClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('together', () => new TogetherClient())
}
