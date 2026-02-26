import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { CohereClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('cohere', () => new CohereClient())
}
