import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { CopilotClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('copilot', () => new CopilotClient())
}
