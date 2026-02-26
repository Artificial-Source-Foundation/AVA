import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { KimiClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('kimi', () => new KimiClient())
}
