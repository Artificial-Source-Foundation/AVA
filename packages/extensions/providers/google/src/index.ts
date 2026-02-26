import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { GoogleClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('google', () => new GoogleClient())
}
