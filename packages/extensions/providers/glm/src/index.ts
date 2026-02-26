import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { GLMClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('glm', () => new GLMClient())
}
