/**
 * Anthropic provider extension.
 * Registers Claude as an LLM provider.
 */

import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { AnthropicClient } from './client.js'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('anthropic', () => new AnthropicClient())
}
